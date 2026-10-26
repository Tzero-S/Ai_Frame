#pragma once

// 内置扩展点注册入口：把采集/推理/执行的内置实现登记到 SPI 工厂（§4.4）。
// 引擎启动时调用一次；第三方插件可在此之后追加注册。
namespace aim {

void RegisterBuiltinExtensions();

} // namespace aim
