# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(demoncat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
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

dcat_test(test_output      tests/test_output.c)
dcat_test(test_registry    tests/test_registry.c)
dcat_test(test_cli         tests/test_cli.c)
dcat_test(test_executor    tests/test_executor_mock.c)
dcat_test(test_precheck    tests/test_precheck.c)
dcat_test(test_state       tests/test_state.c)
