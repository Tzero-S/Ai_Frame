#pragma once
#include <string>
#include <vector>

// ONNX metadata 解析（§5.10）：类别表的唯一来源。
//
// Ultralytics 在导出 ONNX 时把类别写进 ModelProto.metadata_props 的 "names" 键，
// 内容是 Python dict 的字面量，例如：
//     {0: 'body', 1: 'head'}
//     {"0": "body", "1": "head"}      ← 部分导出工具会带引号键
// 本函数把它解析成按类别索引升序排列的名字数组。
namespace aim::inference {

// 解析 names 元数据。规则：
//   1) 优先按 "整数键: 引号值" 配对解析，并按 key 数值升序排序（不依赖出现顺序）；
//   2) 键允许带引号（"0" / '0'）或不带（0）；
//   3) 值支持单/双引号，并处理反斜杠转义；
//   4) 完全解析不出键值对时，退化为"抓取所有引号内字符串"（兼容非 dict 写法）。
std::vector<std::string> ParseClassNames(const std::string& names_meta);

} // namespace aim::inference
