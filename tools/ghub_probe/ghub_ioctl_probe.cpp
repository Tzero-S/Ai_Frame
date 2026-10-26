// GHUB 虚拟 HID 通道实机探针 v2：用 IOCTL 驱动光标
//
// 背景：v1 验证了设备可打开，但 WriteFile 返回 ERROR_INVALID_FUNCTION(1)，
// 说明该设备不接受标准 HID 写入，必须走 DeviceIoControl。
//
// IOCTL 候选来自 gh.dll 反汇编（docs/BeeEdit可复用情报.md §1.4 遗留项）：
//   0x2A2010    —— 位移函数内的栈传参常数（输入缓冲 4 字节 [dx,dy,0,0]）
//   0x40100000  —— 初始化函数内的 edx 常数（输入缓冲 0x60 字节）
//
// 只移动几个像素并立刻复位。
#include <windows.h>

#include <cstdio>

namespace {

const char* kDev =
    "\\??\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}";

POINT Cursor() {
    POINT p{};
    GetCursorPos(&p);
    return p;
}

// 返回：0=成功且光标动了；1=IOCTL 成功但光标没动；2=IOCTL 失败
int TryIoctl(HANDLE h, DWORD code, int dx, int dy, const char* tag) {
    unsigned char in[4] = { static_cast<unsigned char>(static_cast<signed char>(dx)),
                            static_cast<unsigned char>(static_cast<signed char>(dy)),
                            0, 0 };
    DWORD ret = 0;
    SetCursorPos(960, 540);
    Sleep(20);
    const POINT before = Cursor();

    const BOOL ok = DeviceIoControl(h, code, in, sizeof(in), nullptr, 0, &ret, nullptr);
    const DWORD err = ok ? 0 : GetLastError();
    Sleep(30);
    const POINT after = Cursor();
    const int moved = after.x - before.x;

    if (!ok) {
        std::printf("  %s IOCTL 0x%08lX → 失败 GetLastError=%lu\n", tag,
                    static_cast<unsigned long>(code), static_cast<unsigned long>(err));
        return 2;
    }
    std::printf("  %s IOCTL 0x%08lX → 成功 (ret=%lu)，光标 X %ld→%ld（位移 %d）\n", tag,
                static_cast<unsigned long>(code), static_cast<unsigned long>(ret),
                static_cast<long>(before.x), static_cast<long>(after.x), moved);
    if (moved != 0) {
        // 复位
        unsigned char back[4] = { static_cast<unsigned char>(static_cast<signed char>(-moved)),
                                  0, 0, 0 };
        DWORD r2 = 0;
        DeviceIoControl(h, code, back, sizeof(back), nullptr, 0, &r2, nullptr);
        Sleep(20);
        return 0;
    }
    return 1;
}

} // namespace

int main() {
    std::printf("=== GHUB IOCTL 实机探针 v2 ===\n设备: %s\n\n", kDev);

    HANDLE h = CreateFileA(kDev, GENERIC_WRITE | GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("打开失败 GetLastError=%lu\n", GetLastError());
        return 2;
    }
    std::printf("打开成功 handle=%p\n\n", h);

    int best = 2;
    const struct { DWORD code; const char* tag; } cands[] = {
        { 0x002A2010, "[位移]" },
        { 0x40100000, "[初始化]" },
    };
    for (const auto& c : cands) {
        const int r = TryIoctl(h, c.code, 5, 0, c.tag);
        if (r < best) best = r;
    }

    CloseHandle(h);
    std::printf("\n=== 结论 ===\n");
    if (best == 0) std::printf("GHUB 通道：实机验证通过（IOCTL 可驱动光标）\n");
    else if (best == 1) std::printf("GHUB 通道：IOCTL 被接受但光标未动（需确认设备实例/权限）\n");
    else std::printf("GHUB 通道：IOCTL 均被拒绝（该设备实例不接受位移指令）\n");
    return best;
}
