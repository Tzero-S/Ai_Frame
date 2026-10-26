# 引擎静态库：聚合全部源码（头 + 实现）。逐模块新增时本 glob 自动纳入，
# 无需手工登记每个文件（保持与 §9.1 目录结构一致）。
file(GLOB_RECURSE AIM_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# main.cpp 单独构成可执行入口，不进入静态库（避免与测试的 main 冲突）
list(REMOVE_ITEM AIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

add_library(aim_engine STATIC ${AIM_SOURCES})
target_include_directories(aim_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(aim_engine_exe main.cpp)
target_link_libraries(aim_engine_exe PRIVATE aim_engine)

# 产物名去标识（§5.12 配套）：进程列表里不该出现 aim/engine 字样。
# 目标名仍为 aim_engine_exe，所有 CMake 内部引用（POST_BUILD / TARGET_FILE_DIR）不受影响。
set_target_properties(aim_engine_exe PROPERTIES OUTPUT_NAME "helper")

# 数据文件（弹道库等）随可执行文件部署：ResolveDataFile 会按 exe 目录查找
set(AIM_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../data")
if(EXISTS "${AIM_DATA_DIR}")
  add_custom_command(TARGET aim_engine_exe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${AIM_DATA_DIR}"
            "$<TARGET_FILE_DIR:aim_engine_exe>/data"
    COMMENT "deploy data/ next to aim_engine_exe")
endif()

# 平台链接（Windows）。Win32 绑定模块（capture/actuator/hotkey/overlay/fov/ipc）
# 需要这些系统库；非绑定模块不强制。
if(WIN32)
  target_link_libraries(aim_engine PUBLIC
    kernel32 user32 gdi32 d3d11 dxgi shell32 ws2_32 setupapi advapi32 gdiplus ole32)
endif()

# ONNX Runtime（AIM_WITH_ORT=ON 时才链接；OFF 时由 backend_ort.cpp 的占位实现兜底）
if(AIM_WITH_ORT)
  target_compile_definitions(aim_engine PUBLIC AIM_WITH_ORT=1)
  target_include_directories(aim_engine PUBLIC "${ORT_DIR}/include")
  if(WIN32)
    # ORT 头在 Windows 用 MSVC 的 _stdcall；MinGW 映射为 __stdcall（MSVC 下同样合法）
    target_compile_definitions(aim_engine PUBLIC _stdcall=__stdcall)
    target_link_libraries(aim_engine PUBLIC "${ORT_DIR}/lib/onnxruntime.lib")
  else()
    target_link_libraries(aim_engine PUBLIC onnxruntime)
  endif()
endif()

# DirectML EP：编译期宏；DML 设备由 onnxruntime.dll 内部创建，无需额外链接 DirectML.lib
if(AIM_ENABLE_DML)
  target_compile_definitions(aim_engine PUBLIC AIM_ENABLE_DML=1)
endif()

# 把 ORT DLL 放到引擎 exe 同目录（避免被系统目录里的旧版 ORT 抢先加载）
if(WIN32 AND AIM_WITH_ORT)
  add_custom_command(TARGET aim_engine_exe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ORT_DIR}/bin/onnxruntime.dll" "$<TARGET_FILE_DIR:aim_engine_exe>")
endif()

# DML EP 依赖 DirectML.dll：系统自带版本可能过旧（如 Win10 的 1.8.0），
# ORT 1.20 需要 ≥1.13，因此把 ORT 运行目录里的 DirectML.dll 一并拷到 exe 同目录。
if(WIN32 AND AIM_ENABLE_DML)
  if(EXISTS "${ORT_DIR}/bin/DirectML.dll")
    add_custom_command(TARGET aim_engine_exe POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${ORT_DIR}/bin/DirectML.dll" "$<TARGET_FILE_DIR:aim_engine_exe>")
  else()
    message(WARNING
      "AIM_ENABLE_DML=ON 但 ${ORT_DIR}/bin/DirectML.dll 不存在；"
      "运行期将回落到系统 DirectML（需 ≥1.13，否则 DML 会话初始化会失败）")
  endif()
endif()
