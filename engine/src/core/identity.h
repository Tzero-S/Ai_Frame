#pragma once
#include <string>

// 运行期身份（去标识化，§5.12 配套）
//
// 为什么需要：命名管道名是**最容易被枚举的指纹**。反作弊或任何进程只要遍历
// `\\.\pipe\` 就能看到名字；叫 `aim_engine` 等于把用途写在脸上。而进程名可以
// 随便叫，管道名却是硬编码的。
//
// 做法：默认生成随机管道名（10 位十六进制，无可读前缀），并把名字写到
// `%TEMP%\aimframe.pipe` 供控制台发现；`--pipe` 显式指定时完全走老路径。
// 附带收益：多个实例不再抢同一个管道名。
namespace aim::identity {

// 生成随机管道名（不含 `\\.\pipe\` 前缀），形如 "a3f9c1d2e4"
std::string MakeEphemeralPipeName();

// 发现文件路径（%TEMP%\aimframe.pipe）；取不到临时目录时返回空串
std::string DiscoveryFilePath();

// 把管道名写入发现文件。成功返回写入的路径，失败返回空串。
std::string WriteDiscovery(const std::string& pipe_name);

// 读取发现文件里的管道名；文件不存在/为空返回空串。
std::string ReadDiscovery();

// 删除发现文件（退出时清理，避免留下陈旧名字）
void RemoveDiscovery();

} // namespace aim::identity
