cmake_minimum_required(VERSION 3.10)
project(demoncat C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)
find_package(Threads REQUIRED)

add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)
set_target_properties(cjson PROPERTIES POSITION_INDEPENDENT_CODE ON)


set(DCAT_CORE
    src/core/types.c src/core/cli.c src/core/registry.c src/core/executor.c
    src/core/precheck.c src/core/state.c src/core/config.c
    src/core/output.c src/core/dispatch.c src/core/reinject.c src/core/help.c
    src/injectors/injectors.c src/plugins/plugin_manager.c)
add_executable(dcat src/main.c ${DCAT_CORE})
target_include_directories(dcat PRIVATE src src/core src/injectors src/plugins third_party/cjson)
target_link_libraries(dcat PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})

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

# ---- Tier 1: Mock 表驱动故障测试 (38 条全覆盖) ----
dcat_test(test_faults_cpu_storage  tests/test_faults_cpu_storage.c)
dcat_test(test_faults_network      tests/test_faults_network.c)
dcat_test(test_faults_process     tests/test_faults_process.c)
dcat_test(test_faults_npu         tests/test_faults_npu.c)

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
