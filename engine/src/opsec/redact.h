#pragma once
#include <string>
#include <string_view>

// 路径脱敏（§5.12 / anti_detect.strip_paths）
//
// 为什么需要：日志与错误信息里带完整绝对路径，等于把自己的安装位置、用户名、
// 项目目录全部写进文件。`strip_paths` 这个配置项一直存在，但此前**没有任何实现**。
//
// 规则：把形如 `C:\a\b\c.onnx`、`\\server\share\x`、`/home/u/x` 的路径替换成
// `…\c.onnx`（只保留最后一段），其余文本原样保留。
namespace aim::opsec {

// 开关（默认开，与 anti_detect.strip_paths 默认值一致）。
// 由 RunLoop/入口在读到配置后同步。
void SetPathScrubbing(bool on);
bool PathScrubbingEnabled();

// 脱敏一段文本：路径只保留文件名，其它内容不变。
std::string ScrubPaths(std::string_view text);

} // namespace aim::opsec
