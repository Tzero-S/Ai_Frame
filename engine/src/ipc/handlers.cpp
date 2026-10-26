#include "ipc/handlers.h"

namespace aim::ipc {

void HandlerRegistry::Register(const std::string& cmd, Handler h) {
    handlers_[cmd] = std::move(h);
}

bool HandlerRegistry::Has(const std::string& cmd) const {
    return handlers_.find(cmd) != handlers_.end();
}

Response HandlerRegistry::Dispatch(const Request& req) const {
    auto it = handlers_.find(req.cmd);
    if (it == handlers_.end()) {
        Response r;
        r.id = req.id;
        r.code = 404;
        r.msg = "unknown cmd: " + req.cmd;
        return r;
    }
    Response r = it->second(req);
    r.id = req.id; // 统一回填请求 id
    return r;
}

} // namespace aim::ipc
