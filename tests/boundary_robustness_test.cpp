// ============================================================
// boundary_robustness_test.cpp —— 边界与鲁棒性专项回归（阶段 E 增补）
// ------------------------------------------------------------
// 针对“正常流程之外”的输入与状态做专项校验：
//   1) 聚合根 GroupFH：空/重复消息、空白群名、非法撤回窗口、单员禁言对
//      管理员是否真的生效、人数上限边界、群主/管理员不变量、
//      切换到微信模式后原 QQ 管理员不再是特权账号（踢人/禁言/改群名/发公告被拒）；
//   2) 平台消息策略：越界消息类型、文本长度上限的“等于/超过”边界；
//   3) 群注册表：加入/推荐/踢人的平台规则与人数上限；建群参数边界；
//      预置群注入的幂等性；
//   4) 文本存档：特殊字符往返、记录号含逗号、CRLF、空/损坏/截断/非法数值
//      存档不得覆盖现有数据、写盘失败不得破坏原存档。
// 运行：cmake --build build && ctest --test-dir build --output-on-failure
// ============================================================
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "fh_mini_test.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/platform/persist_util_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_registry_fh.hpp"

namespace {

using namespace std::chrono;
using std::make_shared;
using std::shared_ptr;
namespace pu = persist_util_fh;

// ---------------- 通用小工具 ----------------

shared_ptr<UserFH> mkUser(const std::string& id) {
    return make_shared<UserFH>(id, "昵称" + id);
}

shared_ptr<MessageFH> mkMsg(const std::string& id, const shared_ptr<UserFH>& from,
                            const std::string& text = "hi") {
    return make_shared<MessageFH>(id, from, text);
}

// 每次调用给出一个新的存档文件名；同时清掉上次运行可能残留的同名文件，
// 保证用例独立于执行顺序与历史残留
std::string uniquePath(const std::string& tag) {
    static int seq = 0;
    std::string path =
        "fh_boundary_" + tag + "_" + std::to_string(++seq) + ".dat";
    std::remove(path.c_str());
    return path;
}

// 某自然人在注册表中最新的自建群号（groupsOfUser 按目录顺序返回）
std::string newestGroupId(const GroupRegistryFH& gr, const UserProfileFH& owner) {
    const std::vector<const GroupInfoFH*> mine = gr.groupsOfUser(owner);
    return mine.empty() ? std::string("(none)") : mine.back()->groupId;
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

// 测试用自然人集合（QQ 与微博同号；部分绑定微信）
struct People {
    UserRegistryFH reg;
    shared_ptr<UserProfileFH> a;  // QQ 20001 + 微信 wx-a
    shared_ptr<UserProfileFH> b;  // QQ 20002 + 微信 wx-b
    shared_ptr<UserProfileFH> c;  // QQ 20003，未绑定微信
    shared_ptr<UserProfileFH> d;  // QQ 20004 + 微信 wx-d

    People() {
        a = reg.registerUser("20001", "甲", "2006-01-01", "杭州", 2021);
        b = reg.registerUser("20002", "乙", "2007-02-02", "北京", 2021);
        c = reg.registerUser("20003", "丙", "2008-03-03", "广州", 2021);
        d = reg.registerUser("20004", "丁", "2009-04-04", "深圳", 2021);
        reg.bindWeChat(a, "wx-a");
        reg.bindWeChat(b, "wx-b");
        reg.bindWeChat(d, "wx-d");
    }
};

// ---------------- 1. GroupFH 聚合根边界 ----------------

FH_TEST(MessageBoundariesAndDuplicateIdAreEnforced) {
    auto owner = mkUser("o1");
    auto member = mkUser("m1");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("b-msg", 1001, "消息边界", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK(g->inviteMember(owner, member));

    // MessageFH 构造期即拒绝空标识 / 空内容 / 空发送者
    bool threw = false;
    try { MessageFH bad("", owner, "x"); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { MessageFH bad("m", owner, ""); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { MessageFH bad("m", nullptr, "x"); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);

    // 默认构造出的空消息不能发送（聚合根侧兜底校验）
    FH_CHECK(!g->sendMessage(owner, make_shared<MessageFH>()));

    FH_CHECK(g->sendMessage(owner, mkMsg("m1", owner, "第一条")));
    // 同一标识再次发送必须被拒，否则“按标识撤回”会命中另一条消息
    FH_CHECK(!g->sendMessage(member, mkMsg("m1", member, "冒名")));
    // 发送者与操作者不一致 → 拒绝
    FH_CHECK(!g->sendMessage(member, mkMsg("m2", owner, "代发")));
    FH_CHECK_EQ(g->messages().size(), std::size_t(1));
    // 撤回仍然精确指向唯一那条消息
    FH_CHECK(g->recallMessage(owner, "m1"));
    FH_CHECK(g->messages().front()->isRecalled());
}

FH_TEST(BlankGroupNameAndAnnouncementAreRejected) {
    auto owner = mkUser("o1");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("b-name", 1001, "群名边界", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK(!g->editGroup(owner, ""));
    FH_CHECK(!g->editGroup(owner, "   "));      // 全空白同样无效
    FH_CHECK(!g->editGroup(owner, " \t\r\n "));
    FH_CHECK_EQ(g->getName(), std::string("群名边界"));  // 群名未被污染
    FH_CHECK(g->editGroup(owner, " 有效名称 "));          // 首尾空格不算空
    FH_CHECK(!g->publishAnnouncement(owner, ""));
    FH_CHECK(!g->publishAnnouncement(owner, "  \n "));
    FH_CHECK_EQ(g->getAnnouncement(), std::string(""));
    FH_CHECK(g->publishAnnouncement(owner, "正常公告"));
}

FH_TEST(NegativeRecallLimitIsRejectedWithoutPoisoningConfig) {
    auto owner = mkUser("o1");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("b-recall", 1001, "撤回窗口", cfg,
                                  make_shared<QQPolicyFH>(), owner);

    // 负数窗口必须被拒且配置保持原值（旧实现先赋值再校验，会把 -1 留下来，
    // 使之后所有消息都无法撤回）
    FH_CHECK(!g->setRecallTimeLimit(owner, seconds(-1)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(120));
    FH_CHECK(g->setRecallTimeLimit(owner, seconds(0)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(0));
    FH_CHECK(g->setRecallTimeLimit(owner, seconds(60)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(60));

    // 越权者不能改（返回 false，且不改配置）
    auto member = mkUser("m1");
    FH_CHECK(g->inviteMember(owner, member));
    FH_CHECK(!g->setRecallTimeLimit(member, seconds(10)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(60));

    // 构造期非法配置仍然抛异常（保持既有契约）
    bool threw = false;
    try {
        GroupConfigFH bad(0, false, false, seconds(120));
        (void)bad;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    FH_CHECK(threw);
    threw = false;
    try {
        GroupConfigFH bad(50, false, false, seconds(-5));
        (void)bad;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    FH_CHECK(threw);
}

FH_TEST(IndividualMuteActuallyRestrictsAdmin) {
    auto owner = mkUser("o1");
    auto admin = mkUser("a1");
    auto member = mkUser("m1");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("b-mute", 1001, "禁言", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK(g->inviteMember(owner, admin));
    FH_CHECK(g->inviteMember(owner, member));
    FH_CHECK(g->setAdmin(owner, admin, true));

    // 群主可禁言管理员：禁言必须真正生效，而不是“返回成功但毫无效果”
    FH_CHECK(g->muteMember(owner, admin, true));
    FH_CHECK(g->isMuted(admin));
    FH_CHECK(!g->sendMessage(admin, mkMsg("ma", admin, "管理员发言")));
    FH_CHECK(g->muteMember(owner, admin, false));   // 解除后恢复
    FH_CHECK(g->sendMessage(admin, mkMsg("ma", admin, "管理员发言")));

    // 普通成员不能禁言管理员（权限矩阵不变）
    FH_CHECK(!g->muteMember(member, admin, true));
}

FH_TEST(MemberCapacityBoundaryIsEnforced) {
    auto owner = mkUser("o1");
    auto u2 = mkUser("u2");
    auto u3 = mkUser("u3");
    GroupConfigFH cfg(2, false, false, seconds(120));  // 上限 2 人
    auto g = make_shared<GroupFH>("b-cap", 1001, "人数上限", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK_EQ(g->members().size(), std::size_t(1));
    FH_CHECK(g->inviteMember(owner, u2));           // 恰好到上限 → 通过
    FH_CHECK_EQ(g->members().size(), std::size_t(2));
    FH_CHECK(!g->inviteMember(owner, u3));          // 超一人 → 拒绝
    FH_CHECK_EQ(g->members().size(), std::size_t(2));
    FH_CHECK_EQ(g->getRole(u3), std::nullopt);
}

FH_TEST(OwnerAndAdminInvariantsHoldAtBoundaries) {
    auto owner = mkUser("o1");
    auto admin = mkUser("a1");
    auto member = mkUser("m1");
    auto outsider = mkUser("x1");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("b-inv", 1001, "不变量", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK(g->inviteMember(owner, admin));
    FH_CHECK(g->inviteMember(owner, member));
    FH_CHECK(g->setAdmin(owner, admin, true));

    FH_CHECK(!g->kickMember(owner, outsider));      // 目标不在群内
    FH_CHECK(!g->kickMember(owner, owner));         // 不能踢自己
    FH_CHECK(!g->leaveGroup(owner));                // 群主不能直接退群
    FH_CHECK(!g->setAdmin(owner, owner, true));     // 不能把群主设成管理员
    FH_CHECK(!g->setAdmin(owner, admin, true));     // 已是管理员
    FH_CHECK(!g->transferOwner(owner, outsider));   // 目标不在群内
    FH_CHECK(!g->transferOwner(owner, owner));      // 不能转让给自己

    FH_CHECK(g->transferOwner(owner, member));
    FH_CHECK_EQ(g->getRole(member), GroupRoleFH::OWNER);
    FH_CHECK_EQ(g->getRole(owner), GroupRoleFH::MEMBER);
    int ownerCount = 0;
    for (const auto& kv : g->members())
        if (kv.second.getRole() == GroupRoleFH::OWNER) ++ownerCount;
    FH_CHECK_EQ(ownerCount, 1);                     // 任意时刻至多一个群主

    FH_CHECK(g->disband(member));
    FH_CHECK(g->isDisbanded());
    FH_CHECK(!g->sendMessage(member, mkMsg("after", member, "解散后")));
    FH_CHECK(!g->editGroup(member, "解散后改名"));
    FH_CHECK(!g->switchPolicy(make_shared<WeChatPolicyFH>()));
}

// 切换到微信模式后，原 QQ 管理员不再是特权账号（踢人/禁言/改群名/发公告一律被拒），
// 群主仍可；由 EDIT_GROUP 授权的群配置变更同样只归群主。
FH_TEST(WeChatModeRevokesRetainedAdminPrivileges) {
    auto owner = mkUser("o1");
    auto admin = mkUser("a1");
    auto member = mkUser("m1");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("b-wxmode", 1001, "模式切换", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK(g->inviteMember(owner, admin));
    FH_CHECK(g->inviteMember(owner, member));
    FH_CHECK(g->setAdmin(owner, admin, true));

    // QQ 模式：管理员可踢人/禁言，也可改群名、发公告、改群配置
    FH_CHECK(g->muteMember(admin, member, true));
    FH_CHECK(g->muteMember(admin, member, false));
    FH_CHECK(g->kickMember(admin, member));
    FH_CHECK(!g->contains(member));
    FH_CHECK(g->editGroup(admin, "QQ管理员改名"));
    FH_CHECK(g->publishAnnouncement(admin, "QQ管理员公告"));
    FH_CHECK(g->setRecallTimeLimit(admin, seconds(60)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(60));

    // 加回成员后切到微信模式：成员数据不受影响，管理员身份仍在
    FH_CHECK(g->inviteMember(owner, member));
    FH_CHECK(g->switchPolicy(make_shared<WeChatPolicyFH>()));
    FH_CHECK_EQ(g->getRole(admin), GroupRoleFH::ADMIN);
    FH_CHECK_EQ(g->members().size(), std::size_t(3));

    // 微信模式：管理员不是特权账号 —— 管理动作一律被拒，且状态不变
    const std::string nameBefore = g->getName();
    const std::string annBefore = g->getAnnouncement();
    FH_CHECK(!g->kickMember(admin, member));
    FH_CHECK(g->contains(member));
    FH_CHECK(!g->muteMember(admin, member, true));
    FH_CHECK(!g->isMuted(member));
    FH_CHECK(!g->editGroup(admin, "微信管理员改名"));
    FH_CHECK_EQ(g->getName(), nameBefore);
    FH_CHECK(!g->publishAnnouncement(admin, "微信管理员公告"));
    FH_CHECK_EQ(g->getAnnouncement(), annBefore);
    // 由 EDIT_GROUP 授权的群配置变更同样收归群主
    FH_CHECK(!g->setRecallTimeLimit(admin, seconds(10)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(60));
    FH_CHECK(!g->setMemberInviteEnabled(admin, true));
    FH_CHECK(!g->getConfig().isMemberInviteEnabled());

    // 群主仍然可以踢人/禁言/改群名/发公告/改配置
    FH_CHECK(g->muteMember(owner, member, true));
    FH_CHECK(g->isMuted(member));
    FH_CHECK(g->muteMember(owner, member, false));
    FH_CHECK(g->kickMember(owner, member));
    FH_CHECK(!g->contains(member));
    FH_CHECK(g->editGroup(owner, "微信群主改名"));
    FH_CHECK(g->publishAnnouncement(owner, "微信群主公告"));
    FH_CHECK(g->setRecallTimeLimit(owner, seconds(30)));
    FH_CHECK_EQ(g->getConfig().getRecallTimeLimit(), seconds(30));
}

// ---------------- 2. 平台消息策略边界 ----------------

FH_TEST(MessageKindAndPlatformBoundsAreRejected) {
    const auto bogus = static_cast<MessageKindFH>(99);
    FH_CHECK(!isValidKindFH(bogus));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ, bogus));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat, bogus));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo, bogus));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::COUNT,
                                                   MessageKindFH::TEXT));
    // 合法取值不受影响
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ,
                                                  MessageKindFH::DOCUMENT));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat,
                                                    MessageKindFH::DOCUMENT));
}

FH_TEST(GroupTextLengthBoundaryAtRegistry) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::QQ, "长度边界群", 50));
    const std::string gid = gr.groupsOfUser(*p.a).front()->groupId;

    const std::size_t limit = PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::QQ);
    FH_CHECK(gr.sendGroupMessage(*p.a, PlatformKindFH::QQ, gid,
                                 MessageKindFH::TEXT, std::string(limit, 'x')));
    FH_CHECK(!gr.sendGroupMessage(*p.a, PlatformKindFH::QQ, gid,
                                  MessageKindFH::TEXT, std::string(limit + 1, 'x')));
    FH_CHECK_EQ(gr.chatOf(gid).size(), std::size_t(1));  // 超长消息未落库

    // 非成员不能发言；类型不受支持时拒绝
    FH_CHECK(!gr.sendGroupMessage(*p.c, PlatformKindFH::QQ, gid,
                                  MessageKindFH::TEXT, "外部发言"));
    FH_CHECK(!gr.sendGroupMessage(*p.a, PlatformKindFH::Weibo, gid,
                                  MessageKindFH::TEXT, "平台不匹配"));
}

// ---------------- 3. 群注册表边界 ----------------

FH_TEST(JoinInviteAndCapacityRulesAtRegistry) {
    People p;
    GroupRegistryFH gr;

    // 微信群只能推荐加入：直接申请一律拒绝（官方预置群同样适用）
    FH_CHECK(!gr.joinGroup(*p.a, PlatformKindFH::WeChat, "1003"));

    // 自建微信群：创建者即群主且自动入群，其他人只能被推荐进来
    FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::WeChat, "自建微信群", 5));
    const std::string wx = "1007";
    FH_CHECK(gr.findGroup(wx) != nullptr);
    FH_CHECK(!gr.inviteIntoGroup(*p.d, *p.b, wx));   // 操作者不是群成员
    FH_CHECK(gr.inviteIntoGroup(*p.a, *p.b, wx));    // 群主推荐好友
    FH_CHECK(!gr.inviteIntoGroup(*p.a, *p.b, wx));   // 目标已在群内
    FH_CHECK(!gr.inviteIntoGroup(*p.a, *p.c, wx));   // 目标未绑定微信号
    FH_CHECK(!gr.inviteIntoGroup(*p.a, *p.a, wx));   // 不能推荐自己

    // QQ 群可以申请加入；满员边界：上限 2，创建者已占一席
    FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::QQ, "小群", 2));
    const std::string small = "1008";
    FH_CHECK(gr.joinGroup(*p.b, PlatformKindFH::QQ, small));   // 恰好到上限
    FH_CHECK(!gr.joinGroup(*p.c, PlatformKindFH::QQ, small));  // 超一人被拒
    FH_CHECK(!gr.joinGroup(*p.b, PlatformKindFH::QQ, small));  // 重复申请被拒
    FH_CHECK_EQ(gr.findGroup(small)->memberIds.size(), std::size_t(2));

    // 未绑定微信号的自然人不能建微信群
    FH_CHECK(!gr.createGroup(*p.c, PlatformKindFH::WeChat, "丙的群", 10));
}

FH_TEST(WeChatKickRequiresOwnerAndPredefinedGroupIsProtected) {
    People p;
    GroupRegistryFH gr;

    // 自建微信群：群主可踢人；普通成员（哪怕是老成员）不能踢
    FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::WeChat, "踢人测试群", 5));
    FH_CHECK(gr.inviteIntoGroup(*p.a, *p.b, "1007"));
    FH_CHECK(!gr.kickMember(*p.b, *p.a, "1007"));   // 普通成员不能踢群主
    FH_CHECK(gr.kickMember(*p.a, *p.b, "1007"));    // 群主可踢
    FH_CHECK(!gr.kickMember(*p.a, *p.a, "1007"));   // 不能踢自己
    FH_CHECK(!gr.kickMember(*p.a, *p.b, "1007"));   // 目标已不在群内
    FH_CHECK(!gr.kickMember(*p.a, *p.c, "1007"));   // 目标从未入群

    // 官方预置群不可解散、不可转让群主
    FH_CHECK(!gr.disbandGroup(*p.a, "1003"));
    FH_CHECK(!gr.transferOwner(*p.a, *p.b, "1003"));
    FH_CHECK(gr.findGroup("1003") != nullptr);
}

FH_TEST(CreateGroupRejectsInvalidArguments) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(!gr.createGroup(*p.a, PlatformKindFH::QQ, "", 50));       // 空群名
    FH_CHECK(!gr.createGroup(*p.a, PlatformKindFH::QQ, "群", 0));      // 人数上限 0
    FH_CHECK(!gr.createGroup(*p.c, PlatformKindFH::WeChat, "群", 50)); // 无微信号
    const std::size_t before = gr.groupCount();
    FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::QQ, "正常群", 50));
    FH_CHECK_EQ(gr.groupCount(), before + 1);
    const GroupInfoFH* g = gr.findGroup("1007");  // 自建群号从 1007 起
    FH_CHECK(g != nullptr);
    if (g) {
        FH_CHECK_EQ(g->ownerId, std::string("20001"));
        FH_CHECK_EQ(g->memberIds.size(), std::size_t(1));
    }
}

FH_TEST(EnsurePredefinedMembersIsIdempotentAndDoesNotResurrect) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.ensurePredefinedMembers(
        "1003", {"wx-a", "wx-b"}));                       // 首次注入
    FH_CHECK(!gr.ensurePredefinedMembers("1003", {"wx-a"}));  // 已注入 → 不动作
    FH_CHECK_EQ(gr.findGroup("1003")->memberIds.size(), std::size_t(2));

    // 所有人退群后不能被再次“复活”（文档承诺的幂等语义）
    FH_CHECK(gr.leaveGroup(*p.a, "1003"));
    FH_CHECK(gr.leaveGroup(*p.b, "1003"));
    FH_CHECK_EQ(gr.findGroup("1003")->memberIds.size(), std::size_t(0));
    FH_CHECK(!gr.ensurePredefinedMembers("1003", {"wx-a", "wx-b"}));
    FH_CHECK_EQ(gr.findGroup("1003")->memberIds.size(), std::size_t(0));

    FH_CHECK(!gr.ensurePredefinedMembers("1001", {}));        // 空名单不动作
    FH_CHECK(!gr.ensurePredefinedMembers("1007", {"wx-a"}));  // 非预置群不动作
}

// ---------------- 4. 文本存档边界 ----------------

FH_TEST(GroupArchiveRoundTripKeepsSpecialCharacters) {
    const std::string path = uniquePath("grp_rt");
    const std::string weirdName =
        std::string("群|名\n带\\转义") + pu::kFieldSepFH + "与分隔符";
    std::string gid;
    {
        People p;
        GroupRegistryFH gr(path);   // 文件不存在 → 保持预置群
        FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::QQ, weirdName, 50));
        gid = newestGroupId(gr, *p.a);
        FH_CHECK(gr.joinGroup(*p.b, PlatformKindFH::QQ, gid));
        FH_CHECK(gr.setGroupAdmin(*p.a, *p.b, gid, true));
        FH_CHECK(gr.sendGroupMessage(*p.a, PlatformKindFH::QQ, gid,
                                     MessageKindFH::TEXT, "消息\n内容|含符"));
    }
    {
        GroupRegistryFH reloaded;
        FH_CHECK(reloaded.setPersistencePath(path));
        const GroupInfoFH* g = reloaded.findGroup(gid);
        FH_CHECK(g != nullptr);
        if (g) {
            FH_CHECK_EQ(g->name, weirdName);                     // 群名逐字节还原
            FH_CHECK_EQ(g->memberIds.size(), std::size_t(2));
            FH_CHECK_EQ(g->adminIds.size(), std::size_t(1));
            FH_CHECK_EQ(reloaded.chatOf(gid).size(), std::size_t(1));
            if (!reloaded.chatOf(gid).empty()) {
                FH_CHECK_EQ(reloaded.chatOf(gid).front().content,
                            std::string("消息\n内容|含符"));
            }
        }
    }
    std::remove(path.c_str());
}

FH_TEST(MemberIdsContainingCommaRoundTrip) {
    const std::string path = uniquePath("grp_comma");
    // 号码中含逗号：子分隔符必须转义，否则 "a,b" 会被读成两个成员
    const std::string tricky = "a,b";
    std::string gid;
    {
        UserRegistryFH reg;
        auto u = reg.registerUser(tricky, "逗号号", "2006-01-01", "杭州", 2021);
        GroupRegistryFH gr(path);
        FH_CHECK(gr.createGroup(*u, PlatformKindFH::QQ, "逗号群", 50));
        gid = newestGroupId(gr, *u);
        FH_CHECK(gr.findGroup(gid)->memberIds.size() == std::size_t(1));
    }
    {
        GroupRegistryFH reloaded;
        FH_CHECK(reloaded.setPersistencePath(path));
        const GroupInfoFH* g = reloaded.findGroup(gid);
        FH_CHECK(g != nullptr);
        if (g) {
            FH_CHECK_EQ(g->memberIds.size(), std::size_t(1));
            if (g->memberIds.size() == 1) FH_CHECK_EQ(g->memberIds.front(), tricky);
        }
    }
    std::remove(path.c_str());
}

FH_TEST(CrlfArchiveLoadsCleanly) {
    const std::string path = uniquePath("grp_crlf");
    std::string gid;
    {
        People p;
        GroupRegistryFH gr(path);
        FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::QQ, "换行存档", 50));
        gid = newestGroupId(gr, *p.a);
        FH_CHECK(gr.joinGroup(*p.b, PlatformKindFH::QQ, gid));
    }
    // 把存档改写成 CRLF（编辑器在 Windows 上另存为的常见结果）
    std::string content;
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        content = ss.str();
    }
    std::string crlf;
    for (char ch : content) {
        if (ch == '\n') crlf += "\r\n";
        else crlf.push_back(ch);
    }
    writeFile(path, crlf);

    GroupRegistryFH reloaded;
    FH_CHECK(reloaded.setPersistencePath(path));
    const GroupInfoFH* g = reloaded.findGroup(gid);
    FH_CHECK(g != nullptr);
    if (g) {
        FH_CHECK_EQ(g->name, std::string("换行存档"));      // 群名未带尾部 '\r'
        FH_CHECK_EQ(g->platform, PlatformKindFH::QQ);      // 平台名未带 '\r'
        FH_CHECK_EQ(g->memberIds.size(), std::size_t(2));
        if (g->memberIds.size() == 2) {
            // 行尾字段最容易吞掉 '\r'：成员号必须逐字节干净
            FH_CHECK_EQ(g->memberIds.front(), std::string("20001"));
            FH_CHECK_EQ(g->memberIds.back(), std::string("20002"));
        }
    }
    std::remove(path.c_str());
}

FH_TEST(CorruptOrUnusableArchiveNeverWipesCurrentData) {
    // 1) 只有垃圾内容
    std::string path = uniquePath("grp_bad");
    writeFile(path, "not-a-record\n");
    {
        GroupRegistryFH gr;
        FH_CHECK(!gr.setPersistencePath(path));           // 报告不可用
        FH_CHECK_EQ(gr.groupCount(), std::size_t(6));     // 6 个官方预置群仍在
        FH_CHECK(gr.findGroup("1001") != nullptr);
    }
    std::remove(path.c_str());
    // 2) 空文件
    path = uniquePath("grp_empty");
    writeFile(path, "");
    {
        GroupRegistryFH gr;
        FH_CHECK(!gr.setPersistencePath(path));
        FH_CHECK_EQ(gr.groupCount(), std::size_t(6));
    }
    std::remove(path.c_str());
    // 3) 存档被截断（最后一行不完整）
    path = uniquePath("grp_trunc");
    writeFile(path, std::string("G") + pu::kFieldSepFH + "QQ" + pu::kFieldSepFH);
    {
        GroupRegistryFH gr;
        FH_CHECK(!gr.setPersistencePath(path));
        FH_CHECK_EQ(gr.groupCount(), std::size_t(6));
    }
    std::remove(path.c_str());
}

FH_TEST(ArchiveRejectsIllegalFieldsAndDuplicateGroups) {
    const std::string sep(1, pu::kFieldSepFH);
    const std::string path = uniquePath("grp_illegal");
    std::ostringstream ss;
    // 合法记录
    ss << 'G' << sep << "QQ" << sep << "1001" << sep << "电影兴趣群" << sep << ""
       << sep << 50 << sep << 1 << sep << "-" << sep << "-" << '\n';
    // 人数上限为负数（旧实现 stoul 会变成天文数字，等于“无上限”）
    ss << 'G' << sep << "QQ" << sep << "1008" << sep << "负上限群" << sep << ""
       << sep << "-5" << sep << 0 << sep << "-" << sep << "-" << '\n';
    // 群号重复的同一群
    ss << 'G' << sep << "QQ" << sep << "1001" << sep << "重复群" << sep << ""
       << sep << 50 << sep << 1 << sep << "-" << sep << "-" << '\n';
    // 消息类型越界
    ss << 'M' << sep << "1001" << sep << 99 << sep << "10001" << sep << "甲"
       << sep << "越界类型" << sep << 0 << sep << 1 << '\n';
    // 群人数上限不是数字
    ss << 'G' << sep << "QQ" << sep << "1009" << sep << "非法上限" << sep << ""
       << sep << "abc" << sep << 0 << sep << "-" << sep << "-" << '\n';
    writeFile(path, ss.str());

    GroupRegistryFH gr;
    FH_CHECK(gr.setPersistencePath(path));
    FH_CHECK_EQ(gr.groupCount(), std::size_t(1));        // 只有 1001 可用
    FH_CHECK(gr.findGroup("1001") != nullptr);
    FH_CHECK(gr.findGroup("1008") == nullptr);           // 非法人数上限被丢弃
    FH_CHECK(gr.findGroup("1009") == nullptr);           // 非数字人数上限被丢弃
    FH_CHECK_EQ(gr.findGroup("1001")->name, std::string("电影兴趣群"));  // 重复群取首条
    FH_CHECK_EQ(gr.chatOf("1001").size(), std::size_t(0));  // 越界消息类型被丢弃
    std::remove(path.c_str());
}

FH_TEST(FriendArchiveSurvivesCorruptionAndRoundTripsRemark) {
    const std::string path = uniquePath("friend");
    People p;
    FriendRegistryFH fr;
    FH_CHECK(fr.makeFriends(*p.a, *p.b, PlatformKindFH::QQ));
    FH_CHECK(fr.setRemark(*p.a, *p.b, PlatformKindFH::QQ, "备注\n含|符"));
    const std::size_t edges = fr.edgeCount();
    FH_CHECK(fr.saveToFile(path));

    FriendRegistryFH reloaded;
    FH_CHECK(reloaded.loadFromFile(path));
    FH_CHECK_EQ(reloaded.edgeCount(), edges);
    FH_CHECK(reloaded.isFriend(*p.a, *p.b, PlatformKindFH::QQ));
    FH_CHECK_EQ(reloaded.remarkOf(*p.a, *p.b, PlatformKindFH::QQ),
                std::string("备注\n含|符"));

    // 存档被写坏：不得用空集合覆盖内存中已有的好友关系
    writeFile(path, "broken-line\n");
    FriendRegistryFH kept;
    FH_CHECK(kept.makeFriends(*p.a, *p.b, PlatformKindFH::QQ));
    const std::size_t keptEdges = kept.edgeCount();
    FH_CHECK(!kept.loadFromFile(path));
    FH_CHECK_EQ(kept.edgeCount(), keptEdges);
    std::remove(path.c_str());
}

FH_TEST(ActivationArchiveRebuildsInsteadOfAccumulating) {
    const std::string path = uniquePath("act");
    People p;
    FH_CHECK(p.a->addActivated(PlatformKindFH::QQ));
    FH_CHECK(p.a->addActivated(PlatformKindFH::Weibo));
    FH_CHECK(p.reg.saveActivatedToFile(path));

    // 存档里把微博从甲的开通列表中删掉
    std::ostringstream ss;
    ss << 'A' << pu::kFieldSepFH << "20001" << pu::kFieldSepFH << "QQ" << '\n';
    writeFile(path, ss.str());
    FH_CHECK(p.reg.loadActivatedFromFile(path));

    FH_CHECK(p.a->isActivated(PlatformKindFH::QQ));
    FH_CHECK(!p.a->isActivated(PlatformKindFH::Weibo));  // 只增不减的实现会留成 true
    std::remove(path.c_str());
}

FH_TEST(SaveToUnwritablePathFailsAndKeepsExistingArchive) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.createGroup(*p.a, PlatformKindFH::QQ, "存档群", 50));

    const std::string good = uniquePath("grp_good");
    FH_CHECK(gr.saveToFile(good));
    std::string before;
    {
        std::ifstream in(good, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        before = ss.str();
    }
    FH_CHECK(!before.empty());

    // 目录不存在 → 写失败必须返回 false，且不得破坏已有存档。
    // 注意：MinGW/libstdc++ 上 std::ofstream 打开不存在的目录时会“报告成功”，
    // 随后所有写入静默失败；只看 !out 的实现会返回 true（谎报存档已写入）。
    // 原子写（写 .tmp 再 rename）能通过 rename 失败识别出这种情况。
    FH_CHECK(!gr.saveToFile("no_such_dir_fh/sub/archive.dat"));
    std::string after;
    {
        std::ifstream in(good, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        after = ss.str();
    }
    FH_CHECK_EQ(after, before);
    std::remove(good.c_str());
}

}  // namespace

int main() { return ::fhtest::runAll("boundary-robustness"); }
