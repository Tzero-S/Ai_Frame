#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/spi.h"

// ONNX Runtime 推理后端（§5.3.1）。CPU / DML 两种 EP。
// 说明：公共头不引入 ORT 头（§9.2 include-what-you-use），实现细节在 .cpp。
namespace aim::inference {

enum class OrtEp { Cpu, Dml };

class OrtBackend final : public spi::IBackend {
public:
    explicit OrtBackend(OrtEp ep = OrtEp::Cpu);
    ~OrtBackend() override;

    bool load(const spi::ModelSpec& spec) override;
    bool infer(const spi::Frame& frame, std::vector<Detection>& out) override;
    std::string backend_name() const override;
    spi::PerfCounters counters() const override { return counters_; }

    // 从 ONNX 元数据 names 解析出的类别名（§5.10.1 ①）。
    const std::vector<std::string>& class_names() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    OrtEp ep_;
    spi::ModelSpec spec_{};
    spi::PerfCounters counters_{};
};

} // namespace aim::inference
