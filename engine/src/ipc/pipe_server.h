#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// 命名管道服务端（§8.1）：长度前缀帧 + 请求/响应回调。
// 平台相关实现（Win32）在 .cpp；公共头不引入 windows.h（§9.2）。
namespace aim::ipc {

class PipeServer {
public:
    using Callback = std::function<std::string(const std::string&)>;

    PipeServer() = default;
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    // pipe_name 不含 "\\.\pipe\" 前缀。回调返回响应 JSON 文本。
    bool Start(const std::string& pipe_name, Callback cb, std::string* err = nullptr);
    // 可选：服务线程名（M9 L2 中性化，如 "w1"）；空则不设置
    void SetThreadName(std::string name) { thread_name_ = std::move(name); }
    void Stop();
    bool Running() const { return running_.load(); }

private:
    void Loop();

    void* pipe_ = nullptr; // HANDLE
    std::string name_;
    std::string thread_name_;
    Callback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mtx_;
};

} // namespace aim::ipc
