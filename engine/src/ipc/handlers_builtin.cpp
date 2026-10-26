#include "ipc/handlers_builtin.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/base64.h"
#include "core/clock.h"
#include "core/config.h"
#include "core/json.h"
#include "core/paths.h"
#include "fov/fov_estimator.h"
#include "fov/resolution_detect.h"
#include "inference/backend_ort.h"
#include "inference/model_validator.h"
#include "inference/preview.h"
#include "ipc/protocol.h"

namespace aim::ipc {

void RegisterBuiltinHandlers(HandlerRegistry& reg, ConfigStore& store, const EngineRuntime& rt) {
    reg.Register(cmd::kEngineHello, [&store](const Request&) {
        Response r;
        r.code = 0;
        r.msg = "ok";
        r.payload["engine_version"] = "1.0.0";
        r.payload["schema_version"] = store.Get().schema_version;
        return r;
    });

    reg.Register(cmd::kConfigGet, [&store](const Request&) {
        Response r;
        json::Value cfg;
        std::string e;
        if (!json::parse(ConfigManager::ToJson(store.Get(), 0), cfg, e)) {
            r.code = 500;
            r.msg = "config serialize failed";
            return r;
        }
        r.payload["config"] = cfg;
        r.payload["version"] = store.Version();
        r.code = 0;
        r.msg = "ok";
        return r;
    });

    reg.Register(cmd::kConfigApply, [&store](const Request& req) {
        Response r;
        const json::Value* cfg = req.payload.find("config");
        if (!cfg) {
            r.code = 400;
            r.msg = "missing config";
            return r;
        }
        // §5.1.3：默认 merge（只覆盖提交到的字段），"replace" 为整份替换。
        // 默认 merge 可避免控制台只渲染部分分组时把 anti_detect/capture 重置为默认。
        const std::string mode = req.payload["mode"].asString("merge");
        const bool replace = (mode == "replace");
        const auto applied = replace ? store.Apply(*cfg) : store.ApplyMerge(*cfg);
        if (!applied.ok()) {
            r.code = 400;
            r.msg = applied.error().message;
            return r;
        }
        r.payload["applied_version"] = applied.value();
        r.payload["mode"] = replace ? "replace" : "merge";
        r.code = 0;
        r.msg = "ok";
        return r;
    });

    // fov.estimate：把「分辨率 → HFOV/VFOV」的唯一实现放在引擎侧。
    // 控制台原本自己抄了一份查表（FovSettings.tsx），两边锚点一旦不同步就会出现
    // 「界面显示 103°、引擎实际用 110°」这类问题——分辨率识别这块的原始 bug
    // 正是两边共用了一个错误的 16:10 锚点。现在界面只负责显示引擎算出来的值。
    reg.Register(cmd::kFovEstimate, [&store](const Request& req) {
        Response r;
        const Config& cfg = store.Get();

        // 分辨率：请求里给了就用请求的（界面上可试算别的分辨率），否则用引擎自己识别的
        int w = req.payload["width"].asInt(0);
        int h = req.payload["height"].asInt(0);
        int mon = cfg.capture.output_monitor;
        bool detected = false;
        if (w > 0 && h > 0) {
            detected = true;
        } else {
            detected = GetMonitorResolution(mon, w, h);
            if (!detected) detected = GetPrimaryResolution(w, h);
        }

        json::Value screen;
        screen["width"] = w;
        screen["height"] = h;
        screen["monitor"] = mon;
        screen["detected"] = detected;
        r.payload["screen"] = screen;

        if (!detected) {
            r.code = 404;
            r.msg = "分辨率识别失败（无法枚举显示设置）";
            return r;
        }

        const auto e = EstimateFov(w, h, cfg.fov);
        r.payload["aspect"] = static_cast<double>(w) / static_cast<double>(h);
        r.payload["auto_hfov_deg"] = e.auto_hfov_deg;
        r.payload["auto_vfov_deg"] = e.auto_vfov_deg;
        r.payload["hfov_deg"] = e.fov.hfov_deg;
        r.payload["vfov_deg"] = e.fov.vfov_deg;
        r.payload["manual"] = e.manual;
        r.payload["inconsistent"] = e.inconsistent;
        r.payload["ads_applied"] = e.ads_applied;
        r.code = 0;
        r.msg = "ok";
        return r;
    });

    // weapon.patterns：把弹道库的真实条目（id / name / rpm / 采样点数）回给控制台，
    // 让「弹道库」下拉跟随数据文件，而不是界面里写死几个武器名。
    // 与 RecoilEngine 用同一套路径解析（ResolveDataFile），保证"界面看到的"就是"引擎加载的"。
    reg.Register(cmd::kWeaponPatterns, [&store](const Request& req) {
        Response r;
        std::string want = req.payload["path"].asString();
        if (want.empty()) want = store.Get().weapon.recoil.patterns_path;

        json::Value items; // 强制初始化为数组
        items.reset(json::JType::Array);
        json::Value out;
        out["items"] = items;
        out["path"] = "";
        out["requested"] = want;

        if (want.empty()) {
            r.code = 404;
            r.msg = "patterns_path 为空（未配置弹道库）";
            r.payload = out;
            return r;
        }
        const std::string path = ResolveDataFile(want);
        if (path.empty()) {
            r.code = 404;
            r.msg = "弹道库未找到：" + want;
            r.payload = out;
            return r;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            r.code = 500;
            r.msg = "弹道库打不开：" + path;
            r.payload = out;
            return r;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        json::Value doc;
        std::string e;
        if (!json::parse(text, doc, e)) {
            r.code = 500;
            r.msg = "弹道库解析失败：" + e;
            r.payload = out;
            return r;
        }
        const json::Value* arr = doc.find("patterns");
        if (!arr || !arr->isArray()) {
            r.code = 500;
            r.msg = "弹道库缺少 patterns 数组";
            r.payload = out;
            return r;
        }
        json::Value list;
        list.reset(json::JType::Array);
        for (const auto& p : arr->arr) {
            const std::string id = p.at("id").asString();
            if (id.empty()) continue; // 无 id 的条目引擎也加载不了，直接跳过
            json::Value item;
            item["id"] = id;
            const std::string name = p.at("name").asString();
            item["name"] = name.empty() ? id : name;
            item["rpm"] = p.at("rpm").asInt(0);
            const json::Value* s = p.find("samples");
            item["samples"] = (s && s->isArray()) ? static_cast<int>(s->arr.size()) : 0;
            list.push_back(std::move(item));
        }
        out["items"] = list;
        out["path"] = path;
        r.payload = out;
        r.code = 0;
        r.msg = "ok";
        return r;
    });

    reg.Register(cmd::kStatusSnapshot, [&rt](const Request&) {
        Response r;
        r.code = 0;
        r.msg = "ok";
        r.payload["status"] = rt.SnapshotJson(Clock::NowMs());
        return r;
    });

    reg.Register(cmd::kModelImport, [](const Request& req) {
        Response r;
        const std::string path = req.payload["path"].asString();
        const auto info = inference::InspectModel(path);
        if (!info.ok) {
            r.code = 400;
            r.msg = info.error;
            return r;
        }
        json::Value model;
        std::string e;
        if (json::parse(inference::ModelInfoToJson(info), model, e)) {
            r.payload["model"] = model;
        }
        r.code = 0;
        r.msg = "ok";
        return r;
    });

    reg.Register(cmd::kModelPreview, [](const Request& req) {
        Response r;

        const std::string model_path = req.payload["model_path"].asString();
        const int w = req.payload["width"].asInt(0);
        const int h = req.payload["height"].asInt(0);
        const std::string b64 = req.payload["bgra_base64"].asString();

        std::vector<uint8_t> bgra;
        std::string e;
        if (!base64::Decode(b64, bgra, &e)) {
            r.code = 400;
            r.msg = e;
            return r;
        }
        // 尺寸校验：用 int64 计算期望长度，避免 w*h*4 在 int 上溢出后绕过检查
        if (w <= 0 || h <= 0) {
            r.code = 400;
            r.msg = "width/height must be positive";
            return r;
        }
        const int64_t expect_bytes = static_cast<int64_t>(w) * h * 4;
        constexpr int64_t kMaxFrameBytes = 64LL * 1024 * 1024; // 单帧上限 64MB（≈4096²）
        if (expect_bytes > kMaxFrameBytes) {
            r.code = 400;
            r.msg = "frame too large";
            return r;
        }
        if (static_cast<int64_t>(bgra.size()) != expect_bytes) {
            r.code = 400;
            r.msg = "bgra size mismatch";
            return r;
        }

        const auto info = inference::InspectModel(model_path);
        if (!info.ok) {
            r.code = 400;
            r.msg = info.error;
            return r;
        }

        spi::ModelSpec ms;
        ms.path = model_path;
        ms.imgsz = (info.input.shape.size() == 4 && info.input.shape[2] > 0)
                       ? static_cast<int>(info.input.shape[2])
                       : 960;
        ms.num_classes = info.num_classes;
        ms.num_kpts = info.num_kpts;
        ms.conf_threshold = static_cast<float>(req.payload["conf"].asNumber(0.35));
        ms.input_nhwc_u8 = false;

        inference::OrtBackend be(inference::OrtEp::Cpu);
        if (!be.load(ms)) {
            r.code = 500;
            r.msg = "model load failed";
            return r;
        }

        spi::Frame f;
        f.w = w;
        f.h = h;
        f.bgra = bgra.data();
        f.valid = true;

        inference::PreviewOptions po;
        po.conf_threshold = ms.conf_threshold;
        po.iou_threshold = static_cast<float>(req.payload["iou"].asNumber(0.45));
        if (req.payload["classes"].isArray()) {
            for (const auto& c : req.payload["classes"].arr) {
                po.class_whitelist.push_back(c.asInt());
            }
        }

        const auto pr = inference::PreviewFrame(be, f, po);
        if (!pr.ok) {
            r.code = 500;
            r.msg = pr.error;
            return r;
        }

        json::Value dets;
        std::string je;
        if (json::parse(inference::DetectionsToJson(pr.detections), dets, je)) {
            r.payload["detections"] = dets;
        }
        r.payload["raw_count"] = pr.raw_count;
        r.payload["infer_ms"] = pr.counters.infer_ms;
        r.code = 0;
        r.msg = "ok";
        return r;
    });
}

} // namespace aim::ipc
