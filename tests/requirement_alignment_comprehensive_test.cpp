// ============================================================
// requirement_alignment_comprehensive_test.cpp
// ------------------------------------------------------------
// 补充现有测试未覆盖的真实场景，严格对照任务书全部 6 条要求：
//
// 1.(1) 用户基本信息：号码 ID（QQ/微博同号、微信独立）、资料、好友/群列表
// 2.(1) 好友管理：添加/修改(备注)/删除/查询
// 2.(2) 微X 共同好友 + 跨服务推荐添加（含双方均开通要求）
// 3.(1) 预置群号 1001~1006
// 3.(2) 加入/退出/挨踢/查询群成员
// 3.(3) QQ 申请加入 vs 微信推荐加入；QQ 临时讨论组；QQ 管理员制度 vs 微信仅群主特权
// 4.   自选开通微X 服务
// 5.   一个服务登录 → 其余联动在线
// 6.(1) 断电保存：文件读写 → 启动加载
// 6.(2) 登录后全部已开通服务上线
// 6.(3) 跨服务推荐好友（来源+目标均须本人开通）
// 6.(4) 一个服务当前群特色功能展示 + 动态切换管理模式
// ============================================================
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "fh_mini_test.hpp"
#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
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

namespace {

using std::make_shared;
using std::shared_ptr;
using std::string;
using std::vector;
using namespace std::chrono;

shared_ptr<UserFH> mkUser(const string& id, const string& nick = "") {
    return make_shared<UserFH>(id, nick.empty() ? ("昵称" + id) : nick);
}

// ================================================================
// 1.(1) 用户基本信息：号码体系 + 资料 + 好友/群列表
// ================================================================

FH_TEST(Comprehensive_UserBasicInfoAndLists) {
    UserRegistryFH reg;
    // 注册自然人（号码 ID、昵称、出生时间、所在地、T龄）
    auto alice = reg.registerUser("u001", "Alice", "2000-01-01", "北京", 2018);
    auto bob   = reg.registerUser("u002", "Bob",   "1999-06-15", "上海", 2016);
    reg.bindWeChat(alice, "wx-alice");
    reg.bindWeChat(bob,   "wx-bob");

    // 号码体系：QQ 与微博共享 ID；微信独立
    FH_CHECK_EQ(alice->getQQId(),   string("u001"));
    FH_CHECK_EQ(alice->getWeiboId(), string("u001"));  // 共享
    FH_CHECK_EQ(alice->getWeChatId(), string("wx-alice"));
    FH_CHECK_EQ(bob->getQQId(),   string("u002"));
    FH_CHECK_EQ(bob->getWeiboId(), string("u002"));

    // 基本资料
    FH_CHECK_EQ(alice->getNickname(), string("Alice"));
    FH_CHECK_EQ(alice->getBirthday(), string("2000-01-01"));
    FH_CHECK_EQ(alice->getLocation(), string("北京"));
    FH_CHECK_EQ(alice->tAge(2026), 8);   // T龄 = 2026 - 2018
    alice->setNickname("ALice修改");
    FH_CHECK_EQ(alice->getNickname(), string("ALice修改"));

    // 好友列表（QQ 好友）
    FriendRegistryFH fr;
    FH_CHECK(fr.makeFriends(*alice, *bob, PlatformKindFH::QQ));
    const auto qqFriends = fr.friendIds(*alice, PlatformKindFH::QQ);
    FH_CHECK_EQ(qqFriends.size(), size_t(1));
    FH_CHECK(!qqFriends.empty() && qqFriends[0] == bob->getQQId());

    // 群列表
    GroupRegistryFH gr;
    FH_CHECK(gr.joinGroup(*alice, PlatformKindFH::QQ, "1001"));
    FH_CHECK(gr.joinGroup(*bob,   PlatformKindFH::QQ, "1001"));
    const auto aliceGroups = gr.groupsOfUser(*alice);
    FH_CHECK_EQ(aliceGroups.size(), size_t(1));
    FH_CHECK(!aliceGroups.empty() && aliceGroups[0]->groupId == "1001");
}

// ================================================================
// 1.(1) 微信独立 ID：未绑定微信的用户不能获取微信账号
// ================================================================

FH_TEST(WeChatIndependentIdWithoutBinding) {
    UserRegistryFH reg;
    auto u = reg.registerUser("uid1", "单人", "2000-01-01", "深圳", 2020);
    FH_CHECK(!u->hasWeChatAccount());
    FH_CHECK_EQ(u->platformAccountId(PlatformKindFH::WeChat), string(""));
    FH_CHECK(reg.bindWeChat(u, "wx-only1"));
    FH_CHECK(u->hasWeChatAccount());
    FH_CHECK_EQ(u->platformAccountId(PlatformKindFH::WeChat), string("wx-only1"));
    // 每人至多绑定一个微信号
    FH_CHECK(!reg.bindWeChat(u, "wx-only2"));
}

// ================================================================
// 2.(1) 好友管理：添加/修改(备注)/删除/查询完整闭环
// ================================================================

FH_TEST(Comprehensive_FriendManagementFullLoop) {
    UserRegistryFH reg;
    auto a = reg.registerUser("fa01", "甲", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("fa02", "乙", "2000-02-02", "上海", 2019);
    auto c = reg.registerUser("fa03", "丙", "2000-03-03", "广州", 2020);
    FriendRegistryFH fr;

    // 添加
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*b, *a, PlatformKindFH::QQ));  // 双向
    // 重复添加失败
    FH_CHECK(!fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    // 自己加自己失败
    FH_CHECK(!fr.makeFriends(*a, *a, PlatformKindFH::QQ));

    // 修改备注（视角属于设置者）
    FH_CHECK(fr.setRemark(*a, *b, PlatformKindFH::QQ, "老同学"));
    FH_CHECK_EQ(fr.remarkOf(*a, *b, PlatformKindFH::QQ), string("老同学"));
    FH_CHECK_EQ(fr.remarkOf(*b, *a, PlatformKindFH::QQ), string(""));  // 对方视角无备注
    FH_CHECK(fr.setRemark(*b, *a, PlatformKindFH::QQ, "同桌"));
    FH_CHECK_EQ(fr.remarkOf(*b, *a, PlatformKindFH::QQ), string("同桌"));

    // 删除
    FH_CHECK(fr.unfriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK_EQ(fr.remarkOf(*a, *b, PlatformKindFH::QQ), string(""));
    // 删除后备注清空，但 c 不受影响
    FH_CHECK(!fr.isFriend(*a, *c, PlatformKindFH::QQ));
}

// ================================================================
// 2.(2) 共同好友：QQ 共同好友 / 微信共同好友 / 微博共同关注
// ================================================================

FH_TEST(Comprehensive_CommonFriendsAllPlatforms) {
    UserRegistryFH reg;
    auto a = reg.registerUser("cf01", "A", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("cf02", "B", "2000-02-02", "上海", 2019);
    auto c = reg.registerUser("cf03", "C", "2000-03-03", "广州", 2020);
    auto d = reg.registerUser("cf04", "D", "2000-04-04", "深圳", 2021);
    reg.bindWeChat(a, "wxcf1"); reg.bindWeChat(b, "wxcf2");
    reg.bindWeChat(c, "wxcf3"); reg.bindWeChat(d, "wxcf4");

    FriendRegistryFH fr;
    // 建立关系
    FH_CHECK(fr.makeFriends(*a, *c, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*b, *c, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*a, *d, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*a, *c, PlatformKindFH::WeChat));
    FH_CHECK(fr.makeFriends(*b, *c, PlatformKindFH::WeChat));
    FH_CHECK(fr.follow(*a, *c)); FH_CHECK(fr.follow(*b, *c));
    FH_CHECK(fr.follow(*a, *d)); FH_CHECK(fr.follow(*b, *d));

    // QQ 共同好友 = {C}
    const auto qqCommon = fr.commonFriends(*a, *b, PlatformKindFH::QQ);
    FH_CHECK_EQ(qqCommon.size(), size_t(1));
    FH_CHECK(!qqCommon.empty() && qqCommon[0] == c->getQQId());

    // 微信共同好友 = {C}
    const auto wxCommon = fr.commonFriends(*a, *b, PlatformKindFH::WeChat);
    FH_CHECK_EQ(wxCommon.size(), size_t(1));
    FH_CHECK(!wxCommon.empty() && wxCommon[0] == c->getWeChatId());

    // 微博共同关注 = {C, D}
    const auto wbCommon = fr.commonFollowing(*a, *b);
    FH_CHECK_EQ(wbCommon.size(), size_t(2));
}

// ================================================================
// 2.(2) + 6.(3) 跨服务推荐好友：双方均须开通来源和目标服务
// ================================================================

FH_TEST(Comprehensive_CrossPlatformRecommendation_AllConditions) {
    UserRegistryFH reg;
    auto a = reg.registerUser("cr01", "推荐人A", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("cr02", "推荐人B", "2000-02-02", "上海", 2019);
    auto c = reg.registerUser("cr03", "目标C",   "2000-03-03", "广州", 2020);
    auto d = reg.registerUser("cr04", "目标D",   "2000-04-04", "深圳", 2021);
    reg.bindWeChat(a, "wx-cr1"); reg.bindWeChat(b, "wx-cr2");
    reg.bindWeChat(c, "wx-cr3"); reg.bindWeChat(d, "wx-cr4");

    FriendRegistryFH fr;
    ActivationManagerFH act;

    // 初始化：A-C QQ 好友，B-C QQ 好友；A-B 微信无好友
    FH_CHECK(fr.makeFriends(*a, *c, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*b, *c, PlatformKindFH::QQ));

    // 全部开通服务
    for (auto* u : {a.get(), b.get(), c.get(), d.get()})
        FH_CHECK(act.activate(*reg.findByQQId(u->getQQId()), PlatformKindFH::QQ));
    for (auto* u : {a.get(), b.get(), c.get()})
        FH_CHECK(act.activate(*reg.findByQQId(u->getQQId()), PlatformKindFH::WeChat));
    // d 只开通 QQ，未开通微信

    // 推荐条件检查（在添加之前）：
    // A → C：QQ 好友 + 双方开通 QQ/WeChat + C 有微信账号 → 可推荐
    FH_CHECK(fr.isRecommendable(*a, *c, PlatformKindFH::QQ, PlatformKindFH::WeChat));
    // A → D：D 未开通微信 → 不可推荐
    FH_CHECK(!fr.isRecommendable(*a, *d, PlatformKindFH::QQ, PlatformKindFH::WeChat));

    // 一键推荐添加：A 依据 QQ 好友关系将 C 添加为微信好友
    FH_CHECK(fr.addFriendFromRecommendation(*a, *c, PlatformKindFH::QQ,
                                             PlatformKindFH::WeChat));
    FH_CHECK(fr.isFriend(*a, *c, PlatformKindFH::WeChat));
    // B 也可一键添加 C（依据 QQ 好友）
    FH_CHECK(fr.addFriendFromRecommendation(*b, *c, PlatformKindFH::QQ,
                                             PlatformKindFH::WeChat));
    // A 和 C 已是微信好友，不再可推荐
    FH_CHECK(!fr.isRecommendable(*a, *c, PlatformKindFH::QQ, PlatformKindFH::WeChat));
    // 推荐列表：A 的 QQ 好友中，C 已是微信好友（排除），D 未开通微信（排除）→ 空
    const auto recs = fr.recommendFriendsFrom(*a, reg, PlatformKindFH::QQ,
                                               PlatformKindFH::WeChat);
    FH_CHECK_EQ(recs.size(), size_t(0));
}

// ================================================================
// 3.(1) 预置群号 1001~1006 且类型正确
// ================================================================

FH_TEST(Comprehensive_PredefinedGroups1001to1006) {
    GroupRegistryFH gr;
    // 数量与类型
    FH_CHECK_EQ(gr.groupCount(), size_t(6));
    const auto qqGroups  = gr.groupsOfPlatform(PlatformKindFH::QQ);
    const auto wxGroups  = gr.groupsOfPlatform(PlatformKindFH::WeChat);
    const auto wbGroups  = gr.groupsOfPlatform(PlatformKindFH::Weibo);
    FH_CHECK_EQ(qqGroups.size(),  size_t(2));
    FH_CHECK_EQ(wxGroups.size(), size_t(2));
    FH_CHECK_EQ(wbGroups.size(), size_t(2));
    // 群号内容
    FH_CHECK(gr.findGroup("1001") != nullptr);
    FH_CHECK(gr.findGroup("1002") != nullptr);
    FH_CHECK(gr.findGroup("1003") != nullptr);
    FH_CHECK(gr.findGroup("1004") != nullptr);
    FH_CHECK(gr.findGroup("1005") != nullptr);
    FH_CHECK(gr.findGroup("1006") != nullptr);
    // 官方群无群主
    for (const char* id : {"1001","1002","1003","1004","1005","1006"}) {
        const GroupInfoFH* g = gr.findGroup(id);
        FH_CHECK(g != nullptr && g->predefined && g->ownerId.empty());
    }
}

// ================================================================
// 3.(2) 加入/退出/挨踢/查询群成员：QQ 群 + 微信群完整流程
// ================================================================

FH_TEST(Comprehensive_GroupJoinLeaveKickQuery) {
    UserRegistryFH reg;
    auto owner = reg.registerUser("gj01", "群主", "2000-01-01", "北京", 2018);
    auto admin = reg.registerUser("gj02", "管理", "2000-02-02", "上海", 2019);
    auto m1    = reg.registerUser("gj03", "成员A","2000-03-03", "广州", 2020);
    auto m2    = reg.registerUser("gj04", "成员B","2000-04-04", "深圳", 2021);
    auto wxO   = reg.registerUser("gj05", "微群主","2000-05-05", "杭州", 2022);
    auto wxM   = reg.registerUser("gj06", "微成员","2000-06-06", "成都", 2023);
    reg.bindWeChat(wxO, "wx-gj5"); reg.bindWeChat(wxM, "wx-gj6");

    GroupRegistryFH gr;

    // --- QQ 自建群：申请加入 + 管理员制度 + 挨踢 ---
    FH_CHECK(gr.createGroup(*owner, PlatformKindFH::QQ, "QQ踢人测试群"));  // 1007
    FH_CHECK(gr.joinGroup(*admin,  PlatformKindFH::QQ, "1007"));
    FH_CHECK(gr.joinGroup(*m1,     PlatformKindFH::QQ, "1007"));
    FH_CHECK(gr.joinGroup(*m2,     PlatformKindFH::QQ, "1007"));
    FH_CHECK(gr.setGroupAdmin(*owner, *admin, "1007", true));

    // 查询群成员
    const auto* ids = gr.memberIdsOf("1007");
    FH_CHECK(ids != nullptr && ids->size() == 4);

    // 挨踢规则：普通成员不可踢；管理员不可踢群主/同级；群主可踢任何人
    FH_CHECK(!gr.kickMember(*m1, *m2, "1007"));      // 普通成员不可踢
    FH_CHECK(!gr.kickMember(*admin, *owner, "1007")); // 管理员不可踢群主
    FH_CHECK(!gr.kickMember(*admin, *admin, "1007")); // 管理员不可踢自己
    FH_CHECK(gr.kickMember(*admin, *m2, "1007"));    // 管理员可踢普通成员
    FH_CHECK(gr.kickMember(*owner, *admin, "1007")); // 群主可踢管理员（同时摘除管理员身份）
    FH_CHECK(!gr.isAdminOf(*admin, "1007"));          // 被踢即摘除管理员
    ids = gr.memberIdsOf("1007");
    FH_CHECK(ids != nullptr && ids->size() == 2);     // 只剩 owner + m1

    // 退出群
    FH_CHECK(gr.leaveGroup(*m1, "1007"));
    FH_CHECK(!gr.leaveGroup(*m1, "1007"));            // 已退出
    ids = gr.memberIdsOf("1007");
    FH_CHECK(ids != nullptr && ids->size() == 1);     // 只剩 owner

    // --- 微信群：推荐加入 + 仅群主可踢 ---
    FH_CHECK(gr.createGroup(*wxO, PlatformKindFH::WeChat, "微信踢人测试群"));  // 1008
    FH_CHECK(gr.inviteIntoGroup(*wxO, *wxM, "1008"));
    FH_CHECK(!gr.kickMember(*wxM, *wxO, "1008"));   // 成员不可踢群主
    FH_CHECK(gr.kickMember(*wxO, *wxM, "1008"));    // 仅群主可踢
    FH_CHECK(!gr.setGroupAdmin(*wxO, *wxM, "1008", true));  // 微信群无管理员
}

// ================================================================
// 3.(3) QQ 可申请加入 / 微信只能推荐加入；临时讨论组仅 QQ
// ================================================================

FH_TEST(Comprehensive_JoinRulesAndDiscussionGroup) {
    UserRegistryFH reg;
    auto xm  = reg.registerUser("jr01", "小明", "2000-06-01", "深圳", 2018);
    auto xh  = reg.registerUser("jr02", "小红", "1999-11-11", "长沙", 2016);
    auto noWx = reg.registerUser("jr03", "无微信", "2001-01-01", "西安", 2022);
    reg.bindWeChat(xm, "wx-jr1");
    reg.bindWeChat(xh, "wx-jr2");

    GroupRegistryFH gr;

    // QQ 群：直接申请加入官方群
    FH_CHECK(gr.joinGroup(*xm, PlatformKindFH::QQ, "1001"));
    // 微信群：直接申请加入官方群 → 失败（只能推荐）
    FH_CHECK(!gr.joinGroup(*xm, PlatformKindFH::WeChat, "1003"));
    // 官方微信群尚无成员，无人可推荐 → 推荐也失败
    FH_CHECK(!gr.inviteIntoGroup(*xm, *xh, "1003"));
    // 自建微信群：群主可推荐
    FH_CHECK(gr.createGroup(*xm, PlatformKindFH::WeChat, "家人群"));  // 1007
    FH_CHECK(gr.inviteIntoGroup(*xm, *xh, "1007"));
    // 未绑微信者不可被推荐
    FH_CHECK(!gr.inviteIntoGroup(*xm, *noWx, "1007"));
    // QQ 群不走推荐路径（inviteIntoGroup 仅限微信）
    FH_CHECK(!gr.inviteIntoGroup(*xm, *xh, "1001"));
    // 满员不可推荐
    FH_CHECK(gr.createGroup(*xm, PlatformKindFH::WeChat, "两人小群", 1));  // 1008
    FH_CHECK(!gr.inviteIntoGroup(*xm, *xh, "1008"));

    // 临时讨论组（仅 QQ）：任何成员可邀请
    DiscussionGroupFH dg("disc-1", "作业讨论组", xm->getQQId());
    FH_CHECK(dg.invite(*xm, *xh));         // 发起人邀请
    FH_CHECK(!dg.invite(*xh, *xh));        // 不能邀请自己
    // 讨论组成员以 QQ 号入册，微信号不参与
    FH_CHECK(dg.contains(xm->getQQId()));
    FH_CHECK(!dg.contains(xm->getWeChatId()));
    // 解散：仅发起人可执行
    FH_CHECK(!dg.disband(*xh));
    FH_CHECK(dg.disband(*xm));
    FH_CHECK(dg.isDisbanded());
    FH_CHECK(!dg.invite(*xm, *xh));        // 解散后不可操作
}

// ================================================================
// 3.(3) 群动态切换管理模式（任务书 6.(4)）
// ================================================================

FH_TEST(Comprehensive_GroupPolicySwitchPreservesMembers) {
    auto owner = mkUser("sw01", "Owner");
    auto admin = mkUser("sw02", "Admin");
    auto member = mkUser("sw03", "Member");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gs", 1001, "切换测试群", cfg,
                                   make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->inviteMember(owner, member);
    g->setAdmin(owner, admin, true);
    g->sendMessage(owner, make_shared<MessageFH>("msg1", owner, "切换前消息"));

    const size_t membersBefore = g->members().size();
    const size_t msgsBefore    = g->messages().size();

    // 切换到微信模式
    FH_CHECK(g->switchPolicy(make_shared<WeChatPolicyFH>()));
    // 成员数据不受伤害
    FH_CHECK_EQ(g->members().size(), membersBefore);
    FH_CHECK_EQ(g->messages().size(), msgsBefore);
    // 权限变化：微信模式下 admin 不可设全员禁言
    FH_CHECK(!g->setAllMute(admin, true));
    FH_CHECK(g->setAllMute(owner, true));
    // 切回 QQ 模式
    FH_CHECK(g->switchPolicy(make_shared<QQPolicyFH>()));
    FH_CHECK(g->setAllMute(admin, true));  // QQ 模式下 admin 可设

    // 切换多次不影响成员数据
    FH_CHECK_EQ(g->members().size(), membersBefore);
}

// ================================================================
// 4. 自选开通微X 服务
// ================================================================

FH_TEST(Comprehensive_ActivationManagement) {
    UserRegistryFH reg;
    auto u = reg.registerUser("act01", "开通测试", "2000-01-01", "北京", 2018);
    ActivationManagerFH act;

    // 注册即有 QQ/微博资格（同号），但未开通（需手动激活）
    FH_CHECK(!u->isActivated(PlatformKindFH::QQ));
    FH_CHECK(!u->isActivated(PlatformKindFH::Weibo));

    // 开通 QQ（有主号，自动具备微博资格）
    FH_CHECK(act.activate(*u, PlatformKindFH::QQ));
    FH_CHECK(u->isActivated(PlatformKindFH::QQ));
    // 重复开通拒绝
    FH_CHECK(!act.activate(*u, PlatformKindFH::QQ));
    // 开通微博（同号）
    FH_CHECK(act.activate(*u, PlatformKindFH::Weibo));
    FH_CHECK_EQ(act.activatedCount(*u), 2);

    // 未绑定微信 → 无法开通微信
    FH_CHECK(!act.activate(*u, PlatformKindFH::WeChat));
    reg.bindWeChat(u, "wx-act1");
    FH_CHECK(act.activate(*u, PlatformKindFH::WeChat));
    FH_CHECK_EQ(act.activatedCount(*u), 3);

    // 取消开通：在线时拒绝，退出后允许
    LoginManagerFH login;
    FH_CHECK(login.login(*u, PlatformKindFH::QQ));
    FH_CHECK(!act.deactivate(*u, PlatformKindFH::WeChat));  // 在线不可取消
    FH_CHECK(!act.deactivate(*u, PlatformKindFH::Weibo));   // 在线不可取消
    login.logoutAll(*u);  // 全部退出后再取消
    FH_CHECK(act.deactivate(*u, PlatformKindFH::WeChat));
    FH_CHECK_EQ(act.activatedCount(*u), 2);
}

// ================================================================
// 5 + 6.(2) 一个服务登录 → 其余已开通服务自动联动在线
// ================================================================

FH_TEST(Comprehensive_LoginLinkageAllServices) {
    UserRegistryFH reg;
    auto u = reg.registerUser("lg01", "联动测试", "2000-01-01", "北京", 2018);
    reg.bindWeChat(u, "wx-lg1");
    ActivationManagerFH act;
    LoginManagerFH login;
    FH_CHECK(act.activate(*u, PlatformKindFH::QQ));
    FH_CHECK(act.activate(*u, PlatformKindFH::Weibo));
    FH_CHECK(act.activate(*u, PlatformKindFH::WeChat));

    // 登录 QQ → 全部已开通服务上线
    FH_CHECK(login.login(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::Weibo));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::WeChat));
    FH_CHECK_EQ(login.onlinePlatforms(*u).size(), size_t(3));

    // 单服务退出，其余保持在线
    FH_CHECK(login.logout(*u, PlatformKindFH::QQ));
    FH_CHECK(!login.isOnline(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::Weibo));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::WeChat));

    // 退出全部
    login.logoutAll(*u);
    FH_CHECK_EQ(login.onlinePlatforms(*u).size(), size_t(0));
    // 退出后仍可重新登录（服务仍为开通状态）
    FH_CHECK(login.login(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::Weibo));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::WeChat));
}

// ================================================================
// 6.(1) 断电保存完整往返（开通/好友/群成员/聊天记录）
// ================================================================

FH_TEST(Comprehensive_PersistenceFullRoundTrip) {
    const string fp = "persist_full_friends.dat";
    const string gp = "persist_full_groups.dat";
    const string ap = "persist_full_act.dat";
    std::remove(fp.c_str()); std::remove(gp.c_str()); std::remove(ap.c_str());

    // === 阶段一：构建数据并写回 ===
    {
        UserRegistryFH reg;
        auto a = reg.registerUser("pfull1", "保存甲", "2000-01-01", "北京", 2018);
        auto b = reg.registerUser("pfull2", "保存乙", "2000-02-02", "上海", 2019);
        reg.bindWeChat(a, "wx-pf1"); reg.bindWeChat(b, "wx-pf2");
        reg.setActivationPath(ap);

        ActivationManagerFH act;
        act.activate(*a, PlatformKindFH::QQ);
        act.activate(*a, PlatformKindFH::WeChat);
        act.activate(*b, PlatformKindFH::Weibo);

        FriendRegistryFH fr; fr.setPersistencePath(fp);
        FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
        FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::WeChat));
        FH_CHECK(fr.setRemark(*a, *b, PlatformKindFH::QQ, "实验伙伴"));

        GroupRegistryFH gr; gr.setPersistencePath(gp);
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::QQ, "断电保存群"));  // 1007
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::WeChat, "微信断电群"));  // 1008
        FH_CHECK(gr.inviteIntoGroup(*a, *b, "1008"));
        FH_CHECK(gr.sendGroupMessage(*a, PlatformKindFH::QQ, "1007",
                                      MessageKindFH::TEXT, "断电前最后一条消息"));
        // 析构写回文件
    }

    // === 阶段二：全新容器从文件恢复 ===
    {
        UserRegistryFH reg;
        auto a = reg.registerUser("pfull1", "保存甲", "2000-01-01", "北京", 2018);
        auto b = reg.registerUser("pfull2", "保存乙", "2000-02-02", "上海", 2019);
        reg.bindWeChat(a, "wx-pf1"); reg.bindWeChat(b, "wx-pf2");
        FH_CHECK(reg.setActivationPath(ap));
        FH_CHECK(a->isActivated(PlatformKindFH::QQ));
        FH_CHECK(a->isActivated(PlatformKindFH::WeChat));
        FH_CHECK(b->isActivated(PlatformKindFH::Weibo));

        FriendRegistryFH fr;
        FH_CHECK(fr.setPersistencePath(fp));
        FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
        FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::WeChat));
        FH_CHECK_EQ(fr.remarkOf(*a, *b, PlatformKindFH::QQ), string("实验伙伴"));

        GroupRegistryFH gr;
        FH_CHECK(gr.setPersistencePath(gp));
        const GroupInfoFH* g7 = gr.findGroup("1007");
        FH_CHECK(g7 != nullptr);
        if (g7) {
            FH_CHECK_EQ(g7->name, string("断电保存群"));
            FH_CHECK_EQ(g7->ownerId, string("pfull1"));
            FH_CHECK_EQ(g7->chat.size(), size_t(1));
            FH_CHECK_EQ(g7->chat.front().content, string("断电前最后一条消息"));
        }
        FH_CHECK(gr.isOwnerOf(*a, "1007"));
        FH_CHECK(gr.isOwnerOf(*a, "1008"));
        const auto* m8 = gr.memberIdsOf("1008");
        FH_CHECK(m8 != nullptr && m8->size() == 2);
        // 自建群号续编正常
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::QQ, "续建群"));
        FH_CHECK(gr.findGroup("1009") != nullptr);
    }

    std::remove(fp.c_str()); std::remove(gp.c_str()); std::remove(ap.c_str());
}

// ================================================================
// 6.(4) 群特色功能展示：QQ 群 vs 微信群能力差异矩阵
// ================================================================

FH_TEST(Comprehensive_GroupFeatureMatrix) {
    auto owner = mkUser("mf01", "Owner");
    auto admin = mkUser("mf02", "Admin");
    auto member = mkUser("mf03", "Member");
    GroupConfigFH cfg(50, true, false, seconds(120));  // memberInviteEnabled=true

    // QQ 群功能矩阵（先演示特权，再转让群主）
    auto qqG = make_shared<GroupFH>("qqmg", 1001, "QQ功能矩阵", cfg,
                                     make_shared<QQPolicyFH>(), owner);
    qqG->inviteMember(owner, admin);
    qqG->inviteMember(owner, member);
    qqG->setAdmin(owner, admin, true);
    // QQ 群主特权：邀请 / 全员禁言 / 改群名 / 公告 / 任免管理员
    FH_CHECK(qqG->inviteMember(owner, mkUser("mf04")));
    FH_CHECK(qqG->setAllMute(owner, true));
    FH_CHECK(qqG->editGroup(owner, "QQ群改名"));
    FH_CHECK(qqG->publishAnnouncement(owner, "群公告"));
    FH_CHECK(qqG->setAdmin(owner, admin, false));  // 撤销管理员（admin 已存在）
    // 转让群主：owner → admin（admin 变为 OWNER，owner 降级为 MEMBER）
    FH_CHECK(qqG->transferOwner(owner, admin));
    FH_CHECK_EQ(qqG->getRole(admin), GroupRoleFH::OWNER);
    FH_CHECK_EQ(*qqG->getRole(owner), GroupRoleFH::MEMBER);
    // 解散群（由新群主 admin 执行）
    FH_CHECK(qqG->disband(admin));

    // --- 微信群功能矩阵（同一份数据动态切换） ---
    auto wxG = make_shared<GroupFH>("wxmg", 1003, "微信功能矩阵", cfg,
                                     make_shared<WeChatPolicyFH>(), owner);
    wxG->inviteMember(owner, admin);
    wxG->inviteMember(owner, member);
    wxG->setAdmin(owner, admin, true);

    // 微信群：仅群主为特权，管理员无特权
    FH_CHECK(wxG->inviteMember(owner, mkUser("mf05")));        // 仅群主可邀请
    FH_CHECK(!wxG->inviteMember(admin, mkUser("mf06")));       // 管理员不可邀请
    FH_CHECK(!wxG->inviteMember(member, mkUser("mf07")));      // 普通成员不可邀请
    FH_CHECK(wxG->setAllMute(owner, true));                    // 仅群主可设全员禁言
    FH_CHECK(!wxG->setAllMute(admin, true));                   // 管理员不可设
    FH_CHECK(wxG->editGroup(owner, "微信群改名"));             // 仅群主可改群名
    FH_CHECK(wxG->publishAnnouncement(owner, "微信公告"));     // 仅群主可发公告
    FH_CHECK(wxG->transferOwner(owner, admin));                // 仅群主可转让
    FH_CHECK_EQ(wxG->getRole(admin), GroupRoleFH::OWNER);
    FH_CHECK_EQ(*wxG->getRole(owner), GroupRoleFH::MEMBER);
    FH_CHECK(wxG->disband(admin));                             // 新群主可解散
}

// ================================================================
// QQ 管理员在禁言期间的发言权限（QQ 管理员可发言，普通成员不可）
// ================================================================

FH_TEST(Comprehensive_AdminCanSpeakDuringAllMute) {
    auto owner = mkUser("as01", "Owner");
    auto admin = mkUser("as02", "Admin");
    auto member = mkUser("as03", "Member");
    GroupConfigFH cfg(50, false, true, seconds(120));  // 构造时全员禁言
    auto g = make_shared<GroupFH>("asg", 1001, "禁言测试群", cfg,
                                   make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->inviteMember(owner, member);
    g->setAdmin(owner, admin, true);

    // 全员禁言状态：Owner 和 Admin 可发言，Member 不可
    FH_CHECK(g->sendMessage(owner, make_shared<MessageFH>("m1", owner, "owner 发言")));
    FH_CHECK(g->sendMessage(admin, make_shared<MessageFH>("m2", admin, "admin 发言")));
    FH_CHECK(!g->sendMessage(member, make_shared<MessageFH>("m3", member, "member 发言")));
    FH_CHECK_EQ(g->messages().size(), size_t(2));

    // 解除禁言后 Member 可发言
    FH_CHECK(g->setAllMute(owner, false));
    FH_CHECK(g->sendMessage(member, make_shared<MessageFH>("m4", member, "解禁后发言")));
    FH_CHECK_EQ(g->messages().size(), size_t(3));
}

// ================================================================
// 微博关注 ≠ 好友：微博关注关系与 QQ/微信好友完全隔离
// ================================================================

FH_TEST(Comprehensive_WeiboFollowIsolation) {
    UserRegistryFH reg;
    auto a = reg.registerUser("wb01", "微博用户A", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("wb02", "微博用户B", "2000-02-02", "上海", 2019);

    FriendRegistryFH fr;
    // QQ 加好友
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
    // 微博关注 ≠ QQ 好友
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::Weibo));
    FH_CHECK(!fr.isFollowing(*a, *b));
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

}  // namespace

int main() { return ::fhtest::runAll("requirement-alignment-comprehensive"); }
