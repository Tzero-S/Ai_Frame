#include "opsec/anti_debug.h"

#include <windows.h>

#include "core/logger.h"

namespace aim::opsec {

bool IsDebuggerAttached() {
    if (IsDebuggerPresent()) return true;

    BOOL remote = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote) return true;
    return false;
}

bool AntiDebugGate(bool enabled) {
    if (!enabled) return false;

    const bool attached = IsDebuggerAttached();
    if (attached) {
        AIM_AUDIT("anti-debug: debugger detected, gate active");
    }
    return attached;
}

} // namespace aim::opsec
