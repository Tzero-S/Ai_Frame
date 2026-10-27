#include <algorithm>
#include <chrono>
#include <conio.h>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <cwctype>
#endif

#include "core/builtins.h"
#include "core/clock.h"
#include "core/config.h"
#include "core/config_schema.h"
#include "core/identity.h"
#include "core/config_store.h"
#include "core/json.h"
#include "core/logger.h"
#include "core/registry.h"
#include "core/run_loop.h"
#include "core/runtime.h"
#include "core/settings_builder.h"
#include "core/spi.h"
#include "fov/resolution_detect.h"
#include "inference/backend_ort.h"
#include "inference/model_validator.h"
#include "inference/preview.h"
#include "ipc/handlers.h"
#include "ipc/handlers_builtin.h"
#include "ipc/pipe_server.h"
#include "ipc/protocol.h"

namespace {

#ifdef _WIN32
// §3.1 ORT 抢加载诊断：onnxruntime.dll 以静态导入形式进入 PE 导入表，
// Windows loader 会在 main() 之前完成解析；System32 自带的 1.10 可能盖过
// exe 同目录的 1.20.1。构建期已由 POST_BUILD 拷贝，此处校验实际加载路径，
// 不一致时向 stderr 告警（不阻断启动）。
void ReportOrtDllSource() {
    HMODULE mod = GetModuleHandleW(L"onnxruntime.dll");
    if (!mod) return;  // 未加载（或改用静态/延迟加载）

    wchar_t dll_path[MAX_PATH] = {};
    wchar_t exe_path[MAX_PATH] = {};
    if (!GetModuleFileNameW(mod, dll_path, MAX_PATH)) return;
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return;

    std::wstring dll_dir(dll_path);
    std::wstring exe_dir(exe_path);
    const auto cut_dir = [](std::wstring& p) {
        const auto pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p.resize(pos);
    };
    cut_dir(dll_dir);
    cut_dir(exe_dir);

    const auto ieq = [](const std::wstring& a, const std::wstring& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::towlower(a[i]) != std::towlower(b[i])) return false;
        }
        return true;
    };

    if (!ieq(dll_dir, exe_dir)) {
        std::fwprintf(stderr,
                      L"WARN: onnxruntime.dll resolved outside exe dir: %ls\n"
                      L"      expected 1.20.1 next to the exe; System32 1.10 may shadow it.\n",
                      dll_path);
    }
}

// Ctrl+C / 关闭窗口 → 请求运行循环优雅退出（§4.2）
aim::RunLoop* g_run_loop = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) &&
        g_run_loop) {
        g_run_loop->RequestStop();
        return TRUE;
    }
    return FALSE;
}
#endif

// 模型来源优先级：--model > AIM_VALORANT_MODEL > AIM_DEFAULT_MODEL > 文档 §3.1 本机预置路径（存在才用）
std::string ResolveModelPath(const std::string& explicit_path) {
    if (!explicit_path.empty()) return explicit_path;
    if (const char* env = std::getenv("AIM_VALORANT_MODEL"); env && *env) return env;
    if (const char* def = std::getenv("AIM_DEFAULT_MODEL"); def && *def) return def;
    const char* kLocal =
        "D:/MyProject/Test.Project/test/projects/VALORANT_aimbot/model/best_s960_fp16_dml.onnx";
    if (std::FILE* f = std::fopen(kLocal, "rb")) {
        std::fclose(f);
        return kLocal;
    }
    return {};
}

} // namespace

// 模型探测（§5.10）：统一入口，既认明文 ONNX，也认第三方加密容器 mbonx。
// mbonx 只能报元信息——密钥在样本进程内按机器码派生，离线无法解密。
static int ProbeModel(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::printf("probe: 打不开 %s\n", path.c_str());
        return 3;
    }
    uint8_t head[16]{};
    const size_t n = std::fread(head, 1, sizeof(head), f);
    std::fseek(f, 0, SEEK_END);
    const long long size = std::ftell(f);
    std::fclose(f);

    if (n >= 8 && std::memcmp(head, "PDAIMBEN", 8) == 0) {
        uint32_t ver = 0;
        uint32_t type = 0;
        if (n >= 16) {
            std::memcpy(&ver, head + 8, 4);
            std::memcpy(&type, head + 12, 4);
        }
        std::printf("probe: 格式 = mbonx（加密容器）\n");
        std::printf("  size   = %lld 字节 (%.2f MiB)\n", size, static_cast<double>(size) / 1048576.0);
        std::printf("  header = PDAIMBEN  ver=%u  type=%u\n", ver, type);
        std::printf("  body   = %lld 字节（流加密，机器码派生密钥）\n", size - 16);
        std::printf("  参数量估算: fp32 ≈ %.2f M / fp16 ≈ %.2f M\n",
                    static_cast<double>(size - 16) / 4.0 / 1e6,
                    static_cast<double>(size - 16) / 2.0 / 1e6);
        std::printf("  结论: 无法离线解密，需在样本进程内截获明文后再加载\n");
        return 0;
    }

    const auto info = aim::inference::InspectModel(path);
    if (!info.ok) {
        std::printf("probe: 既不是 mbonx 也不是可解析的 ONNX\n  error: %s\n", info.error.c_str());
        return 4;
    }
    std::printf("probe: 格式 = onnx\n");
    std::printf("  size   = %lld 字节 (%.2f MiB)\n", size, static_cast<double>(size) / 1048576.0);
    const auto shape_str = [](const std::vector<int64_t>& s) {
        std::string out = "[";
        for (size_t i = 0; i < s.size(); ++i) {
            if (i) out += ",";
            out += std::to_string(s[i]);
        }
        return out + "]";
    };
    std::printf("  input  = %s  dtype=%s  shape=%s  dynamic=%d\n",
                info.input.name.c_str(), aim::inference::DtypeName(info.input.dtype).c_str(),
                shape_str(info.input.shape).c_str(), info.input.dynamic ? 1 : 0);
    std::printf("  output = %s  dtype=%s  shape=%s\n",
                info.output.name.c_str(), aim::inference::DtypeName(info.output.dtype).c_str(),
                shape_str(info.output.shape).c_str());
    std::printf("  classes = %d  关键点 = %d\n", info.num_classes, info.num_kpts);
    if (!info.classes.empty()) {
        std::printf("  类别表  = ");
        for (size_t i = 0; i < info.classes.size(); ++i) {
            std::printf("%s%s", i ? ", " : "", info.classes[i].c_str());
        }
        std::printf("\n");
    }
    // 输入边长（NCHW 的第 2、3 维）
    if (info.input.shape.size() == 4) {
        std::printf("  >>> 输入尺寸 = %lld x %lld（通道 %lld）\n",
                    static_cast<long long>(info.input.shape[2]),
                    static_cast<long long>(info.input.shape[3]),
                    static_cast<long long>(info.input.shape[1]));
    }
    std::printf("  sha256 = %s\n", info.sha256.c_str());
    return 0;
}

// AIMEngine 入口（§4.2 / §9.1）。
//   --selfcheck             校验默认配置 + 内置扩展注册
//   --validate-config PATH  校验指定配置文件（结构 + schema）
//   --serve                 仅控制面（IPC 命名管道）
//   --run                   生产运行循环（采集→推理→瞄准→执行 + 控制面）
//   --preview               实时预览（不移动）
//   --version / --help
namespace {

// 控制面的统一分派回调：解析失败统一回 400，成功则交给 registry。
// run / serve 两处原本各抄一份，容易漂移（比如一处改了错误码另一处没改）。
// post 可选：在响应生成后执行（serve 用它把 config.apply 落盘）。
using PostHook = std::function<void(const aim::ipc::Request&, const aim::ipc::Response&)>;

aim::ipc::PipeServer::Callback MakeDispatch(aim::ipc::HandlerRegistry& reg,
                                            PostHook post = nullptr) {
    return [&reg, post](const std::string& text) {
        aim::ipc::Request req;
        std::string e;
        if (!aim::ipc::ParseRequest(text, req, &e)) {
            aim::ipc::Response er;
            er.code = 400;
            er.msg = e;
            return aim::ipc::Serialize(er);
        }
        const aim::ipc::Response resp = reg.Dispatch(req);
        if (post) post(req, resp);
        return aim::ipc::Serialize(resp);
    };
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // 控制台默认代码页为 936(GBK)，而本程序输出 UTF-8 中文字符串；
    // 不切换会显示为乱码（重定向到文件时不影响字节内容）。
    SetConsoleOutputCP(CP_UTF8);
    ReportOrtDllSource();
#endif
    aim::RegisterBuiltinExtensions();

    bool selfcheck = false;
    bool serve = false;
    bool preview = false;
    bool run_mode = false;
    bool preview_no_show = false;
    bool run_no_overlay = false;
    bool run_dry = false;
    bool no_pipe = false;
    std::string pipe_name; // 空 = 自动生成随机名（去标识 + 多实例不冲突）
    std::string config_path;
    std::string dump_path;
    std::string probe_path;
    int threads_override = -1; // -1 = 未指定
    std::string preview_model;
    std::string backend_override; // 空 = 未指定（preview 默认 cpu，run 用配置值）
    std::string capture_name = "dxgi";
    int frames_limit = 0;
    int preview_roi = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selfcheck") {
            selfcheck = true;
        } else if (a == "--serve") {
            serve = true;
        } else if (a == "--run") {
            run_mode = true;
        } else if (a == "--preview") {
            preview = true;
        } else if (a == "--model" && i + 1 < argc) {
            preview_model = argv[++i];
        } else if (a == "--backend" && i + 1 < argc) {
            backend_override = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            frames_limit = std::atoi(argv[++i]);
        } else if (a == "--no-show") {
            preview_no_show = true;
        } else if (a == "--no-overlay") {
            run_no_overlay = true;
        } else if (a == "--dry-run") {
            run_dry = true;
        } else if (a == "--no-pipe") {
            no_pipe = true;
        } else if (a == "--capture" && i + 1 < argc) {
            capture_name = argv[++i];
        } else if (a == "--roi" && i + 1 < argc) {
            preview_roi = std::atoi(argv[++i]);
        } else if (a == "--pipe" && i + 1 < argc) {
            pipe_name = argv[++i];
        } else if (a == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (a == "--threads" && i + 1 < argc) {
            threads_override = std::atoi(argv[++i]);
        } else if (a == "--probe-model" && i + 1 < argc) {
            probe_path = argv[++i];
        } else if (a == "--dump-default-config" && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (a == "--version") {
            std::printf("AIMEngine 1.0.0 (schema v%d)\n",
                        aim::ConfigManager::Defaults().schema_version);
            return 0;
        } else if (a == "--help") {
            std::printf("AIMEngine 1.0.0\n"
                        "  --selfcheck              validate default config & builtins\n"
                        "  --probe-model PATH       inspect a model (onnx or mbonx container)\n"
                        "  --threads N              override inference.threads (0 = ORT auto)\n"
                        "  --validate-config PATH   validate a config file\n"
                        "  --dump-default-config P  write default config to P\n"
                        "  --serve [--pipe NAME]    start IPC named-pipe server only\n"
                        "  --run [--config P] [--model P] [--frames N]\n"
                        "        [--dry-run] [--no-overlay] [--no-pipe] [--pipe NAME]\n"
                        "                           production loop: capture->aim->actuate\n"
                        "  --preview [--model P] [--backend cpu|dml] [--capture dxgi|gdi]\n"
                        "            [--frames N] [--no-show] [--roi N (默认=模型输入)]\n"
                        "                           real-time overlay preview (no movement)\n"
                        "  --version                print version\n"
                        "  --help                   this help\n");
            return 0;
        } else if (a == "--validate-config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // 管道名去标识：未显式指定时生成随机名，并写发现文件供控制台读取。
    // 显式 --pipe 完全走老路径（脚本/测试都依赖这一点）。
    bool pipe_is_ephemeral = false;
    if (pipe_name.empty()) {
        pipe_name = aim::identity::MakeEphemeralPipeName();
        pipe_is_ephemeral = true;
        if (aim::identity::WriteDiscovery(pipe_name).empty()) {
            std::printf("warn: 无法写入管道发现文件，控制台需用 --pipe %s 连接\n",
                        pipe_name.c_str());
        } else {
            std::atexit([] { aim::identity::RemoveDiscovery(); });
        }
    }
    (void)pipe_is_ephemeral;

    if (!probe_path.empty()) {
        return ProbeModel(probe_path);
    }

    if (selfcheck) {
        const aim::Config cfg = aim::ConfigManager::Defaults();
        const std::string js = aim::ConfigManager::ToJson(cfg, 0);
        auto r = aim::ConfigSchema::ValidateJson(js);
        if (!r.ok()) {
            std::printf("SELFCHECK FAIL: %s\n", r.error().message.c_str());
            return 1;
        }
        std::printf("SELFCHECK OK: schema v%d, builtins registered\n",
                    aim::ConfigManager::Defaults().schema_version);
        return 0;
    }

    if (!dump_path.empty()) {
        auto s = aim::ConfigManager::SaveToFile(dump_path, aim::ConfigManager::Defaults());
        if (!s.ok()) {
            std::printf("write error [%d]: %s\n", s.error().code, s.error().message.c_str());
            return 3;
        }
        std::printf("wrote default config to %s\n", dump_path.c_str());
        return 0;
    }

    // 注意：这里是"只校验并退出"的分支，必须排除所有运行模式。
    // 旧条件只有 !run_mode，导致 `--serve --config x.json` / `--preview --config x.json`
    // 直接打印 "config ok" 后退出 —— 表现为"serve 忽略了配置文件"，实际是根本没进 serve 分支。
    if (!config_path.empty() && !run_mode && !serve && !preview) {
        auto r = aim::ConfigManager::LoadFromFile(config_path);
        if (!r.ok()) {
            std::printf("config error [%d]: %s\nadvice: %s\n",
                        r.error().code, r.error().message.c_str(), r.error().advice.c_str());
            return 2;
        }
        const aim::Config& c = r.value();
        std::printf("config ok: schema v%d, backend=%s, capture=%s, actuator=%s\n",
                    c.schema_version,
                    aim::to_string(c.inference.backend),
                    aim::to_string(c.capture.mode),
                    aim::to_string(c.actuator.channel));
        return 0;
    }

    if (run_mode) {
        aim::Config cfg = aim::ConfigManager::Defaults();
        if (!config_path.empty()) {
            auto r = aim::ConfigManager::LoadFromFile(config_path);
            if (!r.ok()) {
                std::printf("config error [%d]: %s\nadvice: %s\n",
                            r.error().code, r.error().message.c_str(), r.error().advice.c_str());
                return 2;
            }
            cfg = r.value();
        }
        if (threads_override >= 0) cfg.inference.threads = threads_override;
        const std::string model = ResolveModelPath(preview_model);
        if (model.empty()) {
            std::printf("run: 未指定模型；用 --model <path> 或环境变量 AIM_DEFAULT_MODEL 指定\n");
            return 6;
        }
        // --backend 覆盖配置（MinGW 构建无 DML 时用 cpu 跑）
        if (!backend_override.empty()) {
            aim::Backend b{};
            if (!aim::from_string(backend_override, b)) {
                std::printf("unknown backend: %s (dml|cpu|tensorrt)\n", backend_override.c_str());
                return 6;
            }
            cfg.inference.backend = b;
        }

        aim::ConfigStore store(cfg);
        aim::RunLoop loop;
        loop.AttachConfigStore(&store);

        const auto init = loop.Init(cfg, model, frames_limit, !run_no_overlay, run_dry);
        if (!init.ok()) {
            std::printf("run error [%d]: %s\nadvice: %s\n",
                        init.error().code, init.error().message.c_str(),
                        init.error().advice.c_str());
            return 4;
        }

        std::printf("run: capture=%s backend=%s actuator=%s%s model=%s\n",
                    loop.settings().capture_name.c_str(),
                    loop.settings().backend_name.c_str(),
                    run_dry ? "null(dry-run)" : loop.settings().actuator_name.c_str(),
                    run_no_overlay ? " overlay=off" : "",
                    model.c_str());

        // 控制面：默认同时开命名管道，控制台可实时读状态/改配置（热重载）
        std::unique_ptr<aim::ipc::PipeServer> server;
        aim::ipc::HandlerRegistry reg;
        if (!no_pipe) {
            aim::ipc::RegisterBuiltinHandlers(reg, store, loop.runtime());
            server = std::make_unique<aim::ipc::PipeServer>();
            // M9 L2：服务线程名中性化（须在 Start 之前设置）
            if (loop.settings().neutral_thread_names) server->SetThreadName("w1");
            std::string err;
            if (!server->Start(pipe_name, MakeDispatch(reg), &err)) {
                std::printf("run: IPC 启动失败（继续无控制面运行）: %s\n", err.c_str());
                server.reset();
            } else {
                std::printf("run: IPC on \\\\.\\pipe\\%s\n", pipe_name.c_str());
            }
        }

        // 1Hz 打印运行状态（与 status.snapshot 同口径）
        int64_t last_print = 0;
        loop.on_frame = [&loop, &last_print]() {
            const int64_t now = aim::Clock::NowMs();
            if (now - last_print < 1000) return;
            last_print = now;
            const auto s = loop.runtime().Snapshot(now);
            std::printf("run: frames=%llu fps=%.1f infer=%.2fms target=%d%s\n",
                        static_cast<unsigned long long>(s.frames), s.fps,
                        s.timings.infer_ms, s.has_target ? 1 : 0,
                        loop.restart_required() ? " [restart required]" : "");
        };

#ifdef _WIN32
        g_run_loop = &loop;
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

        const int code = loop.Run();

#ifdef _WIN32
        g_run_loop = nullptr;
#endif
        if (server) server->Stop();

        const auto& st = loop.stats();
        const double n = (st.frames > 0) ? static_cast<double>(st.frames) : 1.0;
        std::printf("run done: %d frames, %d failures, avg frame=%.2f ms "
                    "(pre=%.2f infer=%.2f post=%.2f), total %.0f ms\n",
                    st.frames, st.failures,
                    (st.frames > 0) ? static_cast<double>(st.elapsed_ms) / n : 0.0,
                    st.pre_ms_sum / n, st.infer_ms_sum / n, st.post_ms_sum / n,
                    static_cast<double>(st.elapsed_ms));
        if (loop.restart_required()) {
            std::printf("run: %s\n", loop.restart_reason().c_str());
        }
        return code;
    }

    if (preview) {
        // 预览也要认 --config（至少认采集目标屏）：否则 `--preview --config x.json`
        // 会"看起来加载了配置、实际全用默认值"。
        aim::Config pv_cfg = aim::ConfigManager::Defaults();
        if (!config_path.empty()) {
            auto loaded = aim::ConfigManager::LoadFromFile(config_path);
            if (!loaded.ok()) {
                std::printf("config error [%d]: %s\nadvice: %s\n", loaded.error().code,
                            loaded.error().message.c_str(), loaded.error().advice.c_str());
                return 2;
            }
            pv_cfg = loaded.value();
            std::printf("preview: 已加载配置 %s（仅采用 capture.output_monitor）\n",
                        config_path.c_str());
        }

        std::string model = ResolveModelPath(preview_model);
        if (model.empty()) {
            std::printf("preview: 未指定模型；用 --model <path> 或环境变量 AIM_DEFAULT_MODEL 指定\n");
            return 6;
        }

        aim::inference::OrtEp ep = aim::inference::OrtEp::Cpu;
        const std::string preview_backend =
            backend_override.empty() ? std::string("cpu") : backend_override;
        if (preview_backend == "dml") {
            ep = aim::inference::OrtEp::Dml;
        } else if (preview_backend != "cpu") {
            std::printf("unknown backend: %s (cpu|dml)\n", preview_backend.c_str());
            return 6;
        }
#ifdef AIM_ENABLE_DML
        if (ep == aim::inference::OrtEp::Cpu) {
            std::printf("preview: backend=cpu（本构建支持 DML，可用 --backend dml 提速）\n");
        }
#endif

        const auto info = aim::inference::InspectModel(model);
        if (!info.ok) {
            std::printf("preview model error: %s\n", info.error.c_str());
            return 6;
        }
        const int imgsz = (info.input.shape.size() == 4 && info.input.shape[2] > 0)
                              ? static_cast<int>(info.input.shape[2])
                              : 960;
        // --roi 独立于 imgsz：默认与模型输入一致，显式指定则以 ROI 为准
        const int roi = (preview_roi > 0) ? preview_roi : imgsz;

        auto cap = aim::Registry::create<aim::spi::ICapture>(capture_name);
        if (!cap) {
            std::printf("unknown capture: %s\n", capture_name.c_str());
            return 6;
        }
        aim::spi::CaptureSpec cs;
        cs.output_monitor = pv_cfg.capture.output_monitor;
        cs.roi_w = roi;
        cs.roi_h = roi;
        if (!cap->init(cs)) {
            std::printf("capture init failed: %s\n", capture_name.c_str());
            return 6;
        }

        aim::inference::OrtBackend be(ep);
        aim::spi::ModelSpec ms;
        ms.path = model;
        ms.imgsz = imgsz;
        ms.num_classes = info.num_classes;
        ms.num_kpts = info.num_kpts;
        ms.conf_threshold = 0.35f;
        ms.input_nhwc_u8 = false;
        ms.threads = (threads_override >= 0) ? threads_override : 4; // 预览默认 4 线程
        if (!be.load(ms)) {
            std::printf("preview model load failed (backend=%s): %s\n",
                        preview_backend.c_str(), model.c_str());
            return 6;
        }

        int sw = 1920;
        int sh = 1080;
        // 预览固定采 output_monitor=0（见上面的 cs.output_monitor），分辨率按同一块屏取；
        // 取不到再退回主屏，最后才用 1920x1080 并明确告警——不再静默用默认值。
        if (!aim::GetMonitorResolution(cs.output_monitor, sw, sh) &&
            !aim::GetPrimaryResolution(sw, sh)) {
            std::printf("warning: 分辨率识别失败，退化 %dx%d（FOV/准星中心可能不准）\n", sw, sh);
        }

        struct NullActuator final : aim::spi::IActuator {
            void move(const aim::spi::MoveCmd&) override {}
            void click(const aim::spi::ClickCmd&) override {}
        } act;

        aim::PipelineSettings s;
        s.screen_w = static_cast<float>(sw);
        s.screen_h = static_cast<float>(sh);
        s.imgsz = imgsz;
        s.conf_threshold = 0.35f;
        s.iou_threshold = 0.45f;
        s.class_whitelist.clear();
        for (int i = 0; i < info.num_classes; ++i) s.class_whitelist.push_back(i);
        s.class_names = info.classes;
        s.aim = aim::AimConfig{};
        s.scope.mode = aim::ScopeMode::Rect;
        s.scope.half_w = static_cast<float>(sw);
        s.scope.half_h = static_cast<float>(sh);
        s.fov = aim::Fov{};
        s.selection = aim::TargetSelection::NearestCrosshair;
        s.armed = false; // 预览不移动
        s.capture_spec = cs;

        aim::Pipeline pipe;

        // 覆盖层走 SPI（§4.4 注册名 "d3d11"）：--no-show 时不创建窗口，
        // 其余场景由 Pipeline 在映射完检测框后统一绘制。
        std::unique_ptr<aim::spi::IOverlay> overlay;
        if (!preview_no_show) {
            overlay = aim::Registry::create<aim::spi::IOverlay>("d3d11");
            if (!overlay) {
                std::printf("overlay factory 'd3d11' unavailable\n");
                return 6;
            }
        }

        pipe.Configure(aim::PipelineDeps{cap.get(), &be, &act, overlay.get()}, s);

        aim::EngineRuntime rt;
        rt.Start(aim::Clock::NowMs());
        rt.SetSegments(aim::SegmentState::Running, aim::SegmentState::Running,
                       aim::SegmentState::Degraded);

        std::printf("preview: capture=%s model=%s imgsz=%d roi=%d screen=%dx%d frames=%d\n",
                    capture_name.c_str(), model.c_str(), imgsz, roi, sw, sh, frames_limit);

        int done = 0;
        int failures = 0; // 连续失败计数（成功即清零）
        size_t last_dets = 0;
        double infer_sum = 0.0; // 纯推理耗时累计（不含采集/前处理），用于性能验收
        // 有限帧模式下设置失败上限，避免采集/推理持续失败时忙等死循环（100% CPU 挂死）。
        const int max_failures = (frames_limit > 0) ? (frames_limit * 10 + 100) : 0;
        while (frames_limit == 0 || done < frames_limit) {
            const int64_t now = aim::Clock::NowMs();
            const auto r = pipe.ProcessFrame(now);
            if (r.ok) {
                ++done;
                failures = 0;
                last_dets = r.detections.size();
                infer_sum += be.counters().infer_ms;
                aim::FrameTimings t;
                t.cap_ms = 0.0;
                t.infer_ms = be.counters().infer_ms;
                t.post_ms = be.counters().post_ms;
                t.send_ms = 0.0;
                rt.OnFrame(now, r, t);
                rt.SetDetections(r.detections);
            } else {
                ++failures;
                if (max_failures > 0 && failures >= max_failures) {
                    std::printf("preview aborted: %d consecutive frame failures "
                                "(capture or inference unavailable)\n", failures);
                    return 7;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (frames_limit == 0) {
                if (_kbhit()) {
                    _getch();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        std::printf("preview done: %d frames, avg infer=%.2f ms, last detections=%zu\n",
                    done, (done > 0) ? (infer_sum / done) : 0.0, last_dets);
        return done > 0 ? 0 : 7;
    }

    if (serve) {
        // serve 只提供控制面，不跑数据面。因此：
        //   ① 不 Start() 运行时 —— status.snapshot 如实返回 running=false，
        //      而不是伪造一个 frames=0 的"运行中"；
        //   ② 必须读取 --config —— 旧实现无条件用默认值，`--serve --config x.json` 被静默忽略；
        //   ③ config.apply 之后要落盘 —— 否则控制台显示"应用成功"（版本号+1）但重启即丢。
        aim::EngineRuntime rt;
        rt.SetSegments(aim::SegmentState::Failed, aim::SegmentState::Failed,
                       aim::SegmentState::Failed);

        aim::Config initial = aim::ConfigManager::Defaults();
        if (!config_path.empty()) {
            auto loaded = aim::ConfigManager::LoadFromFile(config_path);
            if (!loaded.ok()) {
                std::printf("config error [%d]: %s\nadvice: %s\n", loaded.error().code,
                            loaded.error().message.c_str(), loaded.error().advice.c_str());
                return 2;
            }
            initial = loaded.value();
            std::printf("serve: 已加载配置 %s\n", config_path.c_str());
        }
        aim::ConfigStore config_store(initial);

        aim::ipc::HandlerRegistry reg;
        aim::ipc::RegisterBuiltinHandlers(reg, config_store, rt);

        aim::ipc::PipeServer server;
        std::string err;
        const std::string persist_path = config_path;
        // 配置变更后落盘（只在这个模式做：--run 模式由控制台/用户决定何时写文件）
        auto persist_hook = [&config_store, persist_path](const aim::ipc::Request& req,
                                                         const aim::ipc::Response& resp) {
            if (resp.code != 0 || persist_path.empty()) return;
            if (req.cmd != aim::ipc::cmd::kConfigApply) return;
            const auto saved = config_store.SaveToFile(persist_path);
            if (!saved.ok()) {
                std::printf("serve: 配置已生效但落盘失败 [%d] %s\n", saved.error().code,
                            saved.error().message.c_str());
            }
        };
        if (!server.Start(pipe_name, MakeDispatch(reg, persist_hook), &err)) {
            std::printf("serve error: %s\n", err.c_str());
            return 4;
        }

        std::printf("AIMEngine serving on \\\\.\\pipe\\%s (schema v%d, config-only). "
                    "Press Enter to stop.\n",
                    pipe_name.c_str(), aim::ConfigManager::Defaults().schema_version);
        std::getchar();
        server.Stop();
        return 0;
    }

    std::printf("AIMEngine ready (schema v3).\n"
                "Use --run (production loop) | --preview (no movement) | --serve (IPC only)\n"
                "    | --selfcheck | --validate-config <path> | --version.\n");
    return 0;
}
