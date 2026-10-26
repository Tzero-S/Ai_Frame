#pragma once
#include <string>
#include <string_view>
#include <mutex>
#include <memory>

// 轻量日志实现（§3.1 / §5.14）。
//
// 偏离说明：文档 §3.1 的选型为 spdlog，但本环境未安装 vcpkg/spdlog。
// 为不引入未托管的第三方依赖，这里提供接口与行为对齐 spdlog 的轻量实现
// （分级、多通道、滚动文件、控制台开关、trace 默认关闭）。后续若接入 spdlog，
// 仅需替换本文件实现，调用方宏保持不变。
//
// 通道（§5.14）：engine / perf / audit / trace / crash，各落独立滚动文件。
// 级别（§5.14）：TRACE < DEBUG < INFO < WARN < ERROR；热路径仅留 TRACE（默认关闭）。

namespace aim {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

enum class LogChannel { Engine, Perf, Audit, Trace, Crash };

class Logger {
public:
    static Logger& instance();

    // 设置日志目录（§5.14 默认 %LOCALAPPDATA%\AIMFramework\logs）。
    // 传入空字符串则仅输出到控制台。
    void set_directory(std::string_view dir);
    void set_console(bool on) { console_ = on; }
    void set_level(LogLevel lvl) { min_level_ = lvl; }
    LogLevel level() const { return min_level_; }

    // 写入一条日志。will_drop 用于热路径按需判断是否真的要格式化（避免无效开销）。
    void write(LogChannel ch, LogLevel lvl, std::string_view msg);

    // 便捷通道级封装
    void engine(LogLevel lvl, std::string_view msg) { write(LogChannel::Engine, lvl, msg); }
    void perf(std::string_view msg)   { write(LogChannel::Perf, LogLevel::Info, msg); }
    void audit(std::string_view msg)  { write(LogChannel::Audit, LogLevel::Info, msg); }
    void crash(std::string_view msg)  { write(LogChannel::Crash, LogLevel::Error, msg); }

    // 取某通道当前日志文件路径（用于诊断包导出）。
    std::string file_path(LogChannel ch) const;

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mtx_;
    std::string dir_;
    bool console_ = true;
    LogLevel min_level_ = LogLevel::Info;
};

// 级别 => 短名（用于文件/控制台输出）
const char* to_string(LogLevel lvl);
const char* to_string(LogChannel ch);

} // namespace aim

// ---- 宏（热路径用 _HOT 变体，TRACE 默认关闭，不格式化）----
#define AIM_LOG(ch, lvl, msg) ::aim::Logger::instance().write(::aim::LogChannel::ch, ::aim::LogLevel::lvl, (msg))
#define AIM_ENGINE(lvl, msg)  ::aim::Logger::instance().engine(::aim::LogLevel::lvl, (msg))
#define AIM_PERF(msg)         ::aim::Logger::instance().perf((msg))
#define AIM_AUDIT(msg)        ::aim::Logger::instance().audit((msg))
#define AIM_CRASH(msg)        ::aim::Logger::instance().crash((msg))
