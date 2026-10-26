cmake_minimum_required(VERSION 3.20)
project(AimEngine CXX)

# C++20（§9.2）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 工具链策略（§3.2 / §10.2）：
#   主路径 = MinGW-w64 g++ 16.2 + Ninja（本机已验证；MSVC 缺 Windows SDK 导入库，无法 link）。
#   可选路径 = MSVC v143（需装齐 VS2022 Build Tools + Windows SDK 后自动生效）。
if(MSVC)
  # §9.2 编译选项：/W4 /permissive- /Zc:__cplusplus /guard:cf
  # /utf-8：源码为 UTF-8（含中文注释），不加会被按系统代码页(936)解析导致 C4819 与语法误判
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /guard:cf /utf-8)
  # getenv 在 MSVC 下触发 C4996（"不安全"）。全仓已确认未使用 strcpy/strcat/无界 sprintf，
  # 因此仅关闭该弃用告警，不影响真实越界检查。
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  # MinGW / GCC：与 MSVC 同等的告警级别
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Windows 头文件的 min/max 宏会破坏 std::min/std::max（MSVC C2589），统一禁用
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# ONNX Runtime（可选依赖，§5.3.1）
#   AIM_WITH_ORT=ON  → 编入真实 ORT 后端，需 ORT_DIR 指向含 include/lib/bin 的目录
#   AIM_WITH_ORT=OFF → 仅编入占位后端：接口/注册名不变，load() 返回 false 并给出明确原因
# 说明：真实 ORT 头由 backend_ort.cpp / model_validator.cpp 直接 include，
#       因此"找不到 ORT"必须在 configure 阶段解决，不能留到编译期。
# ---------------------------------------------------------------------------
option(AIM_WITH_ORT "启用 ONNX Runtime 推理后端" ON)

set(ORT_DIR "$ENV{ORT_DIR}" CACHE PATH "ONNX Runtime 根目录（含 include/lib/bin）")

if(AIM_WITH_ORT AND NOT ORT_DIR)
  # 本机预置路径（文档 §3.1）。存在则自动采用，不存在则下面统一降级。
  if(EXISTS "D:/PATH/onnxruntime/include/onnxruntime_cxx_api.h")
    set(ORT_DIR "D:/PATH/onnxruntime" CACHE PATH "ONNX Runtime 根目录（含 include/lib/bin）" FORCE)
  endif()
endif()

if(AIM_WITH_ORT)
  if(NOT EXISTS "${ORT_DIR}/include/onnxruntime_cxx_api.h")
    if(ORT_DIR)
      message(FATAL_ERROR
        "ORT_DIR='${ORT_DIR}' 下缺少 include/onnxruntime_cxx_api.h。\n"
        "请修正 ORT_DIR，或显式关闭 ORT：-DAIM_WITH_ORT=OFF")
    else()
      message(STATUS "AIM_WITH_ORT 自动降级为 OFF：未找到 ONNX Runtime（可用 -DORT_DIR=<path> 指定）")
      set(AIM_WITH_ORT OFF)
    endif()
  else()
    message(STATUS "AIM_WITH_ORT=ON（ORT_DIR=${ORT_DIR}）")
  endif()
endif()

# ---------------------------------------------------------------------------
# DirectML EP（§5.3.1）：dml_provider_factory.h 依赖 Windows SDK 的 DirectML.h /
# d3d12.h，只有 MSVC + 完整 Windows SDK 才能编译，因此非 MSVC 自动关闭。
# ---------------------------------------------------------------------------
option(AIM_ENABLE_DML "启用 ONNX Runtime DirectML EP（需 MSVC + Windows SDK）" ON)

if(AIM_ENABLE_DML)
  if(NOT AIM_WITH_ORT)
    message(STATUS "AIM_ENABLE_DML 自动关闭：AIM_WITH_ORT=OFF")
    set(AIM_ENABLE_DML OFF)
  elseif(NOT MSVC)
    message(STATUS "AIM_ENABLE_DML 自动关闭：DML EP 需要 MSVC 工具链（当前编译器非 MSVC）")
    set(AIM_ENABLE_DML OFF)
  else()
    message(STATUS "AIM_ENABLE_DML=ON（DirectML EP 可用）")
  endif()
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)

# 演示程序（不参与测试，手动运行）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/pipeline_demo.cpp")
  add_executable(aim_pipeline_demo demo/pipeline_demo.cpp)
  target_link_libraries(aim_pipeline_demo PRIVATE aim_engine)
  if(WIN32)
    target_link_libraries(aim_pipeline_demo PRIVATE winmm)
  endif()
endif()
