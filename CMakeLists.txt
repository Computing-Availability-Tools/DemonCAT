# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(demoncat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)
find_package(Threads REQUIRED)

set(DCAT_JSON third_party/cjson/cJSON.c)

set(DCAT_CORE
    src/core/output.c
    src/core/config.c
    src/core/registry.c
    src/core/cli.c
    src/core/executor.c
    src/core/precheck.c
    src/core/state.c
    src/core/dispatch.c
    src/injectors/injectors.c
)

add_executable(dcat src/main.c ${DCAT_CORE} ${DCAT_JSON})
target_include_directories(dcat PRIVATE src third_party/cjson)
target_link_libraries(dcat PRIVATE Threads::Threads)

enable_testing()

function(dcat_test name src)
    add_executable(${name} ${src} ${DCAT_CORE} ${DCAT_JSON})
    target_include_directories(${name} PRIVATE src third_party/cjson)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

# ---- Tier 0: 核心单元测试 (6 个) ----
dcat_test(test_output      tests/test_output.c)
dcat_test(test_registry    tests/test_registry.c)
dcat_test(test_cli         tests/test_cli.c)
dcat_test(test_executor    tests/test_executor_mock.c)
dcat_test(test_precheck    tests/test_precheck.c)
dcat_test(test_state       tests/test_state.c)

# ---- Tier 1: Mock 表驱动故障测试 (39 条全覆盖) ----
dcat_test(test_faults_cpu_storage  tests/test_faults_cpu_storage.c)
dcat_test(test_faults_network      tests/test_faults_network.c)
dcat_test(test_faults_process      tests/test_faults_process.c)
dcat_test(test_faults_npu          tests/test_faults_npu.c)

# ---- Tier 2: 脚本语法检查 (sh -n) ----
add_test(NAME test_syntax COMMAND sh tests/check_syntax.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ---- Tier 3: 真实执行测试 (无需 root 的 7 条故障) ----
dcat_test(test_smoke_cpu      tests/test_smoke_cpu.c)
dcat_test(test_smoke_process  tests/test_smoke_process.c)
dcat_test(test_smoke_storage  tests/test_smoke_storage.c)
