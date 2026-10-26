#include "inference/model_validator.h"
#include "inference/onnx_meta.h"

// 只有 InspectModel 依赖 ONNX Runtime；SHA-256 / JSON 摘要 / dtype 名称
// 在无 ORT 构建下依然可用（见下方 #else 分支）。
#if defined(AIM_WITH_ORT)
#  if defined(_WIN32) && !defined(_stdcall)
#    define _stdcall __stdcall
#  endif
#  include <onnxruntime_cxx_api.h>
#endif

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/json.h"

namespace aim::inference {

#if defined(AIM_WITH_ORT)
namespace {

Ort::Env& GetEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "aim_engine");
    return env;
}

std::vector<std::string> ReadMetadataNames(Ort::Session& session) {
    std::vector<std::string> out;
    try {
        auto meta = session.GetModelMetadata();
        Ort::AllocatorWithDefaultOptions alloc;
        auto keys = meta.GetCustomMetadataMapKeysAllocated(alloc);
        for (auto& k : keys) {
            if (std::string(k.get()) == "names") {
                auto v = meta.LookupCustomMetadataMapAllocated("names", alloc);
                if (v) out = ParseClassNames(v.get());
                break;
            }
        }
    } catch (const Ort::Exception&) {
        // 元数据可选
    }
    return out;
}

bool IsFeatLike(int64_t n) {
    static const int kFeatLike[] = {6, 7, 8, 9, 17, 80, 81, 84, 85, 116};
    for (int v : kFeatLike) if (v == n) return true;
    return false;
}

// 轴自适应判定特征维（与 YoloDecoder §7.3 一致）
int64_t FeatCount(const std::vector<int64_t>& shape) {
    if (shape.size() != 3) return -1;
    const int64_t d1 = shape[1];
    const int64_t d2 = shape[2];
    if (IsFeatLike(d2)) return d2;
    if (IsFeatLike(d1)) return d1;
    if (d1 > 0 && d2 > 0) return std::min(d1, d2);
    return -1;
}

void FillTensor(Ort::Session& session, bool is_input, ModelTensorInfo& t) {
    Ort::TypeInfo type = is_input ? session.GetInputTypeInfo(0)
                                  : session.GetOutputTypeInfo(0);
    auto info = type.GetTensorTypeAndShapeInfo();
    t.dtype = static_cast<int>(info.GetElementType());
    t.shape = info.GetShape();
    t.dynamic = false;
    for (auto d : t.shape) {
        if (d <= 0) {
            t.dynamic = true;
            break;
        }
    }
}

} // namespace
#endif // AIM_WITH_ORT

std::string DtypeName(int dtype) {
    switch (dtype) {
        case 1:  return "float32";
        case 2:  return "uint8";
        case 3:  return "int8";
        case 6:  return "int32";
        case 7:  return "int64";
        case 10: return "float16";
        case 11: return "float64";
        default: return "unknown";
    }
}

bool ComputeSha256(const std::string& path, std::string& hex, std::string* err) {
    hex.clear();

    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (err) *err = "CryptAcquireContext failed";
        return false;
    }
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        if (err) *err = "CryptCreateHash failed";
        return false;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
        if (err) *err = "open file failed: " + path;
        return false;
    }

    char buf[65536];
    while (f) {
        f.read(buf, sizeof(buf));
        const std::streamsize n = f.gcount();
        if (n > 0 && !CryptHashData(hash, reinterpret_cast<BYTE*>(buf),
                                    static_cast<DWORD>(n), 0)) {
            CryptDestroyHash(hash);
            CryptReleaseContext(prov, 0);
            if (err) *err = "CryptHashData failed";
            return false;
        }
    }

    BYTE digest[32]{};
    DWORD len = sizeof(digest);
    const BOOL got = CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0);

    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);

    if (!got) {
        if (err) *err = "CryptGetHashParam failed";
        return false;
    }

    static const char* kHex = "0123456789abcdef";
    hex.reserve(64);
    for (DWORD i = 0; i < len; ++i) {
        hex.push_back(kHex[(digest[i] >> 4) & 0xF]);
        hex.push_back(kHex[digest[i] & 0xF]);
    }
    return true;
}

#if defined(AIM_WITH_ORT)
ModelInfo InspectModel(const std::string& path) {
    ModelInfo info;
    info.file = path;

    try {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            info.error = "model not found: " + path;
            return info;
        }

        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const std::wstring wp = std::filesystem::path(path).wstring();
        Ort::Session session(GetEnv(), wp.c_str(), so);

        Ort::AllocatorWithDefaultOptions alloc;
        auto in_name = session.GetInputNameAllocated(0, alloc);
        auto out_name = session.GetOutputNameAllocated(0, alloc);
        info.input.name = in_name.get();
        info.output.name = out_name.get();

        FillTensor(session, true, info.input);
        FillTensor(session, false, info.output);
        info.classes = ReadMetadataNames(session);

        if (info.output.shape.size() != 3) {
            info.error = "output rank != 3";
            return info;
        }

        const int64_t feats = FeatCount(info.output.shape);
        if (feats > 0) {
            int nc = info.classes.empty() ? static_cast<int>(feats) - 4
                                          : static_cast<int>(info.classes.size());
            if (nc < 1) nc = 1;
            info.num_classes = nc;
            const int rest = static_cast<int>(feats) - 4 - nc;
            info.num_kpts = (rest > 0) ? rest / 3 : 0;

            if (!info.classes.empty()) {
                const bool consistent =
                    (feats == 4 + nc) || (feats == 5 + nc + 3 * info.num_kpts);
                if (!consistent) {
                    info.error = "output feats mismatch with classes";
                    return info;
                }
            }
        } else {
            // 动态 feats：只能依赖类别元数据
            info.num_classes = info.classes.empty() ? 1
                                                    : static_cast<int>(info.classes.size());
            info.num_kpts = 0;
        }

        info.alias = std::filesystem::path(path).stem().string();

        std::string err;
        if (!ComputeSha256(path, info.sha256, &err)) {
            info.error = err;
            return info;
        }

        const std::time_t t = std::time(nullptr);
        if (const std::tm* g = std::gmtime(&t)) {
            std::tm tm = *g;
            char ts[32]{};
            std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
            info.created_at = ts;
        }

        info.ok = true;
        return info;
    } catch (const Ort::Exception& e) {
        info.error = e.what();
        return info;
    } catch (const std::exception& e) {
        info.error = e.what();
        return info;
    }
}

#else // !AIM_WITH_ORT

ModelInfo InspectModel(const std::string& path) {
    ModelInfo info;
    info.file = path;
    info.ok = false;
    info.error = "ONNX Runtime 未编入本构建（AIM_WITH_ORT=OFF）：无法读取 ONNX 元数据";
    // 无 ORT 时仍可给出文件级校验（存在性 + SHA-256），便于离线比对。
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        info.error = "model not found: " + path;
        return info;
    }
    std::string sha;
    if (ComputeSha256(path, sha, nullptr)) info.sha256 = sha;
    return info;
}

#endif // AIM_WITH_ORT

std::string ModelInfoToJson(const ModelInfo& i) {
    json::Value v;
    v["ok"] = i.ok;
    if (!i.error.empty()) v["error"] = i.error;
    v["id"] = i.alias; // 简化：id 用 alias；正式应持久化 uuid
    v["alias"] = i.alias;
    v["file"] = i.file;
    v["sha256"] = i.sha256;

    auto& in = v["input"];
    in["name"] = i.input.name;
    in["dtype"] = DtypeName(i.input.dtype);
    in["dynamic"] = i.input.dynamic;
    json::Value in_shape;
    for (auto d : i.input.shape) in_shape.push_back(json::Value(static_cast<int>(d)));
    in["shape"] = in_shape;

    auto& out = v["output"];
    out["name"] = i.output.name;
    out["dtype"] = DtypeName(i.output.dtype);
    out["dynamic"] = i.output.dynamic;
    json::Value out_shape;
    for (auto d : i.output.shape) out_shape.push_back(json::Value(static_cast<int>(d)));
    out["shape"] = out_shape;

    json::Value cls;
    for (const auto& c : i.classes) cls.push_back(json::Value(c));
    v["classes"] = cls;

    v["num_classes"] = i.num_classes;
    v["num_kpts"] = i.num_kpts;
    v["created_at"] = i.created_at;
    return json::dump(v, 2);
}

} // namespace aim::inference
