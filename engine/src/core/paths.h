#pragma once
#include <string>

// 数据文件定位（§5.7.1 弹道库等）：把"相对路径"解析成可用的绝对路径。
// 搜索顺序：① 按当前工作目录直接打开；② 相对可执行文件目录。
// 两者都不存在时返回空串，由调用方决定退化行为。
namespace aim {

// 可执行文件所在目录（末尾不带分隔符）；取不到返回空串。
std::string ExeDir();

// 解析数据文件；找不到返回空串。
std::string ResolveDataFile(const std::string& path);

} // namespace aim
