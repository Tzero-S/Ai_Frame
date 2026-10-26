#pragma once
#include <cstddef>
#include <string>

// L2 进程层（§5.12.2）：失焦即停、隐藏窗口、内存清零、线程名中性化。
namespace aim::opsec {

class ProcessGuard {
public:
    void Configure(bool hide_on_focus_loss, const std::string& title_pattern);

    bool IsGameFocused() const;
    bool ShouldPause() const;

    // 纯函数：大小写不敏感子串匹配（空 pattern = 总是匹配）
    static bool TitleMatches(const std::string& title, const std::string& pattern);

    // 工具窗口样式：不在任务栏/Alt-Tab 出现
    static void ApplyToolWindow(void* hwnd);

    static void SecureZero(void* p, std::size_t n);
    static bool SetNeutralThreadName(const char* name);

private:
    bool hide_on_focus_loss_ = true;
    std::string title_pattern_;
};

} // namespace aim::opsec
