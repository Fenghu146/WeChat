#pragma once
// ============================================================
// DemoRunner —— 自动演示运行器
// 将main.cpp中的自动演示逻辑拆分到独立模块
// ============================================================
#include <string>

namespace DemoRunner {

// 运行所有自动演示场景
void runAllDemoScenarios();

// 运行QQ群vs微信群核心流程演示
void runAutoScenario();

// 运行多产品账号/开通/登录联动演示
void runAutoPlatformScenario();

// 运行好友/群注册表/QQ临时讨论组演示
void runAutoSocialScenario();

// 运行消息平台差异演示
void runAutoMessageScenario();

} // namespace DemoRunner
