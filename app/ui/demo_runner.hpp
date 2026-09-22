#pragma once
// ============================================================
// DemoRunner —— 自动演示运行器
// ------------------------------------------------------------
// - 支持整体演示（A→D）与分段演示（--demo=A|B|C|D）；
// - 交互终端下每段之间等待回车，便于答辩时边讲边停；
// - 演示中的每处校验都会计入自检汇总，结尾由 printDemoSummary 输出。
// ============================================================
#include <string>

namespace DemoRunner {

// 演示自检统计
struct DemoSummary {
    int total = 0;           // 校验项总数
    int success = 0;         // 实际返回成功
    int failure = 0;         // 实际返回失败
    int expectedFail = 0;    // 标注「应失败」的平台规则/权限校验项
    int unexpectedFail = 0;  // 应成功却失败
    int unexpectedPass = 0;  // 应失败却成功
};

// 运行所有自动演示场景（A→D；交互终端下每段之间等待回车）
void runAllDemoScenarios();

// 只运行指定阶段：'A' 核心流程 / 'B' 多产品 / 'C' 社交 / 'D' 消息
// 返回 false 表示阶段字母无效
bool runDemoScenario(char stage);

// 打印演示自检汇总，返回「与预期不符」的项数（0 表示全部符合预期）
int printDemoSummary();

// 运行QQ群vs微信群核心流程演示
void runAutoScenario();

// 运行多产品账号/开通/登录联动演示
void runAutoPlatformScenario();

// 运行好友/群注册表/QQ临时讨论组演示
void runAutoSocialScenario();

// 运行消息平台差异演示
void runAutoMessageScenario();

} // namespace DemoRunner
