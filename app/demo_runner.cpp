// ============================================================
// DemoRunner —— 自动演示运行器实现
// ============================================================
#include "demo_runner.hpp"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/platform/account_info_fh.hpp"
#include "im/platform/activation_manager_fh.hpp"
#include "im/platform/login_manager_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"
#include "im/social/discussion_group_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_chat_record_fh.hpp"
#include "im/social/group_registry_fh.hpp"

#include "ui_screen.hpp"

namespace DemoRunner {
namespace {

unsigned g_messageSeq = 0;

// ---------- 演示自检与呈现 ----------
// 着色约定：绿色 = 符合预期，红色加粗 = 与预期不符（演示里的失败大多是被
// 平台规则/权限刻意拒绝的「应失败」项，按成败着色会把真正的异常淹没）。

DemoSummary g_summary;
bool g_color = false;
bool g_interactive = false;

const char* kColorOk = "\033[32m";
const char* kColorBad = "\033[1;31m";

std::string paint(const std::string& text, const char* code) {
    if (!g_color) return text;
    return std::string(code) + text + "\033[0m";
}

// 期望成功的校验（绝大多数业务操作）
std::string ok(bool value) {
    ++g_summary.total;
    if (value) {
        ++g_summary.success;
        return paint("成功", kColorOk);
    }
    ++g_summary.failure;
    ++g_summary.unexpectedFail;  // 应成功却失败
    return paint("失败", kColorBad);
}

// 期望失败的校验（平台规则 / 权限要求必须拒绝，输出中标注「应失败」）
std::string okDenied(bool value) {
    ++g_summary.total;
    ++g_summary.expectedFail;
    if (!value) {
        ++g_summary.failure;
        return paint("失败", kColorOk);
    }
    ++g_summary.success;
    ++g_summary.unexpectedPass;  // 应失败却成功
    return paint("成功", kColorBad);
}

bool stdoutIsTty() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool stdinIsTty() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

// 颜色开关：NO_COLOR 关闭；FH_DEMO_COLOR=1 强制开启；否则按输出是否为终端判断
void initDemoOutput() {
    if (std::getenv("NO_COLOR") != nullptr) {
        g_color = false;
    } else if (const char* force = std::getenv("FH_DEMO_COLOR"); force && *force == '1') {
        g_color = true;
    } else {
        g_color = stdoutIsTty();
    }
#ifdef _WIN32
    if (g_color) {  // Windows 10+ 需显式开启 VT 序列处理
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode) ||
            !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            g_color = false;
        }
    }
#endif
    g_interactive = stdinIsTty();

    if (g_color)
        std::cout << "（颜色说明：" << paint("绿色", kColorOk) << " = 符合预期，"
                  << paint("红色", kColorBad) << " = 与预期不符）\n";
}

// 分段演示之间的暂停：仅在交互终端下等待回车，管道/重定向环境直接跳过
void pauseBetweenScenarios() {
    if (!g_interactive) return;
    std::cout << "\n  —— 回车继续下一段演示 ——";
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    if (!std::cin) std::cin.clear();
}

// 生成带自增 ID 的消息
std::shared_ptr<MessageFH> makeMessage(const std::shared_ptr<UserFH>& sender,
                                       const std::string& content) {
    return std::make_shared<MessageFH>("m" + std::to_string(++g_messageSeq),
                                       sender, content);
}

// 将时间点格式化为 "MM-DD HH:MM:SS"
std::string formatClock(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", &local);
    return buf;
}

void printMembers(const std::string& title, const GroupFH& group) {
    std::cout << "  "
              << fh_ui::section(title + " · " + group.getName() + "（群号 " +
                                std::to_string(group.getGroupNumber()) + "），成员 " +
                                std::to_string(group.members().size()) + " 人")
              << "\n";
    fh_ui::Rows rows;
    for (const auto& kv : group.members()) {
        const auto& m = kv.second;
        rows.add(" " + m.getUser()->getNickname() + "（ID " +
                     m.getUser()->getId() + "）",
                 std::string(toZhName(m.getRole())) + "，加入 " +
                     formatClock(m.getJoinedAt()) +
                     (m.isMuted() ? " ［被禁言］" : ""));
    }
    rows.flush(std::cout, 5);
}

void printMessages(const std::string& title, const GroupFH& group) {
    std::cout << "  "
              << fh_ui::section(title + " · 消息记录 " +
                                std::to_string(group.messages().size()) + " 条")
              << "\n";
    fh_ui::Rows rows;
    for (const auto& msg : group.messages())
        rows.add(" " + msg->getSender()->getNickname(),
                 msg->getContent() + (msg->isRecalled() ? " ［已撤回］" : ""));
    rows.flush(std::cout, 5);
}

// 统一小节标题：与手动工作台共用同一套设计语言（—— 小节 ——）
void step(const std::string& id, const std::string& text) {
    std::cout << "\n" << fh_ui::section(id + " " + text) << "\n";
}

// 打印自然人的当前在线服务列表
void printOnlinePlatforms(const char* who, const std::set<PlatformKindFH>& platforms) {
    std::cout << "     " << who << " 当前在线服务：";
    if (platforms.empty()) {
        std::cout << "（无）\n";
        return;
    }
    bool first = true;
    for (PlatformKindFH p : platforms) {
        std::cout << (first ? "" : " / ") << toZhName(p);
        first = false;
    }
    std::cout << "\n";
}

// 将时间点转换为文本格式（群消息记录用）
std::string toTimeText(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &local);
    return std::string(buf);
}

} // namespace

void runAutoScenario() {
    std::cout << "\n" << fh_ui::rule("自动演示：QQ 群 vs 微信群（核心流程）") << "\n";

    auto owner    = std::make_shared<UserFH>("10001", "群主本人");
    auto admin    = std::make_shared<UserFH>("10002", "小管理");
    auto member   = std::make_shared<UserFH>("10003", "普通成员");
    auto outsider = std::make_shared<UserFH>("10004", "路人甲");

    // 同样的用户、同样的配置，只是群策略不同
    auto qqPolicy = std::make_shared<QQPolicyFH>();
    GroupFH qqGroup("g-qq-001", 1001, "C++ 学习群",
                    GroupConfigFH(50, true, false, std::chrono::seconds(120)),
                    qqPolicy, owner);
    GroupFH wxGroup("g-wx-002", 1002, "周末爬山群",
                    GroupConfigFH(50, true, false, std::chrono::seconds(120)),
                    std::make_shared<WeChatPolicyFH>(), owner);

    step("[1]", "群主邀请成员入群，并分别任命管理员");
    {
        // 逐条求值再用结果拼串：一条表达式里放两次状态调用会因求值顺序未定义而错序
        const std::string qqInviteAdmin = ok(qqGroup.inviteMember(owner, admin));
        const std::string qqInviteMember = ok(qqGroup.inviteMember(owner, member));
        const std::string wxInviteAdmin = ok(wxGroup.inviteMember(owner, admin));
        const std::string wxInviteMember = ok(wxGroup.inviteMember(owner, member));
        fh_ui::Rows rows;
        rows.add("QQ 邀请小管理 / 普通成员", qqInviteAdmin + " / " + qqInviteMember);
        rows.add("微信邀请小管理 / 普通成员", wxInviteAdmin + " / " + wxInviteMember);
        rows.add("QQ 任命小管理为管理员", ok(qqGroup.setAdmin(owner, admin, true)));
        rows.add("微信任命小管理为管理员", ok(wxGroup.setAdmin(owner, admin, true)));
        rows.flush(std::cout, 5);
    }

    step("[2]", "邀请规则差异");
    {
        fh_ui::Rows rows;
        rows.add("QQ 普通成员邀请路人甲（开关开启）",
                 ok(qqGroup.inviteMember(member, outsider)));
        rows.add("微信普通成员邀请路人甲",
                 okDenied(wxGroup.inviteMember(member, outsider)),
                 "（应失败，微信群仅群主可推荐加入）");
        rows.add("微信管理员邀请路人甲",
                 okDenied(wxGroup.inviteMember(admin, outsider)),
                 "（应失败，同上；微信管理员不产生特权）");
        rows.add("微信群主邀请路人甲",
                 ok(wxGroup.inviteMember(owner, outsider)), "（仅群主可邀请）");
        rows.add("路人甲再次加入 QQ 群（成员唯一）",
                 okDenied(qqGroup.inviteMember(owner, outsider)), "（应失败）");
        rows.flush(std::cout, 5);
    }

    step("[3]", "全员禁言设置差异");
    {
        fh_ui::Rows rows;
        rows.add("QQ 管理员设置全员禁言", ok(qqGroup.setAllMute(admin, true)));
        rows.add("微信管理员设置全员禁言",
                 okDenied(wxGroup.setAllMute(admin, true)), "（应失败，仅群主）");
        rows.add("微信群主设置全员禁言", ok(wxGroup.setAllMute(owner, true)));
        rows.flush(std::cout, 5);
    }

    step("[4]", "全员禁言下的发言限制");
    {
        fh_ui::Rows rows;
        rows.add("QQ 普通成员发言",
                 okDenied(qqGroup.sendMessage(member,
                                              makeMessage(member, "禁言期间的发言"))),
                 "（应失败）");
        rows.add("QQ 群主发言",
                 ok(qqGroup.sendMessage(owner,
                                        makeMessage(owner, "群主不受禁言影响"))));
        rows.flush(std::cout, 5);
        qqGroup.setAllMute(owner, false);
        wxGroup.setAllMute(owner, false);
        std::cout << "     （两群随后由群主解除全员禁言）\n";
    }

    step("[5]", "动态切换群管理模式（官方：成员数据不受伤害）");
    {
        // 切换与随后的禁言必须严格先后执行，故分步求值
        const std::string switchedToWx =
            ok(qqGroup.switchPolicy(std::make_shared<WeChatPolicyFH>()));
        const std::string wxAdminMute = okDenied(qqGroup.setAllMute(admin, true));
        const std::string switchedToQq = ok(qqGroup.switchPolicy(qqPolicy));
        const std::string qqAdminMute = ok(qqGroup.setAllMute(admin, true));
        qqGroup.setAllMute(owner, false);

        fh_ui::Rows rows;
        rows.add("QQ 群切到“微信模式”后，管理员设全员禁言",
                 switchedToWx + "，执行结果：" + wxAdminMute, "（应失败）");
        rows.add("QQ 群切回“QQ 模式”后，管理员设全员禁言",
                 switchedToQq + "，执行结果：" + qqAdminMute, "（应成功）");
        rows.add("切换前后群成员数不变",
                 std::to_string(qqGroup.members().size()) + " 人",
                 "（成员数据未受伤害）");
        rows.flush(std::cout, 5);
    }

    step("[6]", "撤回消息（归属 + 时间窗）");
    auto msgByMember = makeMessage(member, "大家好呀");
    auto msgByOwner = makeMessage(owner, "这是群主的消息");
    qqGroup.sendMessage(member, msgByMember);
    qqGroup.sendMessage(owner, msgByOwner);
    {
        fh_ui::Rows rows;
        rows.add("普通成员撤回自己的消息",
                 ok(qqGroup.recallMessage(member, msgByMember->getId())));
        rows.add("普通成员撤回群主的消息",
                 okDenied(qqGroup.recallMessage(member, msgByOwner->getId())),
                 "（应失败，只能撤回本人消息）");
        rows.add("群主撤回自己的消息",
                 ok(qqGroup.recallMessage(owner, msgByOwner->getId())));

        // 时间窗边界：构造“发送时刻早于窗口”的消息复现超时，无需真实等待
        const long long limitSec =
            static_cast<long long>(qqGroup.getConfig().getRecallTimeLimit().count());
        auto staleMsg = std::make_shared<MessageFH>(
            "m-stale", owner, "很久以前的消息",
            std::chrono::system_clock::now() - std::chrono::seconds(limitSec + 1));
        qqGroup.sendMessage(owner, staleMsg);
        rows.add("群主撤回 " + std::to_string(limitSec + 1) + " 秒前发送的消息",
                 okDenied(qqGroup.recallMessage(owner, staleMsg->getId())),
                 "（应失败：超出 " + std::to_string(limitSec) + " 秒撤回窗口）");
        auto freshMsg = makeMessage(owner, "刚刚发送的消息");
        qqGroup.sendMessage(owner, freshMsg);
        rows.add("群主撤回刚发送的消息",
                 ok(qqGroup.recallMessage(owner, freshMsg->getId())),
                 "（应成功：在 " + std::to_string(limitSec) + " 秒窗口内）");
        rows.flush(std::cout, 5);
    }

    step("[7]", "任命管理员与转让群主");
    {
        fh_ui::Rows rows;
        rows.add("群主任命普通成员为管理员",
                 ok(qqGroup.setAdmin(owner, member, true)));
        rows.add("普通成员（已管理员）无权再任命别人",
                 okDenied(qqGroup.setAdmin(member, outsider, true)), "（应失败）");
        rows.add("群主将群转让给原管理员",
                 ok(qqGroup.transferOwner(owner, admin)));
        rows.flush(std::cout, 5);
    }

    std::cout << "\n";
    printMembers("QQ 群", qqGroup);
    printMembers("微信群", wxGroup);
    printMessages("QQ 群", qqGroup);
    std::cout << "\n" << fh_ui::rule("阶段 A 自动演示结束") << "\n";
}

void runAutoPlatformScenario() {
    std::cout << "\n"
              << fh_ui::rule("自动演示：多产品账号 / 开通 / 登录（任务书 1·4·5）") << "\n";
    UserRegistryFH registry;
    ActivationManagerFH activation;
    LoginManagerFH login;
    constexpr int CURRENT_YEAR = 2026;

    auto xiaoming = registry.registerUser("10001", "小明", "2000-06-01", "广东·深圳", 2018);
    auto xiaohong = registry.registerUser("10002", "小红", "1999-11-11", "湖南·长沙", 2016);

    step("[B1]", "号码体系：QQ 与微博共享 ID，微信独立可绑定 QQ");
    {
        auto qq = registry.makeAccount(*xiaoming, PlatformKindFH::QQ);
        auto wb = registry.makeAccount(*xiaoming, PlatformKindFH::Weibo);
        fh_ui::Rows rows;
        rows.add(xiaoming->getNickname() + " 的 QQ 号", qq.getAccountId(),
                 "（昵称 " + qq.getNickname() + "，所在地 " + qq.getLocation() +
                     "，T 龄 " + std::to_string(qq.tAge(CURRENT_YEAR)) + " 年）");
        rows.add("微博号", wb.getAccountId(), "（与 QQ 号码相同）");
        rows.flush(std::cout, 5);
    }

    step("[B2]", "自选开通微X 服务（ActivationManagerFH）");
    {
        fh_ui::Rows rows;
        rows.add("开通 QQ", ok(activation.activate(*xiaoming, PlatformKindFH::QQ)));
        rows.add("开通微博（QQ 同号即具资格）",
                 ok(activation.activate(*xiaoming, PlatformKindFH::Weibo)));
        rows.add("未绑定微信号就开通微信",
                 okDenied(activation.activate(*xiaoming, PlatformKindFH::WeChat)),
                 "（应失败）");
        rows.add("为小明绑定微信号 wx-88-0001",
                 ok(registry.bindWeChat(xiaoming, "wx-88-0001")));
        rows.add("绑定后开通微信",
                 ok(activation.activate(*xiaoming, PlatformKindFH::WeChat)));
        rows.add("重复开通 QQ（幂等）",
                 okDenied(activation.activate(*xiaoming, PlatformKindFH::QQ)),
                 "（应失败）");
        rows.flush(std::cout, 5);
        std::cout << "     " << xiaoming->getNickname() << " 已开通：";
        for (PlatformKindFH p : xiaoming->activatedPlatforms())
            std::cout << toZhName(p) << " ";
        std::cout << "（共 " << activation.activatedCount(*xiaoming) << " 个微X 服务）\n";
    }

    step("[B3]", "微信账号与 QQ 绑定关系（AccountInfoFH）");
    {
        auto wx = registry.makeAccount(*xiaoming, PlatformKindFH::WeChat);
        fh_ui::Rows rows;
        rows.add("微信号", wx.getAccountId());
        rows.add("绑定 QQ", wx.hasBindQQ() ? wx.getBindQqId() : std::string("无"));
        rows.flush(std::cout, 5);
    }

    step("[B4]", "登录 + 简单确认联动（任务书第5点）");
    {
        fh_ui::Rows rows;
        rows.add("小红尚未开通服务，登录 QQ",
                 okDenied(login.login(*xiaohong, PlatformKindFH::QQ)), "（应失败）");
        rows.add("小明登录 QQ（仅目标服务上线）",
                 ok(login.login(*xiaoming, PlatformKindFH::QQ)));
        rows.add("确认前微信是否在线",
                 okDenied(login.isOnline(*xiaoming, PlatformKindFH::WeChat)),
                 "（确认前不应上线）");
        const int newly = login.confirmLink(*xiaoming);
        rows.add("简单确认：自动登录其余已开通服务",
                 ok(newly == 2) + "（新上线 " + std::to_string(newly) + " 个）");
        rows.flush(std::cout, 5);
    }
    printOnlinePlatforms("小明", login.onlinePlatforms(*xiaoming));

    step("[B5]", "单服务退出与取消开通规则");
    {
        fh_ui::Rows rows;
        rows.add("在线状态直接取消开通微博",
                 okDenied(activation.deactivate(*xiaoming, PlatformKindFH::Weibo)),
                 "（应失败，须先退出登录）");
        rows.add("退出微博登录", ok(login.logout(*xiaoming, PlatformKindFH::Weibo)));
        rows.flush(std::cout, 5);
    }
    printOnlinePlatforms("小明", login.onlinePlatforms(*xiaoming));
    {
        fh_ui::Rows rows;
        rows.add("退出微博后取消开通微博",
                 ok(activation.deactivate(*xiaoming, PlatformKindFH::Weibo)));
        rows.flush(std::cout, 5);
    }
    std::cout << "     剩余开通服务：";
    for (PlatformKindFH p : xiaoming->activatedPlatforms()) std::cout << toZhName(p) << " ";
    std::cout << "\n" << fh_ui::rule("阶段 B 自动演示结束") << "\n";
}

void runAutoSocialScenario() {
    std::cout << "\n"
              << fh_ui::rule("自动演示：好友 / 群注册表 / QQ 临时讨论组（阶段 C）") << "\n";

    UserRegistryFH registry;
    auto xiaoming = registry.registerUser("10001", "小明", "2000-06-01", "广东·深圳", 2018);
    auto xiaohong = registry.registerUser("10002", "小红", "1999-11-11", "湖南·长沙", 2016);
    auto lurenB = registry.registerUser("10003", "路人乙", "2001-03-03", "四川·成都", 2020);

    registry.bindWeChat(xiaoming, "wx-88-0001");
    registry.bindWeChat(xiaohong, "wx-88-0002");

    FriendRegistryFH friends;
    GroupRegistryFH groups;

    step("[C1]", "好友按平台隔离（QQ/微信双向，微博单向关注）");
    {
        fh_ui::Rows rows;
        rows.add("小明添加小红为 QQ 好友",
                 ok(friends.makeFriends(*xiaoming, *xiaohong, PlatformKindFH::QQ)));
        rows.add("小明添加小红为微信好友",
                 ok(friends.makeFriends(*xiaoming, *xiaohong, PlatformKindFH::WeChat)));
        rows.add("小明关注路人乙（微博单向）", ok(friends.follow(*xiaoming, *lurenB)));
        rows.add("QQ 好友关系不影响微博",
                 okDenied(friends.isFriend(*xiaoming, *xiaohong, PlatformKindFH::Weibo)),
                 "（应失败）");
        rows.flush(std::cout, 5);
    }

    step("[C2]", "共同好友查询");
    {
        fh_ui::Rows rows;
        rows.add("小明和小红的 QQ 共同好友数量",
                 std::to_string(friends.commonFriends(*xiaoming, *xiaohong,
                                                      PlatformKindFH::QQ).size()) + " 人");
        rows.add("小明和小红的微博共同关注数量",
                 std::to_string(friends.commonFriends(*xiaoming, *xiaohong,
                                                      PlatformKindFH::Weibo).size()) + " 人");
        rows.flush(std::cout, 5);
    }

    step("[C3]", "跨服务推荐添加好友（任务书 2.(2)、6.(3)）");
    // 前置条件：本人已开通来源与目标服务，对方须有目标平台账号
    ActivationManagerFH activation;
    activation.activate(*xiaoming, PlatformKindFH::QQ);
    activation.activate(*xiaoming, PlatformKindFH::WeChat);
    registry.bindWeChat(lurenB, "wx-88-0003");
    activation.activate(*lurenB, PlatformKindFH::WeChat);
    friends.makeFriends(*xiaoming, *lurenB, PlatformKindFH::QQ);
    std::cout << "     小明已开通 QQ/微信；路人乙已绑定微信，且与小明是 QQ 好友\n";
    {
        fh_ui::Rows rows;
        rows.add("QQ → 微信 可推荐人数",
                 std::to_string(friends.recommendFriendsFrom(*xiaoming, registry,
                                                             PlatformKindFH::QQ,
                                                             PlatformKindFH::WeChat).size()) +
                     " 人");
        rows.add("小明依 QQ 好友推荐添加路人乙为微信好友",
                 ok(friends.addFriendFromRecommendation(*xiaoming, *lurenB,
                                                        PlatformKindFH::QQ,
                                                        PlatformKindFH::WeChat)));
        rows.add("重复推荐添加（已是微信好友）",
                 okDenied(friends.addFriendFromRecommendation(*xiaoming, *lurenB,
                                                              PlatformKindFH::QQ,
                                                              PlatformKindFH::WeChat)),
                 "（应失败：已是好友）");
        rows.flush(std::cout, 5);
    }

    step("[C4]", "群注册表与预置官方群");
    {
        fh_ui::Rows rows;
        rows.add("小明申请加入 QQ 群 1001",
                 ok(groups.joinGroup(*xiaoming, PlatformKindFH::QQ, "1001")));
        rows.add("小明申请加入微信群 1003",
                 okDenied(groups.joinGroup(*xiaoming, PlatformKindFH::WeChat, "1003")),
                 "（应失败，微信群只能推荐加入）");
        rows.add("小明自建微信群",
                 ok(groups.createGroup(*xiaoming, PlatformKindFH::WeChat, "家人群")));
        rows.flush(std::cout, 5);
    }

    step("[C5]", "QQ 临时讨论组");
    {
        DiscussionGroupFH dg("dg-001", "临时讨论组",
                             xiaoming->platformAccountId(PlatformKindFH::QQ));
        fh_ui::Rows rows;
        rows.add("小明创建讨论组并邀请小红", ok(dg.invite(*xiaoming, *xiaohong)));
        rows.add("小红自由退组", ok(dg.quit(*xiaohong)));
        rows.add("小明解散讨论组", ok(dg.disband(*xiaoming)));
        rows.flush(std::cout, 5);
    }

    step("[C6]", "断电保存（任务书 6.(1)/优化(2)：析构写回 → 启动加载）");
    {
        const std::string actFile = "demo_save_activation.dat";
        const std::string friendFile = "demo_save_friends.dat";
        const std::string groupFile = "demo_save_groups.dat";
        {
            // 进程一：造数据，容器析构时写回文件
            UserRegistryFH regA;
            auto a = regA.registerUser("90001", "演示甲", "2000-01-01", "北京", 2018);
            auto b = regA.registerUser("90002", "演示乙", "2000-02-02", "上海", 2019);
            regA.bindWeChat(a, "wx-90001");
            regA.bindWeChat(b, "wx-90002");
            regA.setActivationPath(actFile);
            ActivationManagerFH actA;
            actA.activate(*a, PlatformKindFH::QQ);
            actA.activate(*a, PlatformKindFH::WeChat);
            FriendRegistryFH friA;
            friA.setPersistencePath(friendFile);
            friA.makeFriends(*a, *b, PlatformKindFH::QQ);
            GroupRegistryFH grpA;
            grpA.setPersistencePath(groupFile);
            grpA.createGroup(*a, PlatformKindFH::QQ, "保存演示群");
            std::cout << "     进程一：开通 2 项 / 好友 1 对 / 自建群 1 个 → 析构写回文件\n";
        }
        {
            // 进程二：全新容器从文件读入（模拟系统重启加载）
            UserRegistryFH regB;
            auto a = regB.registerUser("90001", "演示甲", "2000-01-01", "北京", 2018);
            auto b = regB.registerUser("90002", "演示乙", "2000-02-02", "上海", 2019);
            regB.bindWeChat(a, "wx-90001");
            regB.bindWeChat(b, "wx-90002");
            regB.setActivationPath(actFile);
            FriendRegistryFH friB;
            friB.setPersistencePath(friendFile);
            GroupRegistryFH grpB;
            grpB.setPersistencePath(groupFile);
            const GroupInfoFH* saved = grpB.findGroup("1007");
            std::cout << "     进程二（模拟重启）：开通恢复 " << a->activatedPlatforms().size()
                      << " 项 / 好友恢复 " << ok(friB.isFriend(*a, *b, PlatformKindFH::QQ))
                      << " / 群恢复 " << ok(saved != nullptr) << "\n";
            if (saved) {
                fh_ui::Rows rows;
                rows.add("群 " + saved->groupId + "「" + saved->name + "」",
                         "群主 " + saved->ownerId + "，成员 " +
                             std::to_string(saved->memberIds.size()) + " 人");
                rows.flush(std::cout, 7);
            }
        }
        std::remove(actFile.c_str());
        std::remove(friendFile.c_str());
        std::remove(groupFile.c_str());
    }

    std::cout << "\n" << fh_ui::rule("阶段 C 自动演示结束") << "\n";
}

void runAutoMessageScenario() {
    std::cout << "\n"
              << fh_ui::rule("自动演示：群消息平台差异与群消息扩展（阶段 D）") << "\n";

    UserRegistryFH registry;
    auto xiaoming = registry.registerUser("10001", "小明", "2000-06-01", "广东·深圳", 2018);
    auto xiaohong = registry.registerUser("10002", "小红", "1999-11-11", "湖南·长沙", 2016);

    registry.bindWeChat(xiaoming, "wx-88-0001");
    registry.bindWeChat(xiaohong, "wx-88-0002");

    GroupRegistryFH groups;

    // 前置准备：成员先入群/建群，否则后续发送会因“非群成员”被拒
    step("[D0]", "消息演示前置：入群 / 建群");
    {
        const std::string mingQqJoin = ok(groups.joinGroup(*xiaoming, PlatformKindFH::QQ, "1001"));
        const std::string mingWbJoin = ok(groups.joinGroup(*xiaoming, PlatformKindFH::Weibo, "1005"));
        const std::string hongQqJoin = ok(groups.joinGroup(*xiaohong, PlatformKindFH::QQ, "1001"));
        const std::string hongWbJoin = ok(groups.joinGroup(*xiaohong, PlatformKindFH::Weibo, "1005"));
        fh_ui::Rows rows;
        rows.add("小明入群 QQ 1001 / 微博 1005（QQ/微博群可申请加入）",
                 mingQqJoin + " / " + mingWbJoin);
        rows.add("小明直接申请加入微信群 1003",
                 okDenied(groups.joinGroup(*xiaoming, PlatformKindFH::WeChat, "1003")),
                 "（应失败：微信群只能推荐加入）");
        rows.add("小明自建微信群“家人群”（群号 1007）",
                 ok(groups.createGroup(*xiaoming, PlatformKindFH::WeChat, "家人群")));
        rows.add("小红入群 QQ 1001 / 微博 1005", hongQqJoin + " / " + hongWbJoin);
        rows.flush(std::cout, 5);
    }

    step("[D1]", "消息类型能力差异");
    {
        fh_ui::Rows rows;
        rows.add("小明在 QQ 群 1001 发送文件",
                 ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::QQ, "1001",
                                            MessageKindFH::DOCUMENT, "架构图.pdf")));
        rows.add("小明在微信群 1007 发送文件",
                 okDenied(groups.sendGroupMessage(*xiaoming, PlatformKindFH::WeChat, "1007",
                                                  MessageKindFH::DOCUMENT, "合同.docx")),
                 "（应失败，微信群禁文件）");
        rows.add("小明在微信群 1007 发送图片",
                 ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::WeChat, "1007",
                                            MessageKindFH::IMAGE, "风景.jpg")),
                 "（微信群允许图片）");
        rows.add("小明在微博群 1005 发送图片",
                 okDenied(groups.sendGroupMessage(*xiaoming, PlatformKindFH::Weibo, "1005",
                                                  MessageKindFH::IMAGE, "风景.jpg")),
                 "（应失败，微博仅支持文本/表情）");
        rows.add("小明在微博群 1005 发送文本",
                 ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::Weibo, "1005",
                                            MessageKindFH::TEXT, "今晚一起讨论任务书")),
                 "（微博允许文本）");
        rows.flush(std::cout, 5);
    }

    step("[D2]", "引用回复能力差异");
    {
        fh_ui::Rows rows;
        rows.add("小红在 QQ 群引用回复",
                 ok(groups.sendGroupMessage(*xiaohong, PlatformKindFH::QQ, "1001",
                                            MessageKindFH::TEXT, "收到", true)));
        rows.add("小红在微博群引用回复",
                 okDenied(groups.sendGroupMessage(*xiaohong, PlatformKindFH::Weibo, "1005",
                                                  MessageKindFH::TEXT, "收到", true)),
                 "（应失败，微博不支持引用）");
        rows.flush(std::cout, 5);
    }

    step("[D3]", "文本长度限制");
    {
        fh_ui::Rows rows;
        rows.add("QQ 群文本上限",
                 std::to_string(PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::QQ)) +
                     " 字符");
        rows.add("微信群文本上限",
                 std::to_string(PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::WeChat)) +
                     " 字符");
        rows.add("微博群文本上限",
                 std::to_string(PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::Weibo)) +
                     " 字符");
        rows.flush(std::cout, 5);
    }

    step("[D4]", "聊天记录上限与淘汰");
    for (int i = 1; i <= 55; ++i) {
        groups.sendGroupMessage(*xiaoming, PlatformKindFH::QQ, "1001",
                                MessageKindFH::TEXT, "seq" + std::to_string(i));
    }
    {
        fh_ui::Rows rows;
        rows.add("QQ 群 1001 聊天记录数",
                 std::to_string(groups.chatOf("1001").size()) + " 条",
                 "（上限 " + std::to_string(GroupRegistryFH::kMaxChatRecordsFH) + " 条）");
        rows.flush(std::cout, 5);
    }

    step("[D5]", "消息视图呈现差异");
    std::cout << "     QQ 群 1001 聊天记录（QQ 视图）：\n";
    for (const GroupChatRecordFH& r : groups.chatOf("1001"))
        std::cout << "       · " << PlatformMessagePolicyFH::render(
                                        PlatformKindFH::QQ, r.senderNick,
                                        r.content, r.kind,
                                        toTimeText(r.sentAt))
                  << (r.isReply ? "（引用回复）" : "") << "\n";
    std::cout << "     微信群 1007 聊天记录（微信视图）：\n";
    for (const GroupChatRecordFH& r : groups.chatOf("1007"))
        std::cout << "       · " << PlatformMessagePolicyFH::render(
                                        PlatformKindFH::WeChat, r.senderNick,
                                        r.content, r.kind,
                                        toTimeText(r.sentAt))
                  << "\n";
    std::cout << "     微博群 1005 聊天记录（微博视图）：\n";
    for (const GroupChatRecordFH& r : groups.chatOf("1005"))
        std::cout << "       · " << PlatformMessagePolicyFH::render(
                                        PlatformKindFH::Weibo, r.senderNick,
                                        r.content, r.kind,
                                        toTimeText(r.sentAt))
                  << "\n";
    std::cout << "     同一条内容在不同产品下的视图形态示意（呈现层差异）：\n";
    const std::string sharedText = "今晚八点开会，记得@小红";
    for (PlatformKindFH p :
         {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo})
        std::cout << "       · " << PlatformMessagePolicyFH::render(
                                        p, "小明", sharedText,
                                        MessageKindFH::TEXT, "20:00:00")
                  << "\n";
    std::cout << "     （视图仅为示意；实际能否发送由 D1/D2 的平台规则决定）\n";
    std::cout << "\n" << fh_ui::rule("阶段 D 自动演示结束") << "\n";
}

// 只运行指定阶段，便于答辩时按需演示单段
bool runDemoScenario(char stage) {
    initDemoOutput();
    switch (std::toupper(static_cast<unsigned char>(stage))) {
        case 'A': runAutoScenario();         return true;
        case 'B': runAutoPlatformScenario(); return true;
        case 'C': runAutoSocialScenario();   return true;
        case 'D': runAutoMessageScenario();  return true;
        default:  return false;
    }
}

void runAllDemoScenarios() {
    initDemoOutput();
    runAutoScenario();
    pauseBetweenScenarios();
    runAutoPlatformScenario();
    pauseBetweenScenarios();
    runAutoSocialScenario();
    pauseBetweenScenarios();
    runAutoMessageScenario();
}

// 演示自检汇总：把每处校验的结果与预期对比，给出一个干净的结论
int printDemoSummary() {
    const int abnormal = g_summary.unexpectedFail + g_summary.unexpectedPass;
    std::cout << "\n" << fh_ui::rule("演示自检汇总") << "\n";
    std::cout << "  校验项 " << g_summary.total << " 项：成功 " << g_summary.success
              << "，失败 " << g_summary.failure
              << "（其中标注「应失败」的平台规则 / 权限校验 " << g_summary.expectedFail << " 项）\n";
    if (abnormal == 0) {
        std::cout << "  " << paint("自检结果：全部符合预期", kColorOk) << "\n";
    } else {
        std::cout << "  "
                  << paint("自检结果：" + std::to_string(abnormal) + " 项与预期不符", kColorBad)
                  << "\n";
        std::cout << "    · 应成功却失败：" << g_summary.unexpectedFail << " 项\n";
        std::cout << "    · 应失败却成功：" << g_summary.unexpectedPass << " 项\n";
    }
    return abnormal;
}

} // namespace DemoRunner
