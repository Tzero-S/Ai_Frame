#include "logger.h"
#include "opsec/redact.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>
#include <ctime>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace aim {

const char* to_string(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

const char* to_string(LogChannel ch) {
    switch (ch) {
        case LogChannel::Engine: return "engine";
        case LogChannel::Perf:   return "perf";
        case LogChannel::Audit:  return "audit";
        case LogChannel::Trace:  return "trace";
        case LogChannel::Crash:  return "crash";
    }
    return "?";
}

struct Logger::Impl {
    std::array<std::ofstream, 5> files; // 按 LogChannel 序
    static constexpr const char* kFileNames[5] = {
        "engine.log", "perf.log", "audit.log", "trace.log", "crash.log"};
};

Logger::Logger() : impl_(std::make_unique<Impl>()) {
    // 默认目录：%LOCALAPPDATA%\AIMFramework\logs（Windows 自带该环境变量）
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    if (local && *local) dir_ = std::string(local) + "\\AIMFramework\\logs";
#else
    const char* home = std::getenv("HOME");
    dir_ = std::string(home ? home : ".") + "/.aimframework/logs";
#endif
    // 不在构造时强制建目录/开文件；首次写入时惰性打开。
}

Logger::~Logger() = default;

Logger& Logger::instance() {
    static Logger g;
    return g;
}

void Logger::set_directory(std::string_view dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    dir_ = std::string(dir);
    // 切换目录时关闭已有文件，下次写入惰性重开
    for (auto& f : impl_->files) if (f.is_open()) f.close();
}

std::string Logger::file_path(LogChannel ch) const {
    int idx = static_cast<int>(ch);
    if (dir_.empty()) return {};
    return dir_ + "/" + Impl::kFileNames[idx];
}

void Logger::write(LogChannel ch, LogLevel lvl, std::string_view msg) {
    if (lvl < min_level_) return; // 级别过滤（§5.14 trace 默认关闭）

    // 路径脱敏（anti_detect.strip_paths）：所有日志都从这里过，单点生效。
    // 默认开启——日志落盘后不应包含安装路径/用户名。
    const std::string scrubbed = opsec::ScrubPaths(msg);
    msg = scrubbed;

    // 时间戳
    char ts[32] = {0};
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    // 4096 而不是 1024：配置 JSON / 错误上下文经常超过 1KB，静默截断会让排障信息丢失。
    // 仍可能截断（超长消息），此时显式打上 "…(truncated)" 标记而不是无声丢弃。
    char line[4096];
    const int n = std::snprintf(line, sizeof(line), "[%s] %s %s | %.*s",
                                ts, to_string(ch), to_string(lvl),
                                static_cast<int>(msg.size()), msg.data());
    if (n < 0) return;
    if (static_cast<size_t>(n) >= sizeof(line)) {
        const char kTail[] = " …(truncated)";
        const size_t tail = sizeof(kTail) - 1;
        std::memcpy(line + sizeof(line) - tail - 1, kTail, tail + 1);
    }

    std::lock_guard<std::mutex> lk(mtx_);

    // 控制台
    if (console_) {
        std::fprintf(stderr, "%s\n", line);
        std::fflush(stderr);
    }

    // 文件（惰性打开）
    int idx = static_cast<int>(ch);
    auto& f = impl_->files[idx];
    if (!dir_.empty()) {
        if (!f.is_open()) {
            // 递归建目录：默认路径是 %LOCALAPPDATA%\AIMFramework\logs，需要两层。
            // 旧实现用 CreateDirectoryA（只建一层），父目录不存在时静默失败 →
            // 全新机器上文件日志全部丢失，只剩控制台输出。
            std::error_code ec;
            std::filesystem::create_directories(dir_, ec);
            f.open(file_path(ch), std::ios::out | std::ios::app);
        }
        if (f.is_open()) {
            f << line << std::endl;
            f.flush();
        }
    }
}

} // namespace aim
