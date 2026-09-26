cmake_minimum_required(VERSION 3.16)
# 在 project() 之前尝试定位系统 Ninja，绕过 CLion 内置 Ninja 路径失效问题
find_program(CLION_FIX_NINJA NAMES ninja PATHS /opt/homebrew/bin /usr/local/bin /usr/bin)
if(CLION_FIX_NINJA)
    set(CMAKE_MAKE_PROGRAM "${CLION_FIX_NINJA}" CACHE FILEPATH "Ninja executable" FORCE)
else()
    message(WARNING "未找到系统 Ninja，请执行 brew install ninja 后重试；或在 CLion Settings > Build > Toolchain 中将 Ninja 路径改为 Homebrew 安装位置")
endif()
project(im_platform_fh LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 下源文件统一按 UTF-8 编译，避免中文注释/输出乱码
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 核心库：领域模型 / 社交注册表 / 策略框架的实现源文件
#（其余类型为头文件内联实现）
add_library(im_core_fh
    src/model/group_fh.cpp
    src/social/group_registry_fh.cpp
    src/social/friend_registry_fh.cpp
    src/policy/abstract_group_policy_fh.cpp
    src/policy/qq_policy_fh.cpp
    src/policy/wechat_policy_fh.cpp)
target_include_directories(im_core_fh PUBLIC include)

# 控制台演示程序：main 负责自动演示(--demo)入口，app/ui/ 实现终端界面子系统
#   client_ui_* 为手动测试工作台（按业务界面拆分）；demo_runner 为自动演示；
#   ui_screen 提供共用的终端排版能力（标题栏/键值卡片/菜单列/整帧重绘）
set(FH_UI_SOURCES
    app/ui/ui_screen.cpp
    app/ui/demo_runner.cpp
    app/ui/client_ui_terminal.cpp
    app/ui/client_ui_state.cpp
    app/ui/client_ui_local_chat.cpp
    app/ui/client_ui_official_chat.cpp
    app/ui/client_ui_conversations.cpp
    app/ui/client_ui_hall.cpp
    app/ui/client_ui_accounts.cpp
    app/ui/client_ui_contacts.cpp
    app/ui/client_ui_create.cpp
    app/ui/client_ui_workspace.cpp)
add_executable(demo_fh app/main.cpp ${FH_UI_SOURCES})
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
    social_message_test
    regression_tests
    e2e_integration_test
    requirement_alignment_test
    requirement_alignment_comprehensive_test
    boundary_robustness_test
    demo_flow_smoke_test)
foreach(tgt IN LISTS FH_TEST_TARGETS)
    add_executable(${tgt} tests/${tgt}.cpp)
    target_link_libraries(${tgt} PRIVATE im_core_fh)
    add_test(NAME ${tgt} COMMAND ${tgt})
endforeach()

# 冒烟用例要执行真实的演示程序，先保证其已构建
add_dependencies(demo_flow_smoke_test demo_fh)
set_tests_properties(demo_flow_smoke_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
