// 双缓冲流水线基准 Demo
//
// 目的：量化「采集/推理并行」在不同负载比例下的收益，并验证并发下帧内容不被撕裂。
//
// 为什么按"固定时长"而不是"固定帧数"：
//   双缓冲带背压（消费者跟不上就丢旧帧），固定帧数会让"丢了多少"干扰结论；
//   固定时长下比较的是**实际完成的推理次数**，这才是流水线真正买到的东西。
//
// 用法：
//   aim_pipeline_demo                          # 内置三组场景，各跑 3 次取中位数
//   aim_pipeline_demo --duration 2000 --runs 5
//   aim_pipeline_demo --scenarios "1,15;5,10" --json
#include "core/frame_pipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// 精确等待：Windows 的 Sleep 受系统定时器精度限制（默认 ≈15.6ms），
// 1ms 的 sleep 会被放大十几倍，直接污染测量。
// 折中做法：先睡到还剩 2ms，再用自旋收尾 —— 既准又不长时间占满 CPU。
void WaitMs(int ms) {
    if (ms <= 0) return;
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    // 纯自旋：Windows 的 sleep 受系统定时器粒度影响（默认 ≈15.6ms，
    // 即便 timeBeginPeriod(1) 也不总可靠），1~10ms 的等待会被放大数倍，
    // 直接污染测量。基准程序用少量 CPU 换准确度是值得的。
    int spin = 0;
    while (Clock::now() < deadline) {
        if (++spin > 64) {
            std::this_thread::yield();
            spin = 0;
        }
    }
}

struct Load {
    int w = 320;
    int h = 320;
    int cap_ms = 1;
    int infer_ms = 15;
};

struct Outcome {
    int frames = 0;    // 完成的推理次数
    double ms = 0.0;   // 实际耗时
    uint64_t torn = 0; // 内容校验失败次数（并发撕裂）
    double fps() const { return (ms > 0.0) ? frames * 1000.0 / ms : 0.0; }
    double per_frame() const { return (frames > 0) ? ms / frames : 0.0; }
};

// 模拟一次采集：填一块 BGRA + 固定耗时
std::vector<uint8_t> CaptureOnce(const Load& ld, int seq) {
    std::vector<uint8_t> buf(static_cast<size_t>(ld.w) * ld.h * 4);
    std::memset(buf.data(), seq & 0xFF, buf.size());
    WaitMs(ld.cap_ms);
    return buf;
}

// 串行：采集完再推理，两个阶段互相等
Outcome RunSerial(const Load& ld, int duration_ms) {
    const auto t0 = Clock::now();
    const auto deadline = t0 + std::chrono::milliseconds(duration_ms);
    Outcome o;
    int seq = 0;
    while (Clock::now() < deadline) {
        const auto px = CaptureOnce(ld, seq++);
        (void)px[0];
        WaitMs(ld.infer_ms);
        ++o.frames;
    }
    o.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return o;
}

// 并行：采集线程持续产帧，主线程取最新帧推理
Outcome RunPipelined(const Load& ld, int duration_ms, aim::FramePipelineStats* stats_out) {
    aim::FrameDoubleBuffer buf(/*acquire_timeout_ms=*/5);
    std::atomic<bool> stop{false};
    const auto t0 = Clock::now();
    const auto deadline = t0 + std::chrono::milliseconds(duration_ms);

    std::thread producer([&] {
        int seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            const auto px = CaptureOnce(ld, seq);
            buf.Submit(px.data(), ld.w, ld.h, 1920, 1080, 0, 0, ld.w, ld.h, seq,
                       static_cast<uint64_t>(seq) * 1000);
            ++seq;
        }
        buf.Stop();
    });

    Outcome o;
    aim::OwnedFrame f;
    const size_t expect = static_cast<size_t>(ld.w) * ld.h * 4;
    while (Clock::now() < deadline) {
        if (!buf.Acquire(f)) continue;
        // 内容校验：整帧必须是同一填充值，撕裂会混入上一帧的字节
        const uint8_t want = static_cast<uint8_t>(f.seq & 0xFF);
        if (f.bytes() != expect || f.bgra.front() != want || f.bgra.back() != want) {
            ++o.torn;
        }
        WaitMs(ld.infer_ms);
        ++o.frames;
    }
    o.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    stop.store(true);
    producer.join();
    if (stats_out) *stats_out = buf.Stats();
    return o;
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Scenario {
    int cap_ms = 0;
    int infer_ms = 0;
};

struct Row {
    Scenario sc;
    double serial_per = 0.0;
    double pipe_per = 0.0;
    double speedup = 0.0;
    double ideal = 0.0;
    uint64_t torn = 0;
    aim::FramePipelineStats stats{};
};

Row Measure(const Load& base, const Scenario& sc, int duration_ms, int runs) {
    Load ld = base;
    ld.cap_ms = sc.cap_ms;
    ld.infer_ms = sc.infer_ms;

    Row row;
    row.sc = sc;
    std::vector<double> serial;
    std::vector<double> pipe;
    for (int i = 0; i < runs; ++i) {
        serial.push_back(RunSerial(ld, duration_ms).per_frame());
        aim::FramePipelineStats st{};
        const Outcome p = RunPipelined(ld, duration_ms, &st);
        pipe.push_back(p.per_frame());
        row.torn += p.torn;
        row.stats = st;
    }
    row.serial_per = Median(serial);
    row.pipe_per = Median(pipe);
    row.speedup = (row.pipe_per > 0.0) ? row.serial_per / row.pipe_per : 0.0;

    const int sum = sc.cap_ms + sc.infer_ms;
    const int bound = (sc.cap_ms > sc.infer_ms) ? sc.cap_ms : sc.infer_ms;
    row.ideal = (bound > 0) ? static_cast<double>(sum) / bound : 1.0;
    return row;
}

std::vector<Scenario> ParseScenarios(const std::string& spec, bool* ok) {
    std::vector<Scenario> out;
    *ok = true;
    size_t pos = 0;
    while (pos < spec.size()) {
        const size_t semi = spec.find(';', pos);
        const std::string part =
            spec.substr(pos, (semi == std::string::npos) ? std::string::npos : semi - pos);
        const size_t comma = part.find(',');
        if (comma == std::string::npos) {
            *ok = false;
            return {};
        }
        Scenario sc;
        sc.cap_ms = std::atoi(part.substr(0, comma).c_str());
        sc.infer_ms = std::atoi(part.substr(comma + 1).c_str());
        if (sc.cap_ms < 0 || sc.infer_ms < 0) {
            *ok = false;
            return {};
        }
        out.push_back(sc);
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    if (out.empty()) *ok = false;
    return out;
}

} // namespace

int main(int argc, char** argv) {
    int roi = 320;
    int duration_ms = 1200;
    int runs = 3;
    bool json = false;
    std::string spec = "1,15;5,10;0,15";

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--roi" && i + 1 < argc) {
            roi = std::atoi(argv[++i]);
        } else if (a == "--duration" && i + 1 < argc) {
            duration_ms = std::atoi(argv[++i]);
        } else if (a == "--runs" && i + 1 < argc) {
            runs = std::atoi(argv[++i]);
        } else if (a == "--scenarios" && i + 1 < argc) {
            spec = argv[++i];
        } else if (a == "--json") {
            json = true;
        } else if (a == "--help" || a == "-h") {
            std::printf("用法: aim_pipeline_demo [--roi N] [--duration MS] [--runs N]\n"
                        "                       [--scenarios \"采集,推理;...\"] [--json]\n");
            return 0;
        }
    }

    if (roi < 16) roi = 16;
    if (roi > 4096) roi = 4096;
    if (duration_ms < 100) duration_ms = 100;
    if (runs < 1) runs = 1;
    if (runs > 20) runs = 20;

    bool ok = false;
    const auto scenarios = ParseScenarios(spec, &ok);
    if (!ok) {
        std::fprintf(stderr, "错误: --scenarios 格式应为 \"采集,推理;采集,推理\"\n");
        return 2;
    }

    Load base;
    base.w = roi;
    base.h = roi;

    // 预热：首次分配与线程创建不计入
    RunSerial(Load{roi, roi, 0, 0}, 30);
    RunPipelined(Load{roi, roi, 0, 0}, 30, nullptr);

    std::vector<Row> rows;
    rows.reserve(scenarios.size());
    for (const auto& sc : scenarios) rows.push_back(Measure(base, sc, duration_ms, runs));

    if (json) {
        std::printf("{\n  \"roi\": %d,\n  \"duration_ms\": %d,\n  \"runs\": %d,\n  \"rows\": [\n",
                    roi, duration_ms, runs);
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            std::printf("    {\"cap_ms\": %d, \"infer_ms\": %d, \"serial_ms\": %.3f, "
                        "\"pipelined_ms\": %.3f, \"speedup\": %.3f, \"ideal\": %.3f, "
                        "\"torn\": %llu, \"dropped\": %llu, \"submitted\": %llu}%s\n",
                        r.sc.cap_ms, r.sc.infer_ms, r.serial_per, r.pipe_per, r.speedup,
                        r.ideal, static_cast<unsigned long long>(r.torn),
                        static_cast<unsigned long long>(r.stats.dropped),
                        static_cast<unsigned long long>(r.stats.submitted),
                        (i + 1 < rows.size()) ? "," : "");
        }
        std::printf("  ]\n}\n");
        return 0;
    }

    std::printf("=== 双缓冲流水线基准 ===\n");
    std::printf("ROI %dx%d，每组 %d ms，重复 %d 次取中位数\n\n", roi, roi, duration_ms, runs);

    std::printf("| 采集ms | 推理ms | 串行 ms/帧 | 双缓冲 ms/帧 | 提升 | 理论上限 |\n");
    std::printf("|-------:|-------:|-----------:|-------------:|-----:|---------:|\n");
    for (const Row& r : rows) {
        std::printf("| %6d | %6d | %10.2f | %12.2f | %4.2fx | %7.2fx |\n", r.sc.cap_ms,
                    r.sc.infer_ms, r.serial_per, r.pipe_per, r.speedup, r.ideal);
    }

    uint64_t torn_total = 0;
    uint64_t dropped_total = 0;
    for (const Row& r : rows) {
        torn_total += r.torn;
        dropped_total += r.stats.dropped;
    }
    std::printf("\n完整性校验：撕裂帧 %llu 个（0 = 全部通过）\n",
                static_cast<unsigned long long>(torn_total));
    std::printf("背压丢弃：%llu 帧（消费者跟不上时只保留最新帧，属预期行为）\n",
                static_cast<unsigned long long>(dropped_total));

    std::printf("\n结论：\n");
    std::printf("  · 收益上限 = (采集+推理)/max(采集,推理)，由采集占比决定\n");
    std::printf("  · 实测与理论上限一致 → 并行已无浪费；偏差大 → 检查线程/调度\n");

    return torn_total == 0 ? 0 : 1;
}
