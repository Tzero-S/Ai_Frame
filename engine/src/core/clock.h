#pragma once
#include <chrono>
#include <cstdint>

// 单调时钟（§6 时序预算 / 端到端延迟测量）。使用 steady_clock，避免系统时间跳变。
namespace aim {

class Clock {
public:
    static int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    static int64_t NowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

} // namespace aim
