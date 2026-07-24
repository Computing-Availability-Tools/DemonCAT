cmake_minimum_required(VERSION 3.10)
project(dcat C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # gnu11 扩展（fork/usleep 等）
find_package(Threads REQUIRED)

add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)

set(DCAT_CORE
    src/core/types.c src/core/cli.c src/core/help.c src/core/registry.c src/core/executor.c
    src/core/precheck.c src/core/state.c src/core/config.c
    src/core/output.c src/core/dispatch.c
    src/injectors/injectors.c src/plugins/plugin_manager.c)
add_executable(dcat src/main.c ${DCAT_CORE})
target_include_directories(dcat PRIVATE src src/core src/injectors src/plugins third_party/cjson)
target_link_libraries(dcat PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(dcat PRIVATE -Wall -Wextra -Werror)

enable_testing()
set(DCAT_TESTS test_types test_output test_config test_registry test_executor_mock
    test_precheck test_state test_injectors test_dispatch test_cli test_help test_faults test_plugin_manager test_plugin_integration)
foreach(t ${DCAT_TESTS})
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/${t}.c)
        add_executable(${t} tests/${t}.c ${DCAT_CORE})
        target_include_directories(${t} PRIVATE src src/core src/injectors src/plugins third_party/cjson tests)
        target_link_libraries(${t} PRIVATE cjson Threads::Threads ${CMAKE_DL_LIBS})
        target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
endforeach()

# 示例动态插件（编译到 plugins/libsample.so，自包含 output+types+cjson）
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
