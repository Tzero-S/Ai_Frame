// GHUB 虚拟 HID 通道实机探针
//
// 目的：验证「CreateFile + WriteFile 4 字节报告」能否真正驱动光标。
// 依据：docs/BeeEdit可复用情报.md §1.1/§1.2（设备路径与报告格式）。
//
// 行为：依次尝试候选设备路径 → 打开 → 读光标位置 → 发 (+N,0) → 再读 → 发 (-N,0) 复位。
// 只移动几个像素并立刻复位，不会留下副作用。
//
// 构建：
//   g++ -O2 -o ghub_probe.exe ghub_probe.cpp -lgdi32
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Candidate {
    const char* path;
    const char* note;
};

const Candidate kCandidates[] = {
    { "\\??\\ROOT#SYSTEM#0002#{1abc05c0-c378-41b9-9cef-df1aba82b015}", "LGS 主设备" },
    { "\\??\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}", "LGS 备设备" },
    { "\\\\?\\ROOT#SYSTEM#0002#{1abc05c0-c378-41b9-9cef-df1aba82b015}", "Win32 前缀写法" },
    { "\\\\?\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}", "Win32 前缀写法(备)" },
};

POINT Cursor() {
    POINT p{};
    GetCursorPos(&p);
    return p;
}

bool SendReport(HANDLE h, int dx, int dy) {
    // 报告格式：[report_id=0, dx(int8), dy(int8), 0]
    unsigned char buf[4] = { 0,
                             static_cast<unsigned char>(static_cast<signed char>(dx)),
                             static_cast<unsigned char>(static_cast<signed char>(dy)),
                             0 };
    DWORD written = 0;
    return WriteFile(h, buf, sizeof(buf), &written, nullptr) && written == sizeof(buf);
}

} // namespace

int main() {
    std::printf("=== GHUB 虚拟 HID 实机探针 ===\n");

    int ok_open = 0;
    int ok_move = 0;

    for (const auto& c : kCandidates) {
        std::printf("\n[尝试] %s\n          %s\n", c.path, c.note);

        HANDLE h = CreateFileA(c.path, GENERIC_WRITE | GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD e = GetLastError();
            std::printf("  打开失败: GetLastError=%lu", e);
            if (e == ERROR_FILE_NOT_FOUND) std::printf("（设备不存在 → 未装对应驱动）");
            if (e == ERROR_ACCESS_DENIED) std::printf("（权限不足 → 需管理员或设备被占用）");
            std::printf("\n");
            continue;
        }
        ++ok_open;
        std::printf("  打开成功 handle=%p\n", h);

        // 复位到屏幕中心附近再做实验，避免贴边导致位移被裁
        SetCursorPos(960, 540);
        Sleep(20);
        const POINT before = Cursor();

        if (!SendReport(h, 5, 0)) {
            std::printf("  WriteFile 失败: GetLastError=%lu\n", GetLastError());
            CloseHandle(h);
            continue;
        }
        Sleep(30);
        const POINT after = Cursor();
        const int moved = after.x - before.x;

        // 立即复位
        SendReport(h, -moved, 0);
        Sleep(20);

        std::printf("  发送 (+5,0) → 光标 X %ld → %ld（实测位移 %d）\n",
                    static_cast<long>(before.x), static_cast<long>(after.x), moved);
        if (moved != 0) {
            ++ok_move;
            std::printf("  ✅ 通道可用（位移已生效并复位）\n");
        } else {
            std::printf("  ⚠️ 写成功但光标未动（可能被系统加速/边界裁剪，或该设备不转发）\n");
        }
        CloseHandle(h);
    }

    std::printf("\n=== 结论 ===\n");
    std::printf("可打开设备数 = %d，可驱动光标数 = %d\n", ok_open, ok_move);
    if (ok_move > 0) {
        std::printf("GHUB 通道：实机验证通过\n");
        return 0;
    }
    if (ok_open > 0) {
        std::printf("GHUB 通道：设备可打开但未驱动光标（需进一步确认 IOCTL/报告格式）\n");
        return 1;
    }
    std::printf("GHUB 通道：设备不可打开（未安装 LGS/驱动未加载）\n");
    return 2;
}
