#include "inference/backend_stub.h"
#include "inference/backend_ort.h"
#include "core/logger.h"
#include "core/registry.h"
#include "core/spi.h"
#include <memory>

namespace aim::inference {

namespace {

// §4.4 注册名 "tensorrt"：文档 §5.3.1 标注为 P1 预留（需 TensorRT EP + engine cache）。
// 保留注册名，让"按配置字符串创建后端"的调用方拿到明确失败原因，而不是 nullptr。
class TensorrtBackend final : public spi::IBackend {
public:
    bool load(const spi::ModelSpec& spec) override {
        AIM_ENGINE(Warn, "backend 'tensorrt' 尚未实现（P1 预留）: model=" + spec.path);
        return false;
    }
    bool infer(const spi::Frame&, std::vector<Detection>&) override { return false; }
    std::string backend_name() const override { return "tensorrt(unavailable)"; }
    spi::PerfCounters counters() const override { return {}; }
};

} // namespace

void RegisterBackendFactories() {
    Registry::register_factory<spi::IBackend>("cpu", [] {
        return std::make_unique<OrtBackend>(OrtEp::Cpu);
    });
    Registry::register_factory<spi::IBackend>("dml", [] {
        return std::make_unique<OrtBackend>(OrtEp::Dml);
    });
    Registry::register_factory<spi::IBackend>("tensorrt", [] {
        return std::make_unique<TensorrtBackend>();
    });
}

} // namespace aim::inference
