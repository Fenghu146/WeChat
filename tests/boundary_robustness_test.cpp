// ============================================================
// boundary_robustness_test.cpp —— 边界条件与鲁棒性测试
// ------------------------------------------------------------
// 覆盖：
//   1. 各平台文本长度上限的精确边界（8000/5000/1000）
//   2. 消息类型 × 平台能力矩阵 + 引用回复能力边界
//   3. 聊天记录上限淘汰边界（恰好 50 / 第 51 条）
//   4. 群人数上限边界（满员拒绝、退群后释放名额）
//   5. 撤回时间窗：0 秒窗、运行期缩短窗口后旧消息超窗
//   6. QQ 临时讨论组：容量 20 边界、解散后全拒绝、非发起人解散
//   7. 好友/关注边界：自加、重复、单向/双向语义、备注清空
//   8. 跨服务推荐全部前置条件
//   9. 用户档案：T 龄边界、空昵称保护、微信号唯一性
//  10. 开通边界：非法平台、重复、未绑定微信、在线不可取消
//  11. 登录与“简单确认”：单服务登录、confirmLink 计数/幂等
//  12. 持久化鲁棒性：空/损坏存档不清空现状、非法消息类型行跳过、
//      特殊字符（换行/反斜杠/字段分隔符）往返
// ============================================================
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fh_mini_test.hpp"
#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_membership_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"
#include "im/platform/activation_manager_fh.hpp"
#include "im/platform/login_manager_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/persist_util_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/social/discussion_group_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_chat_record_fh.hpp"
#include "im/social/group_registry_fh.hpp"

namespace {

using namespace std::chrono;
using std::make_shared;
using std::shared_ptr;

shared_ptr<UserFH> mk(const std::string& id) {
    return make_shared<UserFH>(id, "昵称" + id);
}

// 带持久化路径的群注册表（临时文件由调用方清理）
GroupRegistryFH mkGroupReg(const std::string& path) {
    GroupRegistryFH reg;
    reg.setPersistencePath(path);
    return reg;
}

// ============================================================
// 1. 文本长度精确边界（任务书：平台能力差异的简化口径）
// ============================================================

FH_TEST(MessageLengthExactBoundaries) {
    UserRegistryFH reg;
    auto a = reg.registerUser("1", "A", "2000-01-01", "北京", 2018);
    reg.bindWeChat(a, "wx-1");
    GroupRegistryFH groups;
    FH_CHECK(groups.joinGroup(*a, PlatformKindFH::QQ, "1001"));
    FH_CHECK(groups.joinGroup(*a, PlatformKindFH::Weibo, "1005"));
    FH_CHECK(groups.createGroup(*a, PlatformKindFH::WeChat, "wx群"));
    const std::string wxGid = "1007";  // 自建群从 1007 起编号

    // QQ 上限 8000
    std::string at(8000, 'x');
    std::string over(8001, 'x');
    FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::QQ, "1001",
                                     MessageKindFH::TEXT, at));
    FH_CHECK(!groups.sendGroupMessage(*a, PlatformKindFH::QQ, "1001",
                                      MessageKindFH::TEXT, over));
    // 微信上限 5000
    at.resize(5000);
    over.resize(5001);
    FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::WeChat, wxGid,
                                    MessageKindFH::TEXT, at));
    FH_CHECK(!groups.sendGroupMessage(*a, PlatformKindFH::WeChat, wxGid,
                                     MessageKindFH::TEXT, over));
    // 微博上限 1000
    at.resize(1000);
    over.resize(1001);
    FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::Weibo, "1005",
                                    MessageKindFH::TEXT, at));
    FH_CHECK(!groups.sendGroupMessage(*a, PlatformKindFH::Weibo, "1005",
                                     MessageKindFH::TEXT, over));
}

// ============================================================
// 2. 消息类型 × 平台矩阵 + 引用能力
// ============================================================

FH_TEST(MessageKindPlatformMatrix) {
    // 直接校验策略矩阵
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ, MessageKindFH::TEXT));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ, MessageKindFH::IMAGE));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ, MessageKindFH::DOCUMENT));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ, MessageKindFH::VOICE));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ, MessageKindFH::EMOJI));

    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat, MessageKindFH::TEXT));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat, MessageKindFH::IMAGE));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat, MessageKindFH::DOCUMENT));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat, MessageKindFH::VOICE));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat, MessageKindFH::EMOJI));

    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo, MessageKindFH::TEXT));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo, MessageKindFH::IMAGE));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo, MessageKindFH::DOCUMENT));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo, MessageKindFH::VOICE));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo, MessageKindFH::EMOJI));

    // 非法平台一律不支持
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(
        static_cast<PlatformKindFH>(99), MessageKindFH::TEXT));
    FH_CHECK_EQ(PlatformMessagePolicyFH::maxTextLength(
        static_cast<PlatformKindFH>(99)), std::size_t(0));
    FH_CHECK(!PlatformMessagePolicyFH::supportsReply(
        static_cast<PlatformKindFH>(99)));

    // 引用能力：QQ/微信支持，微博不支持
    FH_CHECK(PlatformMessagePolicyFH::supportsReply(PlatformKindFH::QQ));
    FH_CHECK(PlatformMessagePolicyFH::supportsReply(PlatformKindFH::WeChat));
    FH_CHECK(!PlatformMessagePolicyFH::supportsReply(PlatformKindFH::Weibo));
}

FH_TEST(ReplyRejectedOnWeiboButAllowedElsewhere) {
    UserRegistryFH reg;
    auto a = reg.registerUser("2", "B", "2000-01-01", "北京", 2018);
    reg.bindWeChat(a, "wx-2");
    GroupRegistryFH groups;
    FH_CHECK(groups.joinGroup(*a, PlatformKindFH::QQ, "1001"));
    FH_CHECK(groups.joinGroup(*a, PlatformKindFH::Weibo, "1005"));
    FH_CHECK(groups.createGroup(*a, PlatformKindFH::WeChat, "wx群"));

    FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::QQ, "1001",
                                    MessageKindFH::TEXT, "正文", true));
    FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::WeChat, "1007",
                                    MessageKindFH::TEXT, "正文", true));
    FH_CHECK(!groups.sendGroupMessage(*a, PlatformKindFH::Weibo, "1005",
                                      MessageKindFH::TEXT, "正文", true));
}

// ============================================================
// 3. 聊天记录上限淘汰边界
// ============================================================

FH_TEST(ChatRecordEvictionBoundary) {
    UserRegistryFH reg;
    auto a = reg.registerUser("3", "C", "2000-01-01", "北京", 2018);
    GroupRegistryFH groups;
    FH_CHECK(groups.joinGroup(*a, PlatformKindFH::QQ, "1001"));

    // 连发 50 条：恰好满，不淘汰
    for (int i = 1; i <= 50; ++i)
        FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::QQ, "1001",
                                         MessageKindFH::TEXT,
                                         "msg" + std::to_string(i)));
    FH_CHECK_EQ(groups.chatOf("1001").size(),
                GroupRegistryFH::kMaxChatRecordsFH);
    FH_CHECK_EQ(groups.chatOf("1001").front().content, std::string("msg1"));
    FH_CHECK_EQ(groups.chatOf("1001").back().content, std::string("msg50"));

    // 第 51 条：淘汰最早一条
    FH_CHECK(groups.sendGroupMessage(*a, PlatformKindFH::QQ, "1001",
                                     MessageKindFH::TEXT, "msg51"));
    FH_CHECK_EQ(groups.chatOf("1001").size(),
                GroupRegistryFH::kMaxChatRecordsFH);
    FH_CHECK_EQ(groups.chatOf("1001").front().content, std::string("msg2"));
    FH_CHECK_EQ(groups.chatOf("1001").back().content, std::string("msg51"));
}

// ============================================================
// 4. 群人数上限边界（GroupFH 聚合根）
// ============================================================

FH_TEST(MemberLimitBoundaryWithSlotRelease) {
    auto owner = mk("o1");
    auto m1 = mk("m1");
    auto m2 = mk("m2");
    auto m3 = mk("m3");
    GroupConfigFH cfg(2, false, false, seconds(120));  // 上限 2
    auto g = make_shared<GroupFH>("glim", 1001, "边界群", cfg,
                                  make_shared<QQPolicyFH>(), owner);
    FH_CHECK_EQ(g->members().size(), std::size_t(1));  // 群主
    FH_CHECK(g->inviteMember(owner, m1));              // 第 2 人：恰好满员
    FH_CHECK(!g->inviteMember(owner, m2));             // 满员拒绝
    FH_CHECK_EQ(g->members().size(), std::size_t(2));

    // m1 退群释放名额后，m2 可加入
    FH_CHECK(g->leaveGroup(m1));
    FH_CHECK(g->inviteMember(owner, m2));
    FH_CHECK_EQ(g->members().size(), std::size_t(2));

    // 单人群：上限 1 时仅群主可存在
    auto g1 = make_shared<GroupFH>("g1x", 1002, "单人群",
                                   GroupConfigFH(1, false, false, seconds(120)),
                                   make_shared<QQPolicyFH>(), owner);
    FH_CHECK(!g1->inviteMember(owner, m1));
}

// ============================================================
// 5. 撤回时间窗：0 秒窗口与运行期缩短
// ============================================================

FH_TEST(RecallWindowZeroAndShrinkAtRuntime) {
    auto owner = mk("o1");
    // 0 秒窗口：仅“now == sentAt”可通过（<= 边界）
    GroupConfigFH cfg0(50, false, false, seconds(0));
    auto g0 = make_shared<GroupFH>("g0", 1001, "零窗群", cfg0,
                                   make_shared<QQPolicyFH>(), owner);
    const auto t0 = system_clock::now();
    auto m0 = make_shared<MessageFH>("r0", owner, "零窗消息", t0);
    FH_CHECK(g0->sendMessage(owner, m0));
    GroupContextFH ctx;
    ctx.group = g0.get();
    ctx.operatorUser = owner;
    ctx.message = m0;
    ctx.now = t0;  // 恰好等于发送时刻
    QQPolicyFH pol;
    FH_CHECK(pol.isAllowed(ActionFH::RECALL_MESSAGE, ctx));
    ctx.now = t0 + milliseconds(1);
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, ctx));

    // 运行期缩短窗口：120s → 5s，旧消息随即超窗
    auto g1 = make_shared<GroupFH>("g1", 1002, "缩窗群",
                                   GroupConfigFH(50, false, false, seconds(120)),
                                   make_shared<QQPolicyFH>(), owner);
    const auto t1 = system_clock::now() - seconds(100);
    auto m1 = make_shared<MessageFH>("r1", owner, "旧消息", t1);
    FH_CHECK(g1->sendMessage(owner, m1));  // 发送不看时间窗
    FH_CHECK(g1->recallMessage(owner, "r1"));  // 100s < 120s：可撤回
    FH_CHECK(m1->isRecalled());

    auto m2 = make_shared<MessageFH>("r2", owner, "另一条旧消息", t1);
    FH_CHECK(g1->sendMessage(owner, m2));
    FH_CHECK(g1->setRecallTimeLimit(owner, seconds(5)));  // 群主运行期改窗
    FH_CHECK_EQ(g1->getConfig().getRecallTimeLimit(), seconds(5));
    FH_CHECK(!g1->recallMessage(owner, "r2"));  // 100s > 5s：超窗拒绝
    // 非法窗口（负数）抛异常，配置保持不变
    bool threw = false;
    try { g1->setRecallTimeLimit(owner, seconds(-1)); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    FH_CHECK_EQ(g1->getConfig().getRecallTimeLimit(), seconds(5));
}

// ============================================================
// 6. QQ 临时讨论组边界
// ============================================================

FH_TEST(DiscussionGroupCapacityAndDisbandRules) {
    UserRegistryFH reg;
    auto creator = reg.registerUser("c1", "发起人", "2000-01-01", "北京", 2018);
    auto dg = make_shared<DiscussionGroupFH>("dg", "讨论组", creator->getQQId());
    FH_CHECK_EQ(dg->size(), std::size_t(1));  // 发起人天然在组

    // 容量 20：发起人 + 19 人；第 20 个邀请被拒
    std::vector<shared_ptr<UserProfileFH>> members;
    for (int i = 1; i <= 19; ++i) {
        auto m = reg.registerUser("m" + std::to_string(i), "成员" + std::to_string(i),
                                  "2000-01-01", "北京", 2018);
        members.push_back(m);
        FH_CHECK(dg->invite(*creator, *m));
    }
    FH_CHECK_EQ(dg->size(), DiscussionGroupFH::kCapacity);
    auto extra = reg.registerUser("extra", "额外", "2000-01-01", "北京", 2018);
    FH_CHECK(!dg->invite(*creator, *extra));  // 满员拒绝

    // 普通成员也可邀请（讨论组特性），但满员时同样被拒
    FH_CHECK(!dg->invite(*members[0], *extra));
    FH_CHECK(!dg->invite(*extra, *members[0]));          // 邀请者不在组内
    FH_CHECK(!dg->invite(*creator, *creator));          // 自己邀请自己
    FH_CHECK(!dg->invite(*members[0], *members[0]));    // 已在组内

    // 非发起人不能解散
    FH_CHECK(!dg->disband(*members[0]));
    FH_CHECK(!dg->disband(*extra));
    FH_CHECK(!dg->isDisbanded());

    // 解散后：一切操作拒绝
    FH_CHECK(dg->disband(*creator));
    FH_CHECK(dg->isDisbanded());
    FH_CHECK_EQ(dg->size(), std::size_t(0));
    FH_CHECK(!dg->disband(*creator));  // 重复解散
    FH_CHECK(!dg->invite(*creator, *extra));
    FH_CHECK(!dg->quit(*creator));
}

FH_TEST(DiscussionGroupCreatorCanQuitThenDisband) {
    UserRegistryFH reg;
    auto creator = reg.registerUser("c2", "发起人2", "2000-01-01", "北京", 2018);
    auto m = reg.registerUser("m2", "成员2", "2000-01-01", "北京", 2018);
    auto dg = make_shared<DiscussionGroupFH>("dg2", "讨论组2", creator->getQQId());
    FH_CHECK(dg->invite(*creator, *m));
    // 发起人也允许自由退组（讨论组特性），退组后仍是唯一可解散者
    FH_CHECK(dg->quit(*creator));
    FH_CHECK(!dg->contains(creator->getQQId()));
    FH_CHECK(!dg->disband(*m));      // 剩余成员不能解散
    FH_CHECK(dg->disband(*creator));  // 发起人仍可解散
    FH_CHECK(dg->isDisbanded());
}

FH_TEST(DiscussionGroupRejectsEmptyArguments) {
    bool threw = false;
    try { DiscussionGroupFH d("", "N", "c"); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { DiscussionGroupFH d("id", "", "c"); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { DiscussionGroupFH d("id", "N", ""); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

// ============================================================
// 7. 好友 / 关注边界
// ============================================================

FH_TEST(FriendRegistryEdgeCases) {
    UserRegistryFH reg;
    auto a = reg.registerUser("101", "甲", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("102", "乙", "2000-02-02", "上海", 2018);
    reg.bindWeChat(a, "wx-101");
    reg.bindWeChat(b, "wx-102");
    FriendRegistryFH fr;

    // 自加 / 重复 / 跨平台账号缺失
    FH_CHECK(!fr.makeFriends(*a, *a, PlatformKindFH::QQ));            // 自己
    FH_CHECK(!fr.makeFriends(*a, *b, PlatformKindFH::Weibo));         // 微博请走关注
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(!fr.makeFriends(*a, *b, PlatformKindFH::QQ));            // 重复
    FH_CHECK(!fr.makeFriends(*b, *a, PlatformKindFH::QQ));            // 反向重复
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::WeChat));    // 双方已绑定：可添加
    FH_CHECK(!fr.makeFriends(*a, *b, PlatformKindFH::WeChat));   // 重复添加被拒

    // 未绑定微信的人不能建立微信好友
    auto c = reg.registerUser("103", "丙", "2000-03-03", "广州", 2018);
    FH_CHECK(!fr.makeFriends(*a, *c, PlatformKindFH::WeChat));

    // 删除：非好友 / 单向关系不可“删好友”
    FH_CHECK(fr.follow(*a, *c));  // 微博单向
    FH_CHECK(!fr.unfriend(*a, *c, PlatformKindFH::Weibo));  // 关注不是好友
    FH_CHECK(!fr.unfriend(*c, *b, PlatformKindFH::QQ));     // 非好友
    FH_CHECK(fr.unfriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::WeChat));   // 平台隔离

    // 备注：非好友不可备注；空串清空备注
    FH_CHECK(!fr.setRemark(*a, *c, PlatformKindFH::QQ, "x"));  // 非好友
    auto d = reg.registerUser("104", "丁", "2000-04-04", "深圳", 2018);
    fr.makeFriends(*a, *d, PlatformKindFH::QQ);
    FH_CHECK(fr.setRemark(*a, *d, PlatformKindFH::QQ, "老铁"));
    FH_CHECK_EQ(fr.remarkOf(*a, *d, PlatformKindFH::QQ), std::string("老铁"));
    FH_CHECK(fr.setRemark(*a, *d, PlatformKindFH::QQ, ""));  // 允许清空
    FH_CHECK_EQ(fr.remarkOf(*a, *d, PlatformKindFH::QQ), std::string(""));

    // 微博关注边界
    FH_CHECK(!fr.follow(*c, *c));            // 自关注
    FH_CHECK(!fr.follow(*c, *c));           // 重复（无 edge 时同样拒绝）
    FH_CHECK(!fr.unfollow(*c, *a));         // 未关注不可取消
}

// ============================================================
// 8. 跨服务推荐的全部前置条件
// ============================================================

FH_TEST(RecommendationAllPreconditions) {
    UserRegistryFH reg;
    auto me = reg.registerUser("201", "我", "2000-01-01", "北京", 2018);
    auto p1 = reg.registerUser("202", "朋友", "2000-02-02", "上海", 2018);
    auto p2 = reg.registerUser("203", "无微信号", "2000-03-03", "广州", 2018);
    auto p3 = reg.registerUser("204", "非好友", "2000-04-04", "深圳", 2018);
    reg.bindWeChat(me, "wx-201");
    reg.bindWeChat(p1, "wx-202");
    reg.bindWeChat(p3, "wx-204");  // p2 不绑定
    ActivationManagerFH act;
    act.activate(*me, PlatformKindFH::QQ);
    act.activate(*me, PlatformKindFH::WeChat);
    FriendRegistryFH fr;
    fr.makeFriends(*me, *p1, PlatformKindFH::QQ);
    fr.makeFriends(*me, *p2, PlatformKindFH::QQ);
    fr.makeFriends(*me, *p3, PlatformKindFH::WeChat);  // 微信已是好友

    FH_CHECK(!fr.isRecommendable(*me, *me, PlatformKindFH::QQ, PlatformKindFH::WeChat));       // 自己
    FH_CHECK(!fr.isRecommendable(*me, *p1, PlatformKindFH::QQ, PlatformKindFH::QQ));          // 同平台
    FH_CHECK(!fr.isRecommendable(*me, *p3, PlatformKindFH::QQ, PlatformKindFH::WeChat));      // 来源平台非好友
    FH_CHECK(!fr.isRecommendable(*me, *p2, PlatformKindFH::QQ, PlatformKindFH::WeChat));      // 对方无微信号
    FH_CHECK(!fr.isRecommendable(*me, *p3, PlatformKindFH::WeChat, PlatformKindFH::WeChat));  // 目标平台已是好友
    FH_CHECK(fr.isRecommendable(*me, *p1, PlatformKindFH::QQ, PlatformKindFH::WeChat));       // 唯一满足

    // 未开通来源 / 目标服务时不可推荐（任务书 6.(3)：“本人开通的”服务之间）
    auto me2 = reg.registerUser("205", "未开通", "2000-05-05", "北京", 2018);
    reg.bindWeChat(me2, "wx-205");
    fr.makeFriends(*me2, *p1, PlatformKindFH::QQ);
    FH_CHECK(!fr.isRecommendable(*me2, *p1, PlatformKindFH::QQ, PlatformKindFH::WeChat));

    // 推荐添加成功一次后，重复推荐失败
    FH_CHECK(fr.addFriendFromRecommendation(*me, *p1, PlatformKindFH::QQ, PlatformKindFH::WeChat));
    FH_CHECK(!fr.addFriendFromRecommendation(*me, *p1, PlatformKindFH::QQ, PlatformKindFH::WeChat));

    // 推荐列表只包含真正可推荐的人
    auto list = fr.recommendFriendsFrom(*me, reg, PlatformKindFH::QQ, PlatformKindFH::WeChat);
    FH_CHECK_EQ(list.size(), std::size_t(0));  // p1 已加完，无人可推
}

// ============================================================
// 9. 用户档案边界
// ============================================================

FH_TEST(UserProfileTAgeAndBindingEdgeCases) {
    UserRegistryFH reg;
    auto a = reg.registerUser("301", "A", "2000-01-01", "北京", 2018);
    // T 龄：当年为 0，未来到时为 0，过去按差值
    FH_CHECK_EQ(a->tAge(2018), 0);
    FH_CHECK_EQ(a->tAge(2017), 0);
    FH_CHECK_EQ(a->tAge(2026), 8);

    // 空昵称不允许修改（构造时同样拒绝空昵称）
    a->setNickname("");
    FH_CHECK_EQ(a->getNickname(), std::string("A"));
    bool threw = false;
    try { UserFH bad("", "空"); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);

    // 微信号唯一性（档案级与注册表级双重防线）
    FH_CHECK(!a->bindWeChat(""));                 // 档案级：空号
    FH_CHECK(reg.bindWeChat(a, "wx-301"));        // 走注册表绑定
    FH_CHECK(!a->bindWeChat("wx-other"));         // 档案已绑定
    auto b = reg.registerUser("302", "B", "2000-02-02", "上海", 2018);
    FH_CHECK(!reg.bindWeChat(b, "wx-301"));       // 注册表级全局唯一
    FH_CHECK(!reg.bindWeChat(b, ""));            // 空号
    FH_CHECK(!reg.bindWeChat(nullptr, "wx-x"));  // 空指针

    // 未绑定微信：微信账号解析为空
    FH_CHECK(!b->hasPlatformAccount(PlatformKindFH::WeChat));
    FH_CHECK(b->platformAccountId(PlatformKindFH::WeChat).empty());
    // 未绑定微信时生成账号视图抛异常
    threw = false;
    try { reg.makeAccount(*b, PlatformKindFH::WeChat); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);

    // 主号重复注册被拒
    threw = false;
    try { reg.registerUser("301", "重复", "2000-01-01", "北京", 2018); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

// ============================================================
// 10. 开通边界
// ============================================================

FH_TEST(ActivationBoundaryConditions) {
    UserRegistryFH reg;
    auto a = reg.registerUser("401", "A", "2000-01-01", "北京", 2018);
    ActivationManagerFH act;

    FH_CHECK(!act.activate(*a, static_cast<PlatformKindFH>(99)));  // 非法平台
    FH_CHECK(act.activate(*a, PlatformKindFH::QQ));
    FH_CHECK(!act.activate(*a, PlatformKindFH::QQ));              // 重复开通
    FH_CHECK(!act.activate(*a, PlatformKindFH::WeChat));          // 未绑定微信
    FH_CHECK(!act.deactivate(*a, PlatformKindFH::Weibo));         // 未开通不可取消

    reg.bindWeChat(a, "wx-401");
    FH_CHECK(act.activate(*a, PlatformKindFH::WeChat));
    FH_CHECK_EQ(act.activatedCount(*a), 2);

    // 在线不可取消
    LoginManagerFH login;
    FH_CHECK(login.login(*a, PlatformKindFH::QQ));
    FH_CHECK(!act.deactivate(*a, PlatformKindFH::QQ));
    FH_CHECK(login.logout(*a, PlatformKindFH::QQ));
    FH_CHECK(act.deactivate(*a, PlatformKindFH::QQ));
    FH_CHECK_EQ(act.activatedCount(*a), 1);
}

// ============================================================
// 11. 登录与“简单确认”（任务书第 5 点）
// ============================================================

FH_TEST(LoginOnlyTargetsServiceUntilConfirmed) {
    UserRegistryFH reg;
    auto a = reg.registerUser("501", "A", "2000-01-01", "北京", 2018);
    reg.bindWeChat(a, "wx-501");
    ActivationManagerFH act;
    FH_CHECK(act.activate(*a, PlatformKindFH::QQ));
    FH_CHECK(act.activate(*a, PlatformKindFH::WeChat));
    FH_CHECK(act.activate(*a, PlatformKindFH::Weibo));
    LoginManagerFH login;

    // 未开通不可登录
    auto b = reg.registerUser("502", "B", "2000-02-02", "上海", 2018);
    FH_CHECK(!login.login(*b, PlatformKindFH::QQ));
    FH_CHECK_EQ(login.onlinePlatforms(*b).size(), std::size_t(0));

    // 登录只上线目标服务；简单确认后才联动其余已开通服务
    FH_CHECK(login.login(*a, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*a, PlatformKindFH::QQ));
    FH_CHECK(!login.isOnline(*a, PlatformKindFH::WeChat));
    FH_CHECK(!login.isOnline(*a, PlatformKindFH::Weibo));
    FH_CHECK_EQ(login.confirmLink(*a), 2);  // 微信 + 微博新上线
    FH_CHECK_EQ(login.onlinePlatforms(*a).size(), std::size_t(3));
    FH_CHECK_EQ(login.confirmLink(*a), 0);  // 幂等

    // 部分离线后确认只补离线服务
    FH_CHECK(login.logout(*a, PlatformKindFH::Weibo));
    FH_CHECK_EQ(login.confirmLink(*a), 1);

    // 退出：单服务 / 全部
    FH_CHECK(login.logout(*a, PlatformKindFH::WeChat));
    FH_CHECK(!login.logout(*a, PlatformKindFH::WeChat));  // 已离线再退
    login.logoutAll(*a);
    FH_CHECK_EQ(login.onlinePlatforms(*a).size(), std::size_t(0));
    FH_CHECK_EQ(login.confirmLink(*a), 3);  // 确认可重新拉起全部

    // confirmLink 对未开通任何服务的用户返回 0
    FH_CHECK_EQ(login.confirmLink(*b), 0);
}

// ============================================================
// 12. 持久化鲁棒性
// ============================================================

FH_TEST(PersistenceSurvivesEmptyOrCorruptedSave) {
    const std::string groupPath = "test_groups_corrupt.dat";
    const std::string friendPath = "test_friends_corrupt.dat";

    // 群注册表：空文件 → 保留预置群；损坏文件 → 保留预置群
    {
        std::ofstream(groupPath) << "";
        GroupRegistryFH reg;
        FH_CHECK(!reg.loadFromFile(groupPath));
        FH_CHECK_EQ(reg.groupCount(), std::size_t(6));  // 预置群仍在
        FH_CHECK(reg.findGroup("1001") != nullptr);
    }
    {
        std::ofstream out(groupPath, std::ios::binary);
        out << "JUNK-LINE-1\037\037garbage\n";
        out << "G\037BADPLATFORM\0371\037\n";           // 非法平台名
        out << "G\037QQ\0372\037短行\n";                // 字段不足
        out << "G\037QQ\0373\037名字\037owner\037abc\0371\037-\037-\n";  // maxMembers 非数字
        out << "\n";
        out.close();
        GroupRegistryFH reg;
        FH_CHECK(!reg.loadFromFile(groupPath));  // 无有效 G 行：保持现状
        FH_CHECK_EQ(reg.groupCount(), std::size_t(6));
        FH_CHECK(reg.findGroup("1003") != nullptr);
        std::remove(groupPath.c_str());
    }

    // 好友注册表：空文件 → 保持现状（空），不崩溃
    {
        std::ofstream(friendPath) << "";
        FriendRegistryFH fr;
        FH_CHECK(!fr.loadFromFile(friendPath));
        FH_CHECK_EQ(fr.edgeCount(), std::size_t(0));
        // 损坏文件：跳过非法行，保留有效行
        std::ofstream out(friendPath, std::ios::binary);
        out << "X\037GARBAGE\n";
        out.close();
        FH_CHECK(!fr.loadFromFile(friendPath));
        FH_CHECK_EQ(fr.edgeCount(), std::size_t(0));
        std::remove(friendPath.c_str());
    }
}

FH_TEST(PersistenceSpecialCharactersRoundTrip) {
    const std::string groupPath = "test_groups_special.dat";
    const std::string friendPath = "test_friends_special.dat";
    const std::string actPath = "test_activation_special.dat";
    const std::string specialName = "群名\037含分隔符\\和\n换行";
    const std::string specialMsg = "内容\037含分隔符\\与\n换行";
    const std::string specialRemark = "备注\\含\n换行";
    // 清理上一轮可能残留的存档（析构写回发生在函数末尾的 remove 之前）
    std::remove(groupPath.c_str());
    std::remove(friendPath.c_str());
    std::remove(actPath.c_str());

    // 进程一：造数据；容器在作用域结束（析构）时写回文件
    {
        UserRegistryFH regA;
        auto a = regA.registerUser("601", "甲", "2000-01-01", "北京", 2018);
        auto b = regA.registerUser("602", "乙", "2000-02-02", "上海", 2018);
        regA.bindWeChat(a, "wx-601");
        regA.bindWeChat(b, "wx-602");
        regA.setActivationPath(actPath);
        ActivationManagerFH actA;
        actA.activate(*a, PlatformKindFH::QQ);
        actA.activate(*a, PlatformKindFH::WeChat);

        FriendRegistryFH frA;
        frA.setPersistencePath(friendPath);
        frA.makeFriends(*a, *b, PlatformKindFH::QQ);
        frA.makeFriends(*a, *b, PlatformKindFH::WeChat);
        FH_CHECK(frA.setRemark(*a, *b, PlatformKindFH::QQ, specialRemark));

        GroupRegistryFH grA;
        grA.setPersistencePath(groupPath);
        FH_CHECK(grA.createGroup(*a, PlatformKindFH::QQ, specialName, 50));
        FH_CHECK(grA.joinGroup(*b, PlatformKindFH::QQ, "1007"));
        FH_CHECK(grA.setGroupAdmin(*a, *b, "1007", true));
        FH_CHECK(grA.sendGroupMessage(*a, PlatformKindFH::QQ, "1007",
                                      MessageKindFH::TEXT, specialMsg, true));
    }  // 析构：三个容器写回存档

    // 进程二：全新容器从文件读入（模拟重启）
    {
        UserRegistryFH regB;
        auto a2 = regB.registerUser("601", "甲", "2000-01-01", "北京", 2018);
        auto b2 = regB.registerUser("602", "乙", "2000-02-02", "上海", 2018);
        regB.bindWeChat(a2, "wx-601");
        regB.bindWeChat(b2, "wx-602");
        FH_CHECK(regB.setActivationPath(actPath));
        FH_CHECK(a2->isActivated(PlatformKindFH::QQ));
        FH_CHECK(a2->isActivated(PlatformKindFH::WeChat));
        FH_CHECK(!a2->isActivated(PlatformKindFH::Weibo));

        FriendRegistryFH frB;
        FH_CHECK(frB.setPersistencePath(friendPath));
        FH_CHECK(frB.isFriend(*a2, *b2, PlatformKindFH::QQ));
        FH_CHECK(frB.isFriend(*a2, *b2, PlatformKindFH::WeChat));
        FH_CHECK_EQ(frB.remarkOf(*a2, *b2, PlatformKindFH::QQ), specialRemark);

        GroupRegistryFH grB;
        FH_CHECK(grB.setPersistencePath(groupPath));
        const auto* g = grB.findGroup("1007");
        FH_CHECK(g != nullptr);
        if (g) {
            FH_CHECK_EQ(g->name, specialName);
            FH_CHECK_EQ(g->ownerId, std::string("601"));
            FH_CHECK_EQ(g->memberIds.size(), std::size_t(2));
            FH_CHECK(grB.isAdminOf(*b2, "1007"));
            const auto& chat = grB.chatOf("1007");
            FH_CHECK_EQ(chat.size(), std::size_t(1));
            if (!chat.empty()) {
                FH_CHECK_EQ(chat.front().content, specialMsg);
                FH_CHECK_EQ(chat.front().senderNick, std::string("甲"));
                FH_CHECK(chat.front().isReply);
            }
        }
        // 自建群号续编：已存在 1007 → 下一个自建群号为 1008
        GroupRegistryFH grC;
        FH_CHECK(grC.setPersistencePath(groupPath));
        auto c = regB.registerUser("603", "丙", "2000-03-03", "广州", 2018);
        FH_CHECK(grC.createGroup(*c, PlatformKindFH::QQ, "新群"));
        FH_CHECK(grC.findGroup("1008") != nullptr);
    }  // 进程二的容器析构（写回存档）

    std::remove(groupPath.c_str());
    std::remove(friendPath.c_str());
    std::remove(actPath.c_str());
}

FH_TEST(PersistenceMalformedChatRecordSkipped) {
    const std::string groupPath = "test_groups_badmsg.dat";
    std::remove(groupPath.c_str());  // 清理上一轮残留，避免污染
    {
        UserRegistryFH reg;
        auto a = reg.registerUser("701", "A", "2000-01-01", "北京", 2018);
        GroupRegistryFH gr;
        gr.setPersistencePath(groupPath);
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::QQ, "坏消息群"));
        FH_CHECK(gr.sendGroupMessage(*a, PlatformKindFH::QQ, "1007",
                                     MessageKindFH::TEXT, "正常消息"));
    }  // gr 析构：写回有效存档（G 行 + 1 条 M 行）
    // 向存档追加损坏的 M 行：非法 kind、时间戳非数字、群号不匹配
    {
        std::ofstream out(groupPath, std::ios::app | std::ios::binary);
        out << "M\0371007\03799\037701\037X\037bad\0370\037123\n";  // kind 非法
        out << "M\0371007\0370\037701\037X\037bad\0370\037not-a-number\n";  // 时间戳非法
        out << "M\0379999\0370\037701\037X\037bad\0370\037123\n";  // 群号不匹配
    }
    {
        UserRegistryFH reg2;
        reg2.registerUser("701", "A", "2000-01-01", "北京", 2018);
        GroupRegistryFH gr2;
        FH_CHECK(gr2.setPersistencePath(groupPath));
        const auto* g = gr2.findGroup("1007");
        FH_CHECK(g != nullptr);
        if (g) {
            FH_CHECK_EQ(g->chat.size(), std::size_t(1));  // 三条坏记录全部被跳过
            if (!g->chat.empty())
                FH_CHECK_EQ(g->chat.front().content, std::string("正常消息"));
        }
    }  // gr2 析构（写回存档，但三条坏记录已被跳过，不会累积）
    std::remove(groupPath.c_str());
}

}  // namespace

int main() { return ::fhtest::runAll("boundary-robustness"); }
