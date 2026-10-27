#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 命名管道服务端（§8.1）：长度前缀帧 + 请求/响应回调。
// 平台相关实现（Win32）在 .cpp；公共头不引入 windows.h（§9.2）。
//
// 线程模型：
//   * accept 线程（thread_）只负责 CreateNamedPipe + ConnectNamedPipe，连上后
//     把连接交给一个独立的工作线程，然后立刻回去等下一条连接 —— 否则控制台
//     建立长连接后，脚本/第二个客户端会被永久阻塞。
//   * 每条连接一个工作线程，句柄**只由持有它的线程关闭**。
//     Stop() 只做 CancelIoEx，不 CloseHandle：Win32 要求取消后等操作返回再关闭，
//     否则工作线程可能在句柄被释放/复用后继续操作它（句柄竞态）。
namespace aim::ipc {

class PipeServer {
public:
    using Callback = std::function<std::string(const std::string&)>;

    // 并发连接上限：控制台 + 脚本 + 诊断工具足够；超出时拒绝新连接而不是无限起线程
    static constexpr size_t kMaxConnections = 8;

    PipeServer() = default;
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    // pipe_name 不含 "\\.\pipe\" 前缀。回调返回响应 JSON 文本。
    // 回调可能被多个连接线程并发调用，实现方需自行保证线程安全。
    bool Start(const std::string& pipe_name, Callback cb, std::string* err = nullptr);
    // 可选：服务线程名（M9 L2 中性化，如 "w1"）；空则不设置
    void SetThreadName(std::string name) { thread_name_ = std::move(name); }
    void Stop();
    bool Running() const { return running_.load(); }

private:
    struct Worker {
        std::thread th;
        std::shared_ptr<std::atomic<bool>> done;
    };

    void Loop();
    void ServeConnection(void* handle);
    void SweepFinishedWorkers(); // 调用方需持 workers_mtx_

    void* accept_pipe_ = nullptr;  // 正在 ConnectNamedPipe 的实例（HANDLE）
    std::vector<void*> conns_;     // 活跃连接（HANDLE），供 Stop 取消 I/O
    std::vector<Worker> workers_;
    std::string name_;
    std::string thread_name_;
    Callback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mtx_;          // 保护 accept_pipe_ / conns_
    std::mutex workers_mtx_;  // 保护 workers_
};

} // namespace aim::ipc
