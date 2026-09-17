// ============================================================
// 控制台演示程序（作者代号：FH）
// 流程：
//   1) 默认直接进入手动测试工作台；
//   2) 带 --demo 参数时先自动跑完 A→D 全部演示场景，再进入工作台。
// 编译：cmake --build build 后运行 demo_fh。
// ============================================================
#include <iostream>
#include <string>

#include "demo_runner.hpp"
#include "client_ui.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Windows 控制台按 UTF-8 输出，避免中文乱码
void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    initConsole();
    const bool demoMode =
        argc > 1 && (std::string(argv[1]) == "--demo" ||
                     std::string(argv[1]) == "demo");
    std::cout << "==== 模拟即时通信平台：QQ 群 / 微信群管理（作者代号 FH） ====\n";
    if (demoMode) {
        DemoRunner::runAllDemoScenarios();
        std::cout << "\n自动演示结束，进入手动测试工作台。\n";
    } else {
        std::cout << "（提示：带参数 --demo 可先观看 A→D 自动演示）\n";
    }
    return fh_client::runClientUi();
}
