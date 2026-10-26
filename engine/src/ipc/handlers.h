#pragma once
#include <functional>
#include <map>
#include <string>
#include "ipc/protocol.h"

// IPC 命令分发（§8.1）：cmd → handler 映射；未知命令返回 404。
namespace aim::ipc {

using Handler = std::function<Response(const Request&)>;

class HandlerRegistry {
public:
    void Register(const std::string& cmd, Handler h);
    bool Has(const std::string& cmd) const;
    Response Dispatch(const Request& req) const;

private:
    std::map<std::string, Handler> handlers_;
};

} // namespace aim::ipc
