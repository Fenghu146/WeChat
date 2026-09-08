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

# 测试（阶段 E 接入 GoogleTest；离线环境可用 <cassert> 简易测试代替）
# enable_testing()
# add_executable(im_tests_fh tests/xxx.cpp)
# target_link_libraries(im_tests_fh PRIVATE im_core_fh)
