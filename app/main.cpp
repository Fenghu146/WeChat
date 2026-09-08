// ============================================================
// 控制台演示程序（作者代号：FH）
// 流程：
//   1) 启动即自动运行一遍“QQ 群 vs 微信群”完整核心流程演示；
//   2) 之后进入交互菜单，可反复体验各项操作。
// 编译：cmake --build build 后运行 demo_fh。
// ============================================================
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
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
#include "im/social/group_registry_fh.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Windows 控制台按 UTF-8 输出，避免中文乱码
void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

const char* ok(bool value) { return value ? "成功" : "失败"; }

unsigned g_messageSeq = 0;

// 生成带自增 ID 的消息
std::shared_ptr<MessageFH> makeMessage(const std::shared_ptr<UserFH>& sender,
                                       const std::string& content) {
    return std::make_shared<MessageFH>("m" + std::to_string(++g_messageSeq),
                                       sender, content);
}

void printMembers(const std::string& title, const GroupFH& group) {
    std::cout << "  [" << title << "] " << group.getName()
              << "（群号 " << group.getGroupNumber() << "），成员数 "
              << group.members().size() << "：\n";
    for (const auto& kv : group.members()) {
        const auto& m = kv.second;
        std::cout << "     - " << m.getUser()->getNickname()
                  << "（ID " << m.getUser()->getId() << "）角色："
                  << toZhName(m.getRole())
                  << (m.isMuted() ? " ［被禁言］" : "") << "\n";
    }
}

void printMessages(const std::string& title, const GroupFH& group) {
    std::cout << "  [" << title << "] 消息记录 " << group.messages().size()
              << " 条：\n";
    for (const auto& msg : group.messages()) {
        std::cout << "     - " << msg->getSender()->getNickname() << "："
                  << msg->getContent()
                  << (msg->isRecalled() ? " ［已撤回］" : "") << "\n";
    }
}

// 自动核心流程演示：用独立的数据，顺序展示全部关键规则
void runAutoScenario() {
    std::cout << "\n=========== 自动演示：QQ 群 vs 微信群（核心流程） ===========\n";

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

    std::cout << "[1] 群主邀请成员入群，并分别任命管理员：\n";
    std::cout << "    QQ 邀请小管理 / 普通成员："
              << ok(qqGroup.inviteMember(owner, admin)) << " / "
              << ok(qqGroup.inviteMember(owner, member)) << "\n";
    std::cout << "    微信邀请小管理 / 普通成员："
              << ok(wxGroup.inviteMember(owner, admin)) << " / "
              << ok(wxGroup.inviteMember(owner, member)) << "\n";
    std::cout << "    QQ 任命小管理为管理员："
              << ok(qqGroup.setAdmin(owner, admin, true)) << "\n";
    std::cout << "    微信任命小管理为管理员："
              << ok(wxGroup.setAdmin(owner, admin, true)) << "\n";

    std::cout << "\n[2] 邀请规则差异：\n";
    std::cout << "    QQ 普通成员邀请路人甲（开关开启）："
              << ok(qqGroup.inviteMember(member, outsider)) << "\n";
    std::cout << "    微信普通成员邀请路人甲：" << ok(wxGroup.inviteMember(member, outsider))
              << "（应失败，微信仅 ADMIN+ 可邀请）\n";
    std::cout << "    微信管理员邀请路人甲：" << ok(wxGroup.inviteMember(admin, outsider))
              << "\n";
    std::cout << "    路人甲再次加入 QQ 群（成员唯一）："
              << ok(qqGroup.inviteMember(owner, outsider)) << "（应失败）\n";

    std::cout << "\n[3] 全员禁言设置差异：\n";
    std::cout << "    QQ 管理员设置全员禁言：" << ok(qqGroup.setAllMute(admin, true)) << "\n";
    std::cout << "    微信管理员设置全员禁言：" << ok(wxGroup.setAllMute(admin, true))
              << "（应失败，仅群主）\n";
    std::cout << "    微信群主设置全员禁言：" << ok(wxGroup.setAllMute(owner, true)) << "\n";

    std::cout << "\n[4] 全员禁言下的发言限制：\n";
    std::cout << "    QQ 普通成员发言：" << ok(qqGroup.sendMessage(
                  member, makeMessage(member, "禁言期间的发言")))
              << "（应失败）\n";
    std::cout << "    QQ 群主发言：" << ok(qqGroup.sendMessage(
                  owner, makeMessage(owner, "群主不受禁言影响")))
              << "\n";
    qqGroup.setAllMute(owner, false);
    wxGroup.setAllMute(owner, false);
    std::cout << "    （两群随后由群主解除全员禁言）\n";

    std::cout << "\n[5] 动态切换群管理模式（官方：成员数据不受伤害）：\n";
    std::cout << "    QQ 群切到“微信模式”后，管理员设全员禁言："
              << ok(qqGroup.switchPolicy(std::make_shared<WeChatPolicyFH>()))
              << "，执行结果：" << ok(qqGroup.setAllMute(admin, true))
              << "（应失败）\n";
    std::cout << "    QQ 群切回“QQ 模式”后，管理员设全员禁言："
              << ok(qqGroup.switchPolicy(qqPolicy)) << "，执行结果："
              << ok(qqGroup.setAllMute(admin, true)) << "（应成功）\n";
    qqGroup.setAllMute(owner, false);
    std::cout << "    切换前后群成员数不变：" << qqGroup.members().size()
              << "（成员数据未受伤害）\n";

    std::cout << "\n[6] 撤回消息（归属 + 时间窗）：\n";
    auto msgByMember = makeMessage(member, "大家好呀");
    auto msgByOwner = makeMessage(owner, "这是群主的消息");
    qqGroup.sendMessage(member, msgByMember);
    qqGroup.sendMessage(owner, msgByOwner);
    std::cout << "    普通成员撤回自己的消息："
              << ok(qqGroup.recallMessage(member, msgByMember->getId())) << "\n";
    std::cout << "    普通成员撤回群主的消息："
              << ok(qqGroup.recallMessage(member, msgByOwner->getId()))
              << "（应失败，只能撤回本人消息）\n";
    std::cout << "    群主撤回自己的消息："
              << ok(qqGroup.recallMessage(owner, msgByOwner->getId())) << "\n";

    std::cout << "\n[7] 任命管理员与转让群主：\n";
    std::cout << "    群主任命普通成员为管理员："
              << ok(qqGroup.setAdmin(owner, member, true)) << "\n";
    std::cout << "    普通成员（已管理员）无权再任命别人："
              << ok(qqGroup.setAdmin(member, outsider, true)) << "（应失败）\n";
    std::cout << "    群主将群转让给原管理员："
              << ok(qqGroup.transferOwner(owner, admin)) << "\n";

    printMembers("QQ 群", qqGroup);
    printMembers("微信群", wxGroup);
    printMessages("QQ 群", qqGroup);
    std::cout << "=========== 自动演示结束 ===========\n";
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

// 阶段 B 自动演示：多产品账号 / 开通 / 登录联动（独立数据）
void runAutoPlatformScenario() {
    std::cout << "\n======== 自动演示：多产品账号 / 开通 / 登录（任务书 1·4·5） ========\n";
    UserRegistryFH registry;
    ActivationManagerFH activation;
    LoginManagerFH login;
    constexpr int CURRENT_YEAR = 2026;

    auto xiaoming = registry.registerUser("10001", "小明", "2000-06-01", "广东·深圳", 2018);
    auto xiaohong = registry.registerUser("10002", "小红", "1999-11-11", "湖南·长沙", 2016);

    std::cout << "[B1] 号码体系：QQ 与微博共享 ID，微信独立可绑定 QQ\n";
    {
        auto qq = registry.makeAccount(*xiaoming, PlatformKindFH::QQ);
        auto wb = registry.makeAccount(*xiaoming, PlatformKindFH::Weibo);
        std::cout << "     " << xiaoming->getNickname() << " 的 QQ 号：" << qq.getAccountId()
                  << "（昵称 " << qq.getNickname() << "，所在地 " << qq.getLocation()
                  << "，T 龄 " << qq.tAge(CURRENT_YEAR) << " 年）\n";
        std::cout << "     微博号：" << wb.getAccountId() << "（与 QQ 号码相同）\n";
    }

    std::cout << "\n[B2] 自选开通微X 服务（ActivationManagerFH）\n";
    std::cout << "     开通 QQ：" << ok(activation.activate(*xiaoming, PlatformKindFH::QQ)) << "\n";
    std::cout << "     开通微博（QQ 同号即具资格）："
              << ok(activation.activate(*xiaoming, PlatformKindFH::Weibo)) << "\n";
    std::cout << "     未绑定微信号就开通微信："
              << ok(activation.activate(*xiaoming, PlatformKindFH::WeChat)) << "（应失败）\n";
    std::cout << "     为小明绑定微信号 wx-88-0001："
              << ok(registry.bindWeChat(xiaoming, "wx-88-0001")) << "\n";
    std::cout << "     绑定后开通微信："
              << ok(activation.activate(*xiaoming, PlatformKindFH::WeChat)) << "\n";
    std::cout << "     重复开通 QQ（幂等）："
              << ok(activation.activate(*xiaoming, PlatformKindFH::QQ)) << "（应失败）\n";
    std::cout << "     " << xiaoming->getNickname() << " 已开通：";
    for (PlatformKindFH p : xiaoming->activatedPlatforms())
        std::cout << toZhName(p) << " ";
    std::cout << "（共 " << activation.activatedCount(*xiaoming) << " 个微X 服务）\n";

    std::cout << "\n[B3] 微信账号与 QQ 绑定关系（AccountInfoFH）\n";
    auto wx = registry.makeAccount(*xiaoming, PlatformKindFH::WeChat);
    std::cout << "     微信号：" << wx.getAccountId() << "，绑定 QQ："
              << (wx.hasBindQQ() ? wx.getBindQqId() : "无") << "\n";

    std::cout << "\n[B4] 登录联动：登录一个服务 → 其余已开通服务自动登录\n";
    std::cout << "     小红尚未开通服务，登录 QQ："
              << ok(login.login(*xiaohong, PlatformKindFH::QQ)) << "（应失败）\n";
    std::cout << "     小明登录 QQ：" << ok(login.login(*xiaoming, PlatformKindFH::QQ)) << "\n";
    printOnlinePlatforms("小明", login.onlinePlatforms(*xiaoming));

    std::cout << "\n[B5] 单服务退出与取消开通规则\n";
    std::cout << "     在线状态直接取消开通微博："
              << ok(activation.deactivate(*xiaoming, PlatformKindFH::Weibo))
              << "（应失败，须先退出登录）\n";
    std::cout << "     退出微博登录："
              << ok(login.logout(*xiaoming, PlatformKindFH::Weibo)) << "\n";
    printOnlinePlatforms("小明", login.onlinePlatforms(*xiaoming));
    std::cout << "     退出微博后取消开通微博："
              << ok(activation.deactivate(*xiaoming, PlatformKindFH::Weibo)) << "\n";
    std::cout << "     剩余开通服务：";
    for (PlatformKindFH p : xiaoming->activatedPlatforms()) std::cout << toZhName(p) << " ";
    std::cout << "\n======== 阶段 B 自动演示结束 ========\n";
}

// 阶段 C 自动演示：好友/关注 · 群注册表 · QQ 临时讨论组（独立数据）
void runAutoSocialScenario() {
    std::cout << "\n======== 自动演示：好友 / 群注册表 / QQ 临时讨论组（阶段 C） ========\n";

    UserRegistryFH registry;
    auto xiaoming = registry.registerUser("10001", "小明", "2000-06-01", "广东·深圳", 2018);
    auto xiaohong = registry.registerUser("10002", "小红", "1999-11-11", "湖南·长沙", 2016);
    auto lurenB = registry.registerUser("10003", "路人乙", "2001-03-03", "四川·成都", 2020);
    auto lurenC = registry.registerUser("10004", "路人丙", "2002-07-07", "湖北·武汉", 2021);
    std::cout << "     绑定微信：小明 wx-88-0001 / 小红 wx-88-0002："
              << ok(registry.bindWeChat(xiaoming, "wx-88-0001")) << " / "
              << ok(registry.bindWeChat(xiaohong, "wx-88-0002")) << "\n";

    FriendRegistryFH friends;

    std::cout << "\n[C1] 好友/关注管理（FriendRegistryFH：QQ/微信=双向好友，微博=单向关注）\n";
    std::cout << "     小明与小红加 QQ 好友："
              << ok(friends.makeFriends(*xiaoming, *xiaohong, PlatformKindFH::QQ)) << "\n";
    std::cout << "     小红视角互为好友（双向关系）："
              << ok(friends.isFriend(*xiaohong, *xiaoming, PlatformKindFH::QQ)) << "\n";
    std::cout << "     重复加 QQ 好友："
              << ok(friends.makeFriends(*xiaoming, *xiaohong, PlatformKindFH::QQ))
              << "（应失败）\n";
    std::cout << "     自己加自己："
              << ok(friends.makeFriends(*xiaoming, *xiaoming, PlatformKindFH::QQ))
              << "（应失败）\n";
    std::cout << "     路人乙没有微信号，小明无法加其微信好友："
              << ok(friends.makeFriends(*xiaoming, *lurenB, PlatformKindFH::WeChat))
              << "（应失败）\n";
    std::cout << "     小明与小红加微信好友（双方均已绑定）："
              << ok(friends.makeFriends(*xiaoming, *xiaohong, PlatformKindFH::WeChat)) << "\n";
    std::cout << "     微博关注模型：小明关注小红："
              << ok(friends.follow(*xiaoming, *xiaohong)) << "\n";
    std::cout << "     微博“关注≠好友”：isFriend(微博) = "
              << ok(friends.isFriend(*xiaoming, *xiaohong, PlatformKindFH::Weibo))
              << "，isFollowing = " << ok(friends.isFollowing(*xiaoming, *xiaohong)) << "\n";
    std::cout << "     删除 QQ 好友："
              << ok(friends.unfriend(*xiaoming, *xiaohong, PlatformKindFH::QQ)) << "\n";
    std::cout << "     QQ 关系删除后微信好友不受影响（平台隔离）："
              << ok(friends.isFriend(*xiaoming, *xiaohong, PlatformKindFH::WeChat)) << "\n";

    GroupRegistryFH groupReg;
    std::cout << "\n[C2] 群注册表（GroupRegistryFH：预置各微X 群号 1001~1006）\n";
    std::cout << "     系统预置群目录：\n";
    for (PlatformKindFH p : {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo}) {
        for (const GroupInfoFH* g : groupReg.groupsOfPlatform(p))
            std::cout << "       · [" << toZhName(p) << " 群号 " << g->groupId << "] "
                      << g->name << "（官方群，容量 " << g->maxMembers << "）\n";
    }
    std::cout << "     小明加入 QQ 群 1001："
              << ok(groupReg.joinGroup(*xiaoming, PlatformKindFH::QQ, "1001")) << "\n";
    std::cout << "     重复加入 QQ 群 1001："
              << ok(groupReg.joinGroup(*xiaoming, PlatformKindFH::QQ, "1001"))
              << "（应失败）\n";
    std::cout << "     小红加入 QQ 群 1001："
              << ok(groupReg.joinGroup(*xiaohong, PlatformKindFH::QQ, "1001")) << "\n";
    std::cout << "     小明加入微信群 1003（凭微信号入册）："
              << ok(groupReg.joinGroup(*xiaoming, PlatformKindFH::WeChat, "1003")) << "\n";
    if (const GroupInfoFH* g = groupReg.findGroup("1003")) {
        std::cout << "     微信群 1003 当前成员：";
        for (const std::string& id : g->memberIds) std::cout << id << " ";
        std::cout << "\n";
    }
    std::cout << "     小明在 QQ 自建群“阶段C开发组”（自动分配群号）："
              << ok(groupReg.createGroup(*xiaoming, PlatformKindFH::QQ, "阶段C开发组")) << "\n";
    if (const GroupInfoFH* g = groupReg.findGroup("1007"))
        std::cout << "     自建群号 " << g->groupId << "，群主 " << g->ownerId << " 已自动入群\n";
    std::cout << "     小明当前群列表：";
    for (const GroupInfoFH* g : groupReg.groupsOfUser(*xiaoming))
        std::cout << "[" << toZhName(g->platform) << " " << g->groupId << " "
                  << g->name << "] ";
    std::cout << "\n";

    std::cout << "\n[C3] QQ 临时讨论组（DiscussionGroupFH：QQ 特有、容量小、全员可邀请）\n";
    DiscussionGroupFH disc("讨论组-1", "作业组会", xiaoming->getQQId());
    std::cout << "     小明创建讨论组：" << disc.getName() << "（id=" << disc.getId()
              << "，容量 " << disc.capacity() << "，当前 " << disc.size() << " 人）\n";
    std::cout << "     小明邀请小红："
              << ok(disc.invite(*xiaoming, *xiaohong)) << "\n";
    std::cout << "     任何成员可邀请：小红邀请路人乙："
              << ok(disc.invite(*xiaohong, *lurenB)) << "（区别于正式群需管理员）\n";
    std::cout << "     路人乙邀请路人丙："
              << ok(disc.invite(*lurenB, *lurenC)) << "\n";
    std::cout << "     成员自由退出：小红退组："
              << ok(disc.quit(*xiaohong)) << "\n";
    std::cout << "     非发起人解散（路人乙）："
              << ok(disc.disband(*lurenB)) << "（应失败）\n";
    std::cout << "     发起人解散（小明）："
              << ok(disc.disband(*xiaoming)) << "\n";
    std::cout << "     解散后再邀请："
              << ok(disc.invite(*xiaoming, *xiaohong)) << "（应失败）\n";
    std::cout << "     （微信群无“临时讨论组”概念 —— 平台差异演示点）\n";
    std::cout << "======== 阶段 C 自动演示结束 ========\n";
}

// 阶段 D 自动演示：群消息类型平台差异 / 群消息记录与引用扩展 / 产品视图示意
void runAutoMessageScenario() {
    std::cout << "\n======== 自动演示：群消息平台差异与群消息扩展（阶段 D） ========\n";

    auto toTimeText = [](std::chrono::system_clock::time_point t) -> std::string {
        const std::time_t tt = std::chrono::system_clock::to_time_t(t);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[16] = {0};
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        return std::string(buf);
    };

    // MessageFH 类型扩展示意（阶段 A 消息实体可声明消息类型）
    auto msgUser = std::make_shared<UserFH>("10001", "小明");
    auto voiceMsg =
        std::make_shared<MessageFH>("dm-voice", msgUser, "（60 秒语音）",
                                    MessageKindFH::VOICE);
    std::cout << "     MessageFH 类型扩展：消息 "
              << voiceMsg->getId() << " 的类型为“" << voiceMsg->kindZhName()
              << "”（缺省仍为文本）\n";

    std::cout << "\n[D1] 消息类型 / 文本长度能力（PlatformMessagePolicyFH）\n";
    UserRegistryFH registry;
    auto xiaoming =
        registry.registerUser("10001", "小明", "2000-06-01", "广东·深圳", 2018);
    auto xiaohong =
        registry.registerUser("10002", "小红", "1999-11-11", "湖南·长沙", 2016);
    std::cout << "     绑定微信（小明 wx-88-0001）："
              << ok(registry.bindWeChat(xiaoming, "wx-88-0001")) << "\n";

    GroupRegistryFH groups;
    std::cout << "     小明入群 QQ 1001 / 微信 1003 / 微博 1005："
              << ok(groups.joinGroup(*xiaoming, PlatformKindFH::QQ, "1001"))
              << " / "
              << ok(groups.joinGroup(*xiaoming, PlatformKindFH::WeChat, "1003"))
              << " / "
              << ok(groups.joinGroup(*xiaoming, PlatformKindFH::Weibo, "1005"))
              << "\n";
    std::cout << "     小红入群 QQ 1001："
              << ok(groups.joinGroup(*xiaohong, PlatformKindFH::QQ, "1001"))
              << "\n";

    std::cout << "     小明在 QQ 群 1001 发文件「架构图.pdf」："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::QQ,
                                            "1001", MessageKindFH::FILE,
                                            "架构图.pdf"))
              << "（QQ 支持全部类型）\n";
    std::cout << "     小明在微信群 1003 发文件「合同.docx」："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::WeChat,
                                            "1003", MessageKindFH::FILE,
                                            "合同.docx"))
              << "（应失败：微信群禁文件，简化口径）\n";
    std::cout << "     小明在微信群 1003 发图片「晚霞.jpg」："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::WeChat,
                                            "1003", MessageKindFH::IMAGE,
                                            "晚霞.jpg"))
              << "\n";
    std::cout << "     小明在微博群 1005 发图片「热点截图.jpg」："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::Weibo,
                                            "1005", MessageKindFH::IMAGE,
                                            "热点截图.jpg"))
              << "（应失败：微博群仅文本/表情，简化口径）\n";
    std::cout << "     小明在微博群 1005 发文本「今晚一起讨论任务书」："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::Weibo,
                                            "1005", MessageKindFH::TEXT,
                                            "今晚一起讨论任务书"))
              << "\n";
    const std::string weiboTooLong(1001, '长');
    std::cout << "     小明在微博群 1005 发超长文本（>1000 字）："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::Weibo,
                                            "1005", MessageKindFH::TEXT,
                                            weiboTooLong))
              << "（应失败）\n";

    std::cout << "\n[D2] 群消息增强：引用回复按平台能力校验\n";
    std::cout << "     小红在 QQ 群 1001 引用回复小明："
              << ok(groups.sendGroupMessage(*xiaohong, PlatformKindFH::QQ,
                                            "1001", MessageKindFH::TEXT,
                                            "回复@小明：收到", /*asReply=*/true))
              << "（QQ/微信支持引用）\n";
    std::cout << "     小明在微博群 1005 引用回复："
              << ok(groups.sendGroupMessage(*xiaoming, PlatformKindFH::Weibo,
                                            "1005", MessageKindFH::TEXT,
                                            "回复@小红：收到", /*asReply=*/true))
              << "（应失败：微博不支持引用）\n";

    std::cout << "\n[D3] 群消息记录与产品视图渲染（GroupRegistryFH 记录留档）\n";
    std::cout << "     QQ 群 1001 聊天记录（QQ 视图）：\n";
    for (const GroupChatRecordFH& r : groups.chatOf("1001"))
        std::cout << "       · " << PlatformMessagePolicyFH::render(
                                        PlatformKindFH::QQ, r.senderNick,
                                        r.content, r.kind,
                                        toTimeText(r.sentAt))
                  << (r.isReply ? "（引用回复）" : "") << "\n";
    std::cout << "     微信群 1003 聊天记录（微信视图）：\n";
    for (const GroupChatRecordFH& r : groups.chatOf("1003"))
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
    std::cout << "======== 阶段 D 自动演示结束 ========\n";
}

// 交互菜单演示：使用独立于自动演示的“手动演示群”
int runInteractiveMenu() {
    auto mOwner   = std::make_shared<UserFH>("20001", "手动群主");
    auto mAdmin   = std::make_shared<UserFH>("20002", "手动管理员");
    auto mMember  = std::make_shared<UserFH>("20003", "手动成员");
    auto mOutsider = std::make_shared<UserFH>("20004", "手动路人");

    auto qqGroup = std::make_shared<GroupFH>(
        "g-qq-101", 1003, "手动演示群",
        GroupConfigFH(30, true, false, std::chrono::seconds(120)),
        std::make_shared<QQPolicyFH>(), mOwner);
    qqGroup->inviteMember(mOwner, mAdmin);
    qqGroup->inviteMember(mOwner, mMember);
    qqGroup->setAdmin(mOwner, mAdmin, true);  // 任命为管理员

    int choice = -1;
    while (choice != 0) {
        std::cout << "\n================ 交互菜单 ================\n"
                  << " 1. 普通成员发送一条消息（QQ 群）\n"
                  << " 2. 查看两个群的成员与消息\n"
                  << " 3. 普通成员邀请路人（QQ/微信对比演示）\n"
                  << " 4. 管理员设置全员禁言后普通成员发言\n"
                  << " 5. 动态切换管理模式（切到微信模式再切回）\n"
                  << " 6. 群主解散“手动演示群”并验证不可操作\n"
                  << " 0. 退出\n"
                  << " 请选择：";
        std::cin >> choice;
        if (!std::cin) {  // 处理非数字输入
            std::cin.clear();
            std::cin.ignore(1024, '\n');
            choice = -1;
        }

        switch (choice) {
        case 1: {
            const std::string text = "来自普通成员的消息";
            auto msg = makeMessage(mMember, text);
            std::cout << "  发送结果：" << ok(qqGroup->sendMessage(mMember, msg)) << "\n";
            break;
        }
        case 2:
            printMembers("手动 QQ 群", *qqGroup);
            printMessages("手动 QQ 群", *qqGroup);
            break;
        case 3: {
            std::cout << "  QQ 群普通成员邀请路人："
                      << ok(qqGroup->inviteMember(mMember, mOutsider))
                      << "（QQ 开关开启，应成功）\n";
            auto wx = std::make_shared<GroupFH>(
                "g-wx-102", 1004, "手动微信群",
                GroupConfigFH(30, true, false, std::chrono::seconds(120)),
                std::make_shared<WeChatPolicyFH>(), mOwner);
            wx->inviteMember(mOwner, mMember);
            std::cout << "  微信群普通成员邀请路人："
                      << ok(wx->inviteMember(mMember, mOutsider))
                      << "（应失败，仅 ADMIN+）\n";
            break;
        }
        case 4: {
            std::cout << "  管理员设置全员禁言："
                      << ok(qqGroup->setAllMute(mAdmin, true)) << "\n";
            std::cout << "  普通成员发言："
                      << ok(qqGroup->sendMessage(mMember,
                              makeMessage(mMember, "全员禁言中的尝试")))
                      << "（应失败）\n";
            std::cout << "  群主解除全员禁言："
                      << ok(qqGroup->setAllMute(mOwner, false)) << "\n";
            break;
        }
        case 5: {
            std::cout << "  切到微信模式："
                      << ok(qqGroup->switchPolicy(std::make_shared<WeChatPolicyFH>()))
                      << "；管理员设全员禁言："
                      << ok(qqGroup->setAllMute(mAdmin, true)) << "（应失败）\n";
            std::cout << "  切回 QQ 模式："
                      << ok(qqGroup->switchPolicy(std::make_shared<QQPolicyFH>()))
                      << "；管理员设全员禁言："
                      << ok(qqGroup->setAllMute(mAdmin, true)) << "（应成功）\n";
            qqGroup->setAllMute(mOwner, false);
            break;
        }
        case 6: {
            std::cout << "  群主解散："
                      << ok(qqGroup->disband(mOwner)) << "\n";
            std::cout << "  解散后发送消息："
                      << ok(qqGroup->sendMessage(mMember,
                              makeMessage(mMember, "解散后的消息")))
                      << "（应失败）\n";
            std::cout << "  解散后成员数：" << qqGroup->members().size() << "\n";
            break;
        }
        case 0:
            std::cout << "  再见！\n";
            break;
        default:
            std::cout << "  无效选择，请重新输入。\n";
            break;
        }
    }
    return 0;
}

}  // namespace

int main() {
    initConsole();
    std::cout << "==== 模拟即时通信平台：QQ 群 / 微信群管理（作者代号 FH） ====\n";
    runAutoScenario();
    runAutoPlatformScenario();
    runAutoSocialScenario();
    runAutoMessageScenario();
    return runInteractiveMenu();
}
