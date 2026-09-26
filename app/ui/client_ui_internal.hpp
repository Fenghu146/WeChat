#pragma once
// ============================================================
// 手动测试工作台 · 内部共享头（作者代号：FH）
// ------------------------------------------------------------
// 由原 app/client_ui.cpp 单文件拆分而来：集中声明工作台各模块
// （终端基础组件 / 会话数据世界 / 各业务界面）共享的类型与函数，
// 使各 .cpp 只保留各自的界面实现。
//
// 说明：所有类名 / 函数名与实现逻辑保持不变，仅从匿名命名空间
// 迁入 fh_client 命名空间，以支持跨编译单元共享同一份数据世界。
// ============================================================
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_membership_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/platform/activation_manager_fh.hpp"
#include "im/platform/login_manager_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"
#include "im/social/discussion_group_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_registry_fh.hpp"

#include "client_ui.hpp"
#include "ui_screen.hpp"

namespace fh_client {

using ProfilePtr = std::shared_ptr<UserProfileFH>;
using UserPtr = std::shared_ptr<UserFH>;

// ============================================================
// 会话数据世界：一次启动内数据持久，可反复测试
// ============================================================

struct LocalSlot {
    PlatformKindFH platform = PlatformKindFH::QQ;  // 当前管理模式（策略）
    std::shared_ptr<GroupFH> group;
};

struct App {
    UserRegistryFH registry;
    std::vector<ProfilePtr> people;          // 全部自然人（含新注册）
    ActivationManagerFH activation;
    LoginManagerFH login;
    FriendRegistryFH friends;
    GroupRegistryFH official;                // 官方群目录 + 自建官方群
    std::vector<LocalSlot> locals;           // 本地正式群（聚合根，阶段 A）
    std::vector<std::shared_ptr<DiscussionGroupFH>> discs;  // QQ 临时讨论组
    std::unordered_map<std::string, UserPtr> actorPool;     // 平台账号 -> UserFH

    ProfilePtr me;            // 当前操作的自然人
    std::string notice;       // 底部提示条（最近一次操作结果）
    unsigned msgSeq = 0;      // 本地正式群消息自增号
    int localSeq = 0;         // 本地正式群自增号
    int year = 2026;          // 演示用当前年份（计算 T 龄）
};
extern App g;

// 长帮助页条目（测试指引 / 操作说明共用）
struct HelpEntry {
    bool isSection;
    std::string text;
};

// ------------------------------------------------------------
// 终端基础组件（client_ui_terminal.cpp）
// ------------------------------------------------------------
void initUiConsole();
char waitKey();
std::optional<std::string> askText(const std::string& prompt);
long askNum(const std::string& prompt, long lo, long hi);
void showPagedHelp(const std::string& title, const std::vector<HelpEntry>& entries);
void busy(const std::string& what);
std::string fmtClock(std::chrono::system_clock::time_point tp, bool withDate = false);

// ------------------------------------------------------------
// 数据世界与共用小工具（client_ui_state.cpp）
// ------------------------------------------------------------
void noticeOK(const std::string& msg);
void noticeFail(const std::string& msg);
void noticeInfo(const std::string& msg);
void present(fh_ui::Screen& s, const std::string& prompt = "请按键选择：");
int chooseByLabels(const std::string& title, const std::vector<std::string>& labels,
                   const std::string& hint = std::string());
std::string platCn(PlatformKindFH p);
const char* zhRole(GroupRoleFH role);
UserPtr actorFor(const ProfilePtr& p, PlatformKindFH platform);
std::string nickOf(PlatformKindFH platform, const std::string& accountId);
void seedWorld();
std::string friendMark(const ProfilePtr& p, PlatformKindFH pl);
ProfilePtr pickProfile(bool excludeSelf, std::optional<PlatformKindFH> needAcct,
                       const std::string& title,
                       const std::function<std::string(const ProfilePtr&)>& annotate = {});
void drawAccountCard(fh_ui::Screen& s);
// 正式群“操作身份”解析（client_ui_state.cpp）：
// 切换管理模式只换群策略、成员仍以入群时的平台账号记录（任务书 6.(4)），
// 因此解析身份时允许回退到本人“任一平台”在群内的成员身份。
UserPtr localActorFor(const ProfilePtr& p, const LocalSlot& slot);
// 由成员实体（任一平台的账号）反查其自然人档案
ProfilePtr profileOfMember(const UserPtr& u);

// ------------------------------------------------------------
// 各业务界面入口（供主工作台与相互跳转调用）
// ------------------------------------------------------------
void runLocalChat(const LocalSlot& slot);          // client_ui_local_chat.cpp
void runOfficialChat(std::string groupId);          // client_ui_official_chat.cpp
void runDiscChat(std::size_t index);                // client_ui_conversations.cpp
void runConversationList();                         // client_ui_conversations.cpp
void runHall();                                     // client_ui_hall.cpp
void runServiceCenter();                            // client_ui_accounts.cpp
void runContacts();                                 // client_ui_contacts.cpp
void runCreateScreen();                             // client_ui_create.cpp
bool runAccountGate();                              // client_ui_accounts.cpp
bool runWorkspace();                                // client_ui_workspace.cpp

}  // namespace fh_client
