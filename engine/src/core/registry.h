#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <functional>

// SPI 统一工厂注册（§4.4）：所有扩展点（采集/推理/执行/覆盖层/热键）均通过
// `aim::Registry` 按类型 + 名称注册与创建，业务侧零分支。
//
// 用法（对齐文档 §4.4 代码片段）：
//   aim::Registry::instance().register_factory<aim::spi::ICapture>("dxgi", [] {
//       return std::make_unique<DxgiCapture>();
//   });
//   auto cap = aim::Registry::instance().create<aim::spi::ICapture>("dxgi");
namespace aim {

// 单类型工厂注册表：每种扩展点接口 T 对应一个进程级单例。
template <typename T>
class FactoryRegistry {
public:
    using Factory = std::function<std::unique_ptr<T>()>;

    static FactoryRegistry<T>& instance() {
        static FactoryRegistry<T> reg;
        return reg;
    }

    void register_factory(const std::string& name, Factory fn) {
        map_[name] = std::move(fn);
    }

    bool has(const std::string& name) const {
        return map_.find(name) != map_.end();
    }

    std::unique_ptr<T> create(const std::string& name) const {
        auto it = map_.find(name);
        if (it == map_.end()) return nullptr;
        return it->second();
    }

    std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(map_.size());
        for (const auto& kv : map_) out.push_back(kv.first);
        return out;
    }

    void clear() { map_.clear(); }

private:
    FactoryRegistry() = default;
    std::map<std::string, Factory> map_;
};

// 类型化门面，直接暴露 §4.4 中 `aim::Registry::instance().register_factory<T>(...)`
// 形式的接口。
struct Registry {
    template <typename T>
    static FactoryRegistry<T>& of() {
        return FactoryRegistry<T>::instance();
    }

    template <typename T>
    static void register_factory(const std::string& name,
                                 typename FactoryRegistry<T>::Factory fn) {
        FactoryRegistry<T>::instance().register_factory(name, std::move(fn));
    }

    template <typename T>
    static bool has(const std::string& name) {
        return FactoryRegistry<T>::instance().has(name);
    }

    template <typename T>
    static std::unique_ptr<T> create(const std::string& name) {
        return FactoryRegistry<T>::instance().create(name);
    }

    template <typename T>
    static std::vector<std::string> names() {
        return FactoryRegistry<T>::instance().names();
    }
};

} // namespace aim
