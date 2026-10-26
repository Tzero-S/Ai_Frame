# 单元测试：每个 tests/unit/*_test.cpp 产出独立可执行文件并注册为 CTest 用例。
# 每个测试文件自带 main()（见 aim_test.h）。
file(GLOB UNIT_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*_test.cpp")

# 依赖 ONNX Runtime 的用例：AIM_WITH_ORT=OFF 时不构建（避免"占位后端"造成的假失败）
set(AIM_ORT_TESTS ort_test real_model_test e2e_real_test e2e_test preview_test model_import_test)

foreach(tfile ${UNIT_TESTS})
  get_filename_component(tname ${tfile} NAME_WE)

  if(NOT AIM_WITH_ORT AND tname IN_LIST AIM_ORT_TESTS)
    message(STATUS "跳过 ${tname}（AIM_WITH_ORT=OFF，无 ONNX Runtime）")
    continue()
  endif()

  add_executable(${tname} ${tfile})
  target_include_directories(${tname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${tname} PRIVATE
    AIM_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  target_link_libraries(${tname} PRIVATE aim_engine)
  if(WIN32 AND AIM_WITH_ORT)
    add_custom_command(TARGET ${tname} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${ORT_DIR}/bin/onnxruntime.dll" "$<TARGET_FILE_DIR:${tname}>")
    # DML 用例需要与 exe 同目录的 DirectML.dll（系统自带版本可能过旧）
    if(AIM_ENABLE_DML AND EXISTS "${ORT_DIR}/bin/DirectML.dll")
      add_custom_command(TARGET ${tname} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ORT_DIR}/bin/DirectML.dll" "$<TARGET_FILE_DIR:${tname}>")
    endif()
  endif()
  if(WIN32)
    target_link_libraries(${tname} PRIVATE kernel32 user32)
  endif()
  add_test(NAME ${tname} COMMAND ${tname})
endforeach()
