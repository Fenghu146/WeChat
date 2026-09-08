cmake_minimum_required(VERSION 3.16)
project(im_platform_fh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 下源文件统一按 UTF-8 编译，避免中文注释/输出乱码
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 核心库：策略框架实现（其余类型均为头文件内联实现）
add_library(im_core_fh
    src/policy/abstract_group_policy_fh.cpp
    src/policy/qq_policy_fh.cpp
    src/policy/wechat_policy_fh.cpp)
target_include_directories(im_core_fh PUBLIC include)

# 控制台演示程序
add_executable(demo_fh app/main.cpp)
target_link_libraries(demo_fh PRIVATE im_core_fh)

# ============================================================
# 阶段 E：自动化测试（为真实项目迁移提供回归保障）
#   使用 tests/fh_mini_test.hpp 自包含断言框架 —— 离线课程环境
#   无需下载 GoogleTest；每个测试文件生成独立可执行程序，由 ctest
#   汇总执行（后续接入 GoogleTest 时仅需替换断言宏）。
#   运行：cmake --build build 后 ctest --test-dir build -C Debug
# ============================================================
enable_testing()
set(FH_TEST_TARGETS
    group_core_test
    abstract_policy_test
    qq_wechat_policy_test
    platform_service_test
    social_message_test)
foreach(tgt IN LISTS FH_TEST_TARGETS)
    add_executable(${tgt} tests/${tgt}.cpp)
    target_link_libraries(${tgt} PRIVATE im_core_fh)
    add_test(NAME ${tgt} COMMAND ${tgt})
endforeach()
