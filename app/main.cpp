// ============================================================
// 控制台演示程序（作者代号：FH）
// 流程：
//   1) 默认直接进入手动测试工作台；
//   2) --demo 先自动跑完 A→D 全部演示场景（交互终端下每段之间等待回车），再进入工作台；
//   3) --demo=A|B|C|D 只运行指定阶段，便于答辩时按需演示单段。
// 演示结束会输出自检汇总：校验项总数、其中「应失败」项，以及是否存在与预期不符项。
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
    const std::string arg = argc > 1 ? argv[1] : "";
    const bool demoAll = (arg == "--demo" || arg == "demo");
    const bool demoOne = arg.rfind("--demo=", 0) == 0 && arg.size() > 7;

    std::cout << "==== 模拟即时通信平台：QQ 群 / 微信群管理（作者代号 FH） ====\n";
    if (demoAll) {
        DemoRunner::runAllDemoScenarios();
        DemoRunner::printDemoSummary();
        std::cout << "\n自动演示结束，进入手动测试工作台。\n";
    } else if (demoOne) {
        if (!DemoRunner::runDemoScenario(arg[7])) {
            std::cout << "未知演示阶段「" << arg.substr(7)
                      << "」：可用 A（核心流程）/ B（多产品）/ C（社交）/ D（消息）。\n";
            return 2;
        }
        DemoRunner::printDemoSummary();
        std::cout << "\n该段演示结束，进入手动测试工作台。\n";
    } else {
        std::cout << "（提示：--demo 观看 A→D 全部演示，--demo=A|B|C|D 只演示其中一段）\n";
    }
    return fh_client::runClientUi();
}
