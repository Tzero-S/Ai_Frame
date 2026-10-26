#include "inference/backend_ort.h"
#include "inference/preprocess.h"
#include "inference/onnx_meta.h"

// AIM_WITH_ORT=OFF 时本文件只提供占位实现（见文件末尾 #else 分支），
// 保证 SPI 注册名与接口不变、且无 ORT 依赖也能完整构建。
#if defined(AIM_WITH_ORT)

// ORT 头在 Windows 上用 MSVC 的 `_stdcall` 关键字；MinGW 用 `__stdcall`。
#if defined(_WIN32) && !defined(_stdcall)
#  define _stdcall __stdcall
#endif

#include <onnxruntime_cxx_api.h>
// DML EP 需要 Windows SDK 的 DirectML.h；用 AIM_ENABLE_DML 门控（MSVC+SDK 构建开启）。
#ifdef AIM_ENABLE_DML
#  include <dml_provider_factory.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/clock.h"
#include "core/logger.h"
#include "opsec/process_guard.h"
#include "postprocess/yolo_decoder.h"

namespace aim::inference {

namespace {

// AIM_ORT_LOG_LEVEL：0=verbose 1=info 2=warning 3=error 4=fatal（默认 2）。
// 排查 EP/设备问题时可用 0 打开 ORT 自身日志。
OrtLoggingLevel LogLevelFromEnv() {
    if (const char* v = std::getenv("AIM_ORT_LOG_LEVEL")) {
        const int lv = std::atoi(v);
        if (lv >= 0 && lv <= 4) return static_cast<OrtLoggingLevel>(lv);
    }
    return ORT_LOGGING_LEVEL_WARNING;
}

#ifdef AIM_ENABLE_DML
// AIM_DML_DEVICE：DML 使用的 DXGI 适配器序号（默认 0）。
// 多显卡/虚拟显示器机器上 0 号适配器可能不支持 D3D12 计算，需要显式指定。
int DmlDeviceIdFromEnv() {
    if (const char* v = std::getenv("AIM_DML_DEVICE")) {
        const int id = std::atoi(v);
        if (id >= 0) return id;
    }
    return 0;
}
#endif

Ort::Env& GetEnv() {
    static Ort::Env env(LogLevelFromEnv(), "aim_engine");
    return env;
}

// BGRA → 目标尺寸 BGRA（最近邻），用于图改写后的 u8 NHWC 输入。
void ResizeBgraNearest(const uint8_t* src, int sw, int sh,
                       uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        const int sy = (sh > 0) ? (y * sh) / dh : 0;
        const uint8_t* srow = src + static_cast<size_t>(sy) * sw * 4;
        uint8_t* drow = dst + static_cast<size_t>(y) * dw * 4;
        for (int x = 0; x < dw; ++x) {
            const int sx = (sw > 0) ? (x * sw) / dw : 0;
            const uint8_t* sp = srow + static_cast<size_t>(sx) * 4;
            uint8_t* dp = drow + static_cast<size_t>(x) * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
}


} // namespace

struct OrtBackend::Impl {
    std::unique_ptr<Ort::Session> session;
    std::string input_name;
    std::string output_name;
    std::vector<std::string> class_names;
    bool loaded = false;
    // 前处理缓冲复用：960×960 的 float NCHW 约 11MB，每帧重新分配会显著拖慢 pre 阶段
    std::vector<uint8_t> u8_buf;
    std::vector<float> f32_buf;
};

OrtBackend::OrtBackend(OrtEp ep) : impl_(std::make_unique<Impl>()), ep_(ep) {}
OrtBackend::~OrtBackend() = default;

std::string OrtBackend::backend_name() const {
    return (ep_ == OrtEp::Dml) ? "dml" : "cpu";
}

const std::vector<std::string>& OrtBackend::class_names() const {
    return impl_->class_names;
}

bool OrtBackend::load(const spi::ModelSpec& spec) {
    spec_ = spec;
    impl_->loaded = false;

    try {
        std::error_code ec;
        if (spec.path.empty() || !std::filesystem::exists(spec.path, ec)) {
            AIM_ENGINE(Error, "ORT load: model not found: " + spec.path);
            return false;
        }

        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (spec.threads > 0) so.SetIntraOpNumThreads(spec.threads);

        if (ep_ == OrtEp::Dml) {
#ifdef AIM_ENABLE_DML
            const int device_id = DmlDeviceIdFromEnv();
            OrtStatus* raw = OrtSessionOptionsAppendExecutionProvider_DML(so, device_id);
            if (raw) {
                Ort::Status st(raw); // 接管所有权，析构时释放
                AIM_ENGINE(Error, "DML EP 注册失败 [code=" +
                                       std::to_string(static_cast<int>(st.GetErrorCode())) +
                                       ", device=" + std::to_string(device_id) + "]: " +
                                       st.GetErrorMessage());
                return false;
            }
#else
            // 未编译 DML EP（缺 DirectML.h）：按 §P5 明确降级而非静默失败
            AIM_ENGINE(Warn, "ORT DML EP not compiled in; use cpu backend");
            return false;
#endif
        }

#ifdef _WIN32
        const std::wstring wpath = std::filesystem::path(spec.path).wstring();
        try {
            impl_->session = std::make_unique<Ort::Session>(GetEnv(), wpath.c_str(), so);
        } catch (const Ort::Exception& e) {
            // 部分 EP 失败时 what() 为空，必须同时打印 ORT 错误码才可定位
            AIM_ENGINE(Error, "ORT session init failed [code=" +
                                   std::to_string(static_cast<int>(e.GetOrtErrorCode())) +
                                   ", ep=" + std::string(ep_ == OrtEp::Dml ? "dml" : "cpu") +
                                   "]: " + e.what());
            return false;
        }
#else
        impl_->session = std::make_unique<Ort::Session>(GetEnv(), spec.path.c_str(), so);
#endif

        Ort::AllocatorWithDefaultOptions alloc;
        auto in_name = impl_->session->GetInputNameAllocated(0, alloc);
        auto out_name = impl_->session->GetOutputNameAllocated(0, alloc);
        impl_->input_name = in_name.get();
        impl_->output_name = out_name.get();

        // §5.10.1 ①：类别名来自 ONNX metadata.properties['names']
        impl_->class_names.clear();
        try {
            auto meta = impl_->session->GetModelMetadata();
            Ort::AllocatorWithDefaultOptions meta_alloc;
            auto keys = meta.GetCustomMetadataMapKeysAllocated(meta_alloc);
            for (auto& k : keys) {
                if (std::string(k.get()) == "names") {
                    auto v = meta.LookupCustomMetadataMapAllocated("names", meta_alloc);
                    if (v) impl_->class_names = ParseClassNames(v.get());
                    break;
                }
            }
        } catch (const Ort::Exception&) {
            // 元数据可选；缺失不影响推理
        }

        impl_->loaded = true;
        return true;
    } catch (const Ort::Exception& e) {
        AIM_ENGINE(Error, std::string("ORT load failed: ") + e.what());
        return false;
    } catch (const std::exception& e) {
        AIM_ENGINE(Error, std::string("ORT load failed: ") + e.what());
        return false;
    }
}

bool OrtBackend::infer(const spi::Frame& frame, std::vector<Detection>& out) {
    out.clear();
    if (!impl_->loaded || !frame.bgra || frame.w <= 0 || frame.h <= 0) return false;

    try {
        const int S = (spec_.imgsz > 0) ? spec_.imgsz : 640;
        const Ort::MemoryInfo mem =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const char* in_names[] = { impl_->input_name.c_str() };
        const char* out_names[] = { impl_->output_name.c_str() };

        std::vector<uint8_t>& u8 = impl_->u8_buf;
        std::vector<float>& f32 = impl_->f32_buf;
        Ort::Value input{nullptr};

        const int64_t t0 = Clock::NowUs();

        if (spec_.input_nhwc_u8) {
            u8.resize(static_cast<size_t>(S) * S * 4);
            ResizeBgraNearest(frame.bgra, frame.w, frame.h, u8.data(), S, S);
            const int64_t shape[4] = {1, S, S, 4};
            input = Ort::Value::CreateTensor<uint8_t>(mem, u8.data(), u8.size(), shape, 4);
        } else {
            f32.resize(static_cast<size_t>(S) * S * 3);
            FillNchwRgb(frame.bgra, frame.w, frame.h, f32.data(), S, S);
            const int64_t shape[4] = {1, 3, S, S};
            input = Ort::Value::CreateTensor<float>(mem, f32.data(), f32.size(), shape, 4);
        }

        const int64_t t1 = Clock::NowUs();

        auto outputs = impl_->session->Run(
            Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 1);

        const int64_t t2 = Clock::NowUs();

        if (outputs.empty()) return false;
        Ort::Value& ov = outputs[0];
        auto info = ov.GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> odims = info.GetShape();
        if (odims.size() != 3) {
            AIM_ENGINE(Error, "ORT infer: output rank != 3");
            return false;
        }

        float* odata = ov.GetTensorMutableData<float>();
        TensorView tv;
        tv.data = odata;
        tv.ndim = 3;
        for (int i = 0; i < 3; ++i) tv.dims[i] = static_cast<int>(odims[i]);

        YoloOutputSpec yspec;
        yspec.num_classes = spec_.num_classes;
        yspec.num_kpts = spec_.num_kpts;
        yspec.imgsz = S;
        yspec.conf_threshold = spec_.conf_threshold;

        out = DecodeYolo(tv, yspec);

        // M9 L2（§5.12.2）：输出张量用后清零，避免检测结果残留在内存中
        if (spec_.zero_outputs) {
            const size_t n = static_cast<size_t>(odims[0]) * static_cast<size_t>(odims[1]) *
                             static_cast<size_t>(odims[2]);
            opsec::ProcessGuard::SecureZero(odata, n * sizeof(float));
        }

        const int64_t t3 = Clock::NowUs();
        counters_.pre_ms = static_cast<float>((t1 - t0) / 1000.0);
        counters_.infer_ms = static_cast<float>((t2 - t1) / 1000.0);
        counters_.post_ms = static_cast<float>((t3 - t2) / 1000.0);
        return true;
    } catch (const Ort::Exception& e) {
        AIM_ENGINE(Error, std::string("ORT infer failed: ") + e.what());
        return false;
    } catch (const std::exception& e) {
        AIM_ENGINE(Error, std::string("ORT infer failed: ") + e.what());
        return false;
    }
}

} // namespace aim::inference

#else // !AIM_WITH_ORT

#include "core/logger.h"

namespace aim::inference {

// 占位实现：保持 §4.4 注册名（cpu / dml）与接口不变。
// load() 恒失败并给出明确原因，避免"静默无检测"被误判为模型问题。
struct OrtBackend::Impl {};

OrtBackend::OrtBackend(OrtEp ep) : impl_(std::make_unique<Impl>()), ep_(ep) {}
OrtBackend::~OrtBackend() = default;

std::string OrtBackend::backend_name() const {
    return (ep_ == OrtEp::Dml) ? "dml(unavailable)" : "cpu(unavailable)";
}

const std::vector<std::string>& OrtBackend::class_names() const {
    static const std::vector<std::string> kEmpty;
    return kEmpty;
}

bool OrtBackend::load(const spi::ModelSpec& spec) {
    AIM_ENGINE(Warn, "ONNX Runtime 未编入本构建（AIM_WITH_ORT=OFF）：model=" + spec.path);
    return false;
}

bool OrtBackend::infer(const spi::Frame&, std::vector<Detection>&) { return false; }

} // namespace aim::inference

#endif // AIM_WITH_ORT
