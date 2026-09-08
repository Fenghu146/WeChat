// ============================================================
// regression_tests.cpp —— 真实性逻辑测试补充（多轮压力验证）
// ------------------------------------------------------------
// 针对设计文档规则与代码实现之间的细微差异，补充覆盖：
//   1. 构造期参数校验的完整边界（空 ID / 空内容 / 空发送者 / 零时间窗）
//   2. 转让群主后角色原子交换的完整权限迁移
//   3. 微信群邀请矩阵（群主 / 管理员 / 普通成员，开关 ON/OFF）
//   4. 微博关注与 QQ/微信好友的严格隔离
//   5. 群解散后消息列表保留但不可操作
//   6. 全员禁言期间 Owner 发言的跨平台一致性
//   7. GroupMembership 构造非法参数
// ============================================================
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "fh_mini_test.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_membership_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_registry_fh.hpp"

namespace {

using namespace std::chrono;
using std::make_shared;
using std::shared_ptr;

shared_ptr<UserFH> mk(const std::string& id) {
    return make_shared<UserFH>(id, "昵称" + id);
}

// ----------------------------------------------------------
// 1. 构造期参数校验（覆盖 Message / GroupMembership / GroupConfig）
// ----------------------------------------------------------

FH_TEST(MessageRejectsEmptyId) {
    bool threw = false;
    try { MessageFH m("", mk("s1"), "内容"); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

FH_TEST(MessageRejectsEmptyContent) {
    bool threw = false;
    try { MessageFH m("id1", mk("s1"), ""); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

FH_TEST(MessageRejectsNullSender) {
    bool threw = false;
    try { MessageFH m("id1", nullptr, "内容"); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

FH_TEST(GroupMembershipRejectsNullUser) {
    bool threw = false;
    try { GroupMembershipFH gm(nullptr, GroupRoleFH::MEMBER); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

FH_TEST(ZeroRecallTimeWindowIsValid) {
    // recallTimeLimit=0 是合法边界：消息创建时已超窗，不能撤回任何消息
    GroupConfigFH cfg(500, false, false, seconds(0));
    FH_CHECK_EQ(cfg.getRecallTimeLimit(), seconds(0));
}

// ----------------------------------------------------------
// 2. 转让群主后权限完整迁移
// ----------------------------------------------------------

FH_TEST(TransferOwnerFullPermissionMigration) {
    auto owner = mk("o1");
    auto admin = mk("a1");
    auto member = mk("m1");
    GroupConfigFH cfg(500, false, false, seconds(120));
    auto g = make_shared<GroupFH>("g1", 1001, "测试群", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->inviteMember(owner, member);
    g->setAdmin(owner, admin, true);

    // 转让前：owner 可编辑群名
    FH_CHECK(g->editGroup(owner, "旧名"));
    FH_CHECK_EQ(g->getName(), std::string("旧名"));

    // 转让
    FH_CHECK(g->transferOwner(owner, admin));
    FH_CHECK_EQ(g->getRole(admin), GroupRoleFH::OWNER);
    FH_CHECK_EQ(g->getRole(owner), GroupRoleFH::MEMBER);

    // 转让后：原群主降级为 MEMBER，不再拥有 ADMIN+ 权限
    FH_CHECK(!g->editGroup(owner, "新名"));       // MEMBER 不能改群名
    FH_CHECK(!g->publishAnnouncement(owner, "公告"));  // MEMBER 不能发公告
    FH_CHECK(!g->setAllMute(owner, true));        // MEMBER 不能设全员禁言
    FH_CHECK(!g->muteMember(owner, admin, true)); // MEMBER 不能禁言他人
    FH_CHECK(!g->kickMember(owner, member));      // MEMBER 不能踢人
    FH_CHECK(!g->setAdmin(owner, member, true));  // MEMBER 不能任免管理员
    FH_CHECK(!g->disband(owner));                 // MEMBER 不能解散群

    // 新群主继承全部 OWNER 权限
    auto other = mk("o9");
    g->inviteMember(admin, other);  // 先拉一个普通成员用于后续测试
    FH_CHECK(g->editGroup(admin, "新名"));
    FH_CHECK(g->publishAnnouncement(admin, "欢迎"));
    FH_CHECK(g->setAllMute(admin, true));
    FH_CHECK(g->setAllMute(admin, false));
    FH_CHECK(g->muteMember(admin, member, true));
    FH_CHECK(g->kickMember(admin, member));
    FH_CHECK(g->setAdmin(admin, other, true));
    FH_CHECK(g->disband(admin));
    FH_CHECK(g->isDisbanded());
}

// ----------------------------------------------------------
// 3. 微信群邀请矩阵（群主 / 管理员 / 普通成员 × 开关 ON/OFF）
// ----------------------------------------------------------

FH_TEST(WeChatInviteMatrixAllRoles) {
    // 开关 OFF：微信仅 OWNER 可邀请，ADMIN/MEMBER 均不可
    auto ownerOff = mk("o1-off");
    auto adminOff = mk("a1-off");
    auto memberOff = mk("m1-off");
    auto outsiderOff = mk("x1-off");
    GroupConfigFH cfgOff(500, false, false, seconds(120));
    auto gOff = make_shared<GroupFH>("gw-off", 1003, "微信群关", cfgOff,
                                     make_shared<WeChatPolicyFH>(), ownerOff);
    FH_CHECK(gOff->inviteMember(ownerOff, adminOff));
    FH_CHECK(gOff->setAdmin(ownerOff, adminOff, true));  // 提升为管理员
    FH_CHECK(gOff->inviteMember(ownerOff, memberOff));
    FH_CHECK(gOff->inviteMember(ownerOff, outsiderOff));   // 群主可邀请
    auto outsider2 = mk("x2-off");
    FH_CHECK(!gOff->inviteMember(adminOff, outsider2));    // 管理员亦禁止
    FH_CHECK(!gOff->inviteMember(memberOff, outsiderOff)); // 普通成员不可邀请（已入群）
    auto outsider3 = mk("x3-off");
    FH_CHECK(!gOff->inviteMember(memberOff, outsider3));   // 普通成员不可邀请（新人）

    // 开关 ON：微信依然仅 OWNER 可邀请（ignore memberInviteEnabled）
    auto ownerOn = mk("o1-on");
    auto adminOn = mk("a1-on");
    auto memberOn = mk("m1-on");
    GroupConfigFH cfgOn(500, true, false, seconds(120));
    auto gOn = make_shared<GroupFH>("gw-on", 1004, "微信群开", cfgOn,
                                    make_shared<WeChatPolicyFH>(), ownerOn);
    FH_CHECK(gOn->inviteMember(ownerOn, adminOn));
    FH_CHECK(gOn->setAdmin(ownerOn, adminOn, true));  // 提升为管理员
    FH_CHECK(gOn->inviteMember(ownerOn, memberOn));
    auto outsider4 = mk("x4-on");
    FH_CHECK(gOn->inviteMember(ownerOn, outsider4));       // 群主可邀请
    auto outsider5 = mk("x5-on");
    FH_CHECK(!gOn->inviteMember(adminOn, outsider5));      // 管理员亦禁止
    auto outsider6 = mk("x6-on");
    FH_CHECK(!gOn->inviteMember(memberOn, outsider6));     // 普通成员仍不可邀请
}

// ----------------------------------------------------------
// 4. 微博关注与 QQ/微信好友严格隔离
// ----------------------------------------------------------

FH_TEST(WeiboFollowDoesNotAffectQQFriendship) {
    UserRegistryFH reg;
    auto a = reg.registerUser("u1", "用户A", "2000-01-01", "北京", 2020);
    auto b = reg.registerUser("u2", "用户B", "2000-02-02", "上海", 2020);
    reg.bindWeChat(a, "wx-a");
    reg.bindWeChat(b, "wx-b");

    FriendRegistryFH fr;
    // QQ 加好友
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
    // 微博关注 ≠ QQ 好友
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::Weibo));
    FH_CHECK(!fr.isFollowing(*a, *b));  // 还没关注
    // 微博关注后，QQ 好友关系不受影响
    FH_CHECK(fr.follow(*a, *b));
    FH_CHECK(fr.isFollowing(*a, *b));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));  // QQ 好友仍在
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::Weibo));  // 微博不是好友
    // 取消关注不影响 QQ 好友
    FH_CHECK(fr.unfollow(*a, *b));
    FH_CHECK(!fr.isFollowing(*a, *b));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
}

FH_TEST(QQFriendshipDoesNotCreateWeiboFollowing) {
    UserRegistryFH reg;
    auto a = reg.registerUser("u3", "C", "2001-01-01", "广州", 2021);
    auto b = reg.registerUser("u4", "D", "2001-02-02", "深圳", 2021);

    FriendRegistryFH fr;
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(!fr.isFollowing(*a, *b));  // QQ 好友不会自动变成微博关注
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::Weibo));  // 也不是微博好友
}

// ----------------------------------------------------------
// 5. 群解散后消息列表保留但不可操作
// ----------------------------------------------------------

FH_TEST(DisbandedGroupKeepsMessagesButRejectsAllOps) {
    auto owner = mk("o1");
    auto member = mk("m1");
    GroupConfigFH cfg(500, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gd", 1001, "解散群", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, member);
    // 发几条消息
    FH_CHECK(g->sendMessage(owner, make_shared<MessageFH>("msg1", owner, "你好")));
    FH_CHECK(g->sendMessage(member, make_shared<MessageFH>("msg2", member, "嗨")));
    FH_CHECK(g->sendMessage(owner, make_shared<MessageFH>("msg3", owner, "再见")));

    // 解散
    FH_CHECK(g->disband(owner));
    FH_CHECK(g->isDisbanded());

    // 消息列表保留（用于回顾）
    FH_CHECK_EQ(g->messages().size(), std::size_t(3));

    // 但所有操作被拒绝
    FH_CHECK(!g->sendMessage(owner, make_shared<MessageFH>("msg4", owner, "复活")));
    FH_CHECK(!g->recallMessage(owner, "msg1"));
    FH_CHECK(!g->inviteMember(owner, mk("new")));
    FH_CHECK(!g->editGroup(owner, "改名"));
}

// ----------------------------------------------------------
// 6. 全员禁言期间 Owner 发言跨平台一致
// ----------------------------------------------------------

FH_TEST(AllMuteOwnerCanSpeakBothPlatforms) {
    // 两组独立用户，避免跨群污染
    auto ownerQ = mk("oq1");
    auto adminQ = mk("aq1");
    auto memberQ = mk("mq1");
    auto ownerW = mk("ow1");
    auto adminW = mk("aw1");
    auto memberW = mk("mw1");

    GroupConfigFH cfg(500, false, true, seconds(120));  // 构造时全员禁言
    auto qq = make_shared<GroupFH>("gaq", 1001, "QQ全员禁", cfg,
                                   make_shared<QQPolicyFH>(), ownerQ);
    auto wx = make_shared<GroupFH>("gaw", 1003, "微信全员禁", cfg,
                                   make_shared<WeChatPolicyFH>(), ownerW);
    FH_CHECK(qq->inviteMember(ownerQ, adminQ));
    FH_CHECK(qq->setAdmin(ownerQ, adminQ, true));  // 提升为管理员
    FH_CHECK(qq->inviteMember(ownerQ, memberQ));
    FH_CHECK(wx->inviteMember(ownerW, adminW));
    FH_CHECK(wx->setAdmin(ownerW, adminW, true));  // 提升为管理员
    FH_CHECK(wx->inviteMember(ownerW, memberW));

    // Owner 在两种平台均可发言
    FH_CHECK(qq->sendMessage(ownerQ, make_shared<MessageFH>("q1", ownerQ, "owner qq")));
    FH_CHECK(wx->sendMessage(ownerW, make_shared<MessageFH>("w1", ownerW, "owner wx")));
    // Admin 在两种平台均可发言
    FH_CHECK(qq->sendMessage(adminQ, make_shared<MessageFH>("q2", adminQ, "admin qq")));
    FH_CHECK(wx->sendMessage(adminW, make_shared<MessageFH>("w2", adminW, "admin wx")));
    // Member 在两种平台均不能发言
    FH_CHECK(!qq->sendMessage(memberQ, make_shared<MessageFH>("q3", memberQ, "member qq")));
    FH_CHECK(!wx->sendMessage(memberW, make_shared<MessageFH>("w3", memberW, "member wx")));
}

// ----------------------------------------------------------
// 7. 撤回时间窗含边界：恰好等于时间窗时允许
// ----------------------------------------------------------

FH_TEST(RecallAtExactBoundaryIsAllowed) {
    auto owner = mk("o1");
    GroupConfigFH cfg(500, false, false, seconds(60));
    auto g = make_shared<GroupFH>("grb", 1001, "边界群", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    const auto t0 = system_clock::now();
    auto msg = make_shared<MessageFH>("rb", owner, "边界消息", t0);
    FH_CHECK(g->sendMessage(owner, msg));
    // now == sentAt + 60s → 恰好边界，应允许
    GroupContextFH ctx;
    ctx.group = g.get();
    ctx.operatorUser = owner;
    ctx.message = msg;
    ctx.now = t0 + seconds(60);
    QQPolicyFH pol;
    FH_CHECK(pol.isAllowed(ActionFH::RECALL_MESSAGE, ctx));
    // now == sentAt + 61s → 超过 1s，应拒绝
    ctx.now = t0 + seconds(61);
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, ctx));
}

// ----------------------------------------------------------
// 8. KICK_MEMBER 对OWNER的保护：admin 不能踢 owner
// ----------------------------------------------------------

FH_TEST(CannotKickOwner) {
    auto owner = mk("o1");
    auto admin = mk("a1");
    GroupConfigFH cfg(500, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gk", 1001, "踢人测试", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->setAdmin(owner, admin, true);
    // admin 不能踢 owner（同级保护：owner > admin）
    FH_CHECK(!g->kickMember(admin, owner));
    FH_CHECK_EQ(g->members().size(), std::size_t(2));
    // owner 可以踢 admin
    FH_CHECK(g->kickMember(owner, admin));
    FH_CHECK_EQ(g->members().size(), std::size_t(1));
}

// ----------------------------------------------------------
// 9. 群内成员查询：不在群的用户 getRole 返回 nullopt
// ----------------------------------------------------------

FH_TEST(GetRoleReturnsNulloptForNonMembers) {
    auto owner = mk("o1");
    auto outsider = mk("x9");
    GroupConfigFH cfg(500, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gn", 1001, "查询群", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK(!g->getRole(outsider).has_value());
    FH_CHECK(g->getRole(owner).has_value());
    FH_CHECK_EQ(*g->getRole(owner), GroupRoleFH::OWNER);
}

// ----------------------------------------------------------
// 10. MessageKind 构造：默认类型为 TEXT
// ----------------------------------------------------------

FH_TEST(MessageDefaultKindIsText) {
    auto u = mk("s1");
    MessageFH m("mid", u, "内容");
    FH_CHECK_EQ(m.getKind(), MessageKindFH::TEXT);
}

}  // namespace

int main() { return ::fhtest::runAll("regression"); }
