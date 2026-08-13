cmake_minimum_required(VERSION 3.10)
project(demoncat C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
# add_compile_definitions 需 CMake 3.12+，用 add_definitions 保持 3.10 兼容
add_definitions(-D_POSIX_C_SOURCE=200809L)
find_package(Threads REQUIRED)

add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)
set_target_properties(cjson PROPERTIES POSITION_INDEPENDENT_CODE ON)


set(DCAT_CORE
    src/core/types.c src/core/cli.c src/core/registry.c src/core/executor.c
    src/core/precheck.c src/core/state.c src/core/config.c
    src/core/output.c src/core/serve.c src/core/dispatch.c src/core/reinject.c src/core/help.c
    src/injectors/injectors.c src/plugins/plugin_manager.c)
add_executable(dcat src/main.c ${DCAT_CORE})
target_include_directories(dcat PRIVATE src src/core src/injectors src/plugins third_party/cjson)
target_link_libraries(dcat PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})

# ---- 全局入口：符号链接指向项目树 build/dcat ----
# config/scripts/plugins/web 均按可执行文件位置解析（/proc/self/exe 解引用到项目树），
# git pull 更新脚本/配置后立即生效，无需重新 install。
set(_dcat_entry_code "
  set(_dest \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin\")
  file(MAKE_DIRECTORY \"\${_dest}\")
  execute_process(COMMAND \"${CMAKE_COMMAND}\" -E remove -f \"\${_dest}/dcat\" OUTPUT_QUIET ERROR_QUIET)
  execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                  \"${CMAKE_SOURCE_DIR}/build/dcat\" \"\${_dest}/dcat\"
                  RESULT_VARIABLE _sl_rc)
  if(NOT _sl_rc EQUAL 0)
    message(FATAL_ERROR \"failed to create global dcat entry: \${_dest}/dcat\")
  endif()
  message(STATUS \"global entry: \${_dest}/dcat -> ${CMAKE_SOURCE_DIR}/build/dcat\")
")
install(CODE "${_dcat_entry_code}")
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_INSTALL_PREFIX}/bin/dcat"
    COMMENT "Removing global dcat entry (${CMAKE_INSTALL_PREFIX}/bin/dcat)")
# make clean 连带删除全局入口（入口存在时需 sudo）。
# ADDITIONAL_CLEAN_FILES 需 CMake 3.15+；3.10-3.14 下静默不生效，请改用 `make uninstall`。
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.15)
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${CMAKE_INSTALL_PREFIX}/bin/dcat")
endif()

enable_testing()

function(dcat_test name src)
    add_executable(${name} ${src} ${DCAT_CORE})
    target_include_directories(${name} PRIVATE src src/core src/injectors src/plugins third_party/cjson tests)
    target_link_libraries(${name} PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

# ---- Tier 0: 核心单元测试 ----
dcat_test(test_types        tests/test_types.c)
dcat_test(test_output       tests/test_output.c)
dcat_test(test_config       tests/test_config.c)
dcat_test(test_registry     tests/test_registry.c)
dcat_test(test_executor     tests/test_executor_mock.c)
dcat_test(test_precheck     tests/test_precheck.c)
dcat_test(test_state        tests/test_state.c)
dcat_test(test_injectors    tests/test_injectors.c)
dcat_test(test_dispatch     tests/test_dispatch.c)
dcat_test(test_reinject     tests/test_reinject.c)
dcat_test(test_cli          tests/test_cli.c)
dcat_test(test_faults       tests/test_faults.c)
dcat_test(test_help         tests/test_help.c)

# ---- Tier 0b: 插件测试 ----
dcat_test(test_plugin_manager     tests/test_plugin_manager.c)
dcat_test(test_plugin_integration tests/test_plugin_integration.c)

# ---- Tier 1: Mock 表驱动故障测试 (v0.1 33 条 + 批次2 30 条全覆盖) ----
dcat_test(test_faults_cpu_storage  tests/test_faults_cpu_storage.c)
dcat_test(test_faults_network      tests/test_faults_network.c)
dcat_test(test_faults_process     tests/test_faults_process.c)
dcat_test(test_faults_npu         tests/test_faults_npu.c)
dcat_test(test_faults_batch2_newmods tests/test_faults_batch2_newmods.c)
dcat_test(test_faults_batch2_ext     tests/test_faults_batch2_ext.c)

# ---- Tier 2: 脚本语法检查 (sh -n) ----
add_test(NAME test_syntax COMMAND sh tests/check_syntax.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ---- Tier 3: 真实执行测试 (无需 root) ----
dcat_test(test_smoke_cpu      tests/test_smoke_cpu.c)
dcat_test(test_smoke_process  tests/test_smoke_process.c)
dcat_test(test_smoke_storage  tests/test_smoke_storage.c)
dcat_test(test_smoke_state_lost tests/test_smoke_state_lost.c)

# ---- 示例动态插件 (编译到 plugins/libsample.so) ----
add_library(sample_plugin MODULE
    src/plugins/sample/sample_plugin.c
    src/core/output.c
    src/core/types.c)
target_include_directories(sample_plugin PRIVATE src src/core src/plugins third_party/cjson)
target_link_libraries(sample_plugin PRIVATE cjson)
set_target_properties(sample_plugin PROPERTIES
    OUTPUT_NAME "sample"
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/plugins")

# 集成测试依赖 .so 先构建
if(TARGET sample_plugin AND TARGET test_plugin_integration)
    add_dependencies(test_plugin_integration sample_plugin)
endif()

# ---- NPU ACL 压力工具 (可选, 检测到 CANN 则编译) ----
set(ASCEND_TOOLKIT "/usr/local/Ascend/ascend-toolkit/latest")
if(EXISTS "${ASCEND_TOOLKIT}/aarch64-linux/include/acl/acl.h")
    add_executable(_npu_stress src/scripts/npu/_npu_stress.c)
    target_include_directories(_npu_stress PRIVATE "${ASCEND_TOOLKIT}/aarch64-linux/include")
    target_link_directories(_npu_stress PRIVATE "${ASCEND_TOOLKIT}/aarch64-linux/lib64")
    target_link_libraries(_npu_stress PRIVATE ascendcl acl_cblas)
    set_target_properties(_npu_stress PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
    message(STATUS "NPU stress tool: enabled (ACL found)")
else()
    message(STATUS "NPU stress tool: disabled (ACL not found)")
endif()
