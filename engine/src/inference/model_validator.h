#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 模型导入校验（§5.10.1 / §5.10.3）：
//   打开 ONNX → 读 I/O 名称/类型/shape → 动态轴判定 → 类别元数据 →
//   feats 与类别数一致性 → SHA-256 → 生成校验摘要。
namespace aim::inference {

struct ModelTensorInfo {
    std::string name;
    int dtype = 0; // ONNX TensorProto 数据类型
    std::vector<int64_t> shape;
    bool dynamic = false;
};

struct ModelInfo {
    bool ok = false;
    std::string error;
    std::string file;
    std::string alias;
    std::string sha256;
    ModelTensorInfo input;
    ModelTensorInfo output;
    std::vector<std::string> classes;
    int num_classes = 0;
    int num_kpts = 0;
    std::string created_at;
};

ModelInfo InspectModel(const std::string& path);

// §5.10.3 校验摘要 JSON。
std::string ModelInfoToJson(const ModelInfo& info);

std::string DtypeName(int dtype);

bool ComputeSha256(const std::string& path, std::string& hex, std::string* err = nullptr);

} // namespace aim::inference
