// ============================================================
// requirement_alignment_test.cpp —— 对照课程设计文档的针对性验证
// ------------------------------------------------------------
// 逐条对照《2026-面向对象程序课程设计.md》第一节题目描述与
// “优化提高层次”要求，验证实现与文档对齐：
//   1.(1)(2) 用户基本信息：ID 体系（QQ/微博同号、微信独立绑定）、
//            资料（昵称/出生/T龄/所在地）、好友列表与群列表
//   2.(1)    好友管理：添加/修改（备注）/删除/查询
//   2.(2)    微X 之间共同好友查询
//   2.(2)+6.(3) 跨服务推荐添加好友（微信添加 QQ 推荐好友）
//   3.(1)    预置群号 1001~1006
//   3.(2)    加入群、退出群、挨踢、查询群成员
//   3.(3)    QQ 群可申请加入 / 微信群只能推荐加入；
//            QQ 群管理员制度 / 微信群仅群主为特权账号；
//            QQ 群允许临时讨论组（微信群不允许）
//   6.(1)+优化(2) 开通服务情况、群成员信息、好友信息存文件，
//            容器配置时读入、析构时写回（断电保存）
// ============================================================
#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "fh_mini_test.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
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
using std::size_t;
using std::string;

shared_ptr<UserFH> mkUser(const string& id, const string& nick) {
    return make_shared<UserFH>(id, nick);
}

// 文档 1.(1)：号码 ID 体系 + 基本资料 + 好友列表/群列表
FH_TEST(Doc_1_1_UserBasicInfoAndLists) {
    UserRegistryFH reg;
    auto u = reg.registerUser("u001", "小明", "2000-06-01", "深圳", 2018);
    auto v = reg.registerUser("u002", "小红", "1999-11-11", "长沙", 2016);

    // QQ 与微博共享 ID；微信独立、需绑定
    FH_CHECK_EQ(u->getWeiboId(), u->getQQId());
    FH_CHECK_EQ(v->getWeiboId(), v->getQQId());
    FH_CHECK(!u->hasWeChatAccount());
    FH_CHECK(reg.bindWeChat(u, "wx-u1"));
    FH_CHECK(u->hasWeChatAccount());
    FH_CHECK_EQ(u->getWeChatId(), string("wx-u1"));

    // 基本资料与 T 龄（号码申请时间）
    FH_CHECK_EQ(u->getNickname(), string("小明"));
    FH_CHECK_EQ(u->getBirthday(), string("2000-06-01"));
    FH_CHECK_EQ(u->getLocation(), string("深圳"));
    FH_CHECK_EQ(u->tAge(2026), 8);
    u->setNickname("大明");
    u->setLocation("北京");
    FH_CHECK_EQ(u->getNickname(), string("大明"));
    FH_CHECK_EQ(u->getLocation(), string("北京"));

    // 好友列表
    FriendRegistryFH fr;
    FH_CHECK(fr.makeFriends(*u, *v, PlatformKindFH::QQ));
    const auto friendsOfU = fr.friendIds(*u, PlatformKindFH::QQ);
    FH_CHECK_EQ(friendsOfU.size(), size_t(1));
    FH_CHECK(!friendsOfU.empty() && friendsOfU[0] == v->getQQId());

    // 群列表（GroupRegistryFH::groupsOfUser）
    GroupRegistryFH gr;
    FH_CHECK(gr.joinGroup(*u, PlatformKindFH::QQ, "1001"));
    const auto groupsOfU = gr.groupsOfUser(*u);
    FH_CHECK_EQ(groupsOfU.size(), size_t(1));
    FH_CHECK(!groupsOfU.empty() && groupsOfU[0]->groupId == "1001");
}

// 文档 2.(1)：好友信息添加、修改（备注）、删除、查询
FH_TEST(Doc_2_1_FriendRemarkModification) {
    UserRegistryFH reg;
    auto a = reg.registerUser("f001", "甲", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("f002", "乙", "2000-02-02", "上海", 2019);
    auto c = reg.registerUser("f003", "丙", "2000-03-03", "广州", 2020);
    FriendRegistryFH fr;

    // 添加与查询
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));

    // 修改：备注名（视角属于设置者）
    FH_CHECK(fr.setRemark(*a, *b, PlatformKindFH::QQ, "老同学"));
    FH_CHECK_EQ(fr.remarkOf(*a, *b, PlatformKindFH::QQ), string("老同学"));
    FH_CHECK_EQ(fr.remarkOf(*b, *a, PlatformKindFH::QQ), string(""));
    FH_CHECK(fr.setRemark(*b, *a, PlatformKindFH::QQ, "同桌"));
    FH_CHECK_EQ(fr.remarkOf(*b, *a, PlatformKindFH::QQ), string("同桌"));

    // 修改：非好友不可备注
    FH_CHECK(!fr.setRemark(*a, *c, PlatformKindFH::QQ, "陌生人"));
    // 微博关注不是好友，不支持备注
    FH_CHECK(fr.follow(*a, *b));
    FH_CHECK(!fr.setRemark(*a, *b, PlatformKindFH::Weibo, "关注的人"));

    // 删除与查询
    FH_CHECK(fr.unfriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(!fr.isFriend(*a, *b, PlatformKindFH::QQ));
    FH_CHECK_EQ(fr.remarkOf(*a, *b, PlatformKindFH::QQ), string(""));
}

// 文档 2.(2)：查询微X 之间各自共同好友（QQ/微信共同好友、微博共同关注）
FH_TEST(Doc_2_2_CommonFriendsAndFollowing) {
    UserRegistryFH reg;
    auto a = reg.registerUser("c001", "甲", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("c002", "乙", "2000-02-02", "上海", 2019);
    auto c = reg.registerUser("c003", "丙", "2000-03-03", "广州", 2020);
    auto d = reg.registerUser("c004", "丁", "2000-04-04", "深圳", 2021);
    reg.bindWeChat(a, "wx-c1");
    reg.bindWeChat(b, "wx-c2");
    reg.bindWeChat(c, "wx-c3");
    FriendRegistryFH fr;

    // QQ 共同好友：甲-丙、乙-丙 → 共同好友 = {丙}
    FH_CHECK(fr.makeFriends(*a, *c, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*b, *c, PlatformKindFH::QQ));
    const auto commonQQ = fr.commonFriends(*a, *b, PlatformKindFH::QQ);
    FH_CHECK_EQ(commonQQ.size(), size_t(1));
    FH_CHECK(!commonQQ.empty() && commonQQ[0] == c->getQQId());

    // 微信共同好友：微信关系独立成册（好友列表存该平台账号号码，
    // 微信侧为微信号）
    FH_CHECK(fr.makeFriends(*a, *c, PlatformKindFH::WeChat));
    FH_CHECK(fr.makeFriends(*b, *c, PlatformKindFH::WeChat));
    const auto commonWx = fr.commonFriends(*a, *b, PlatformKindFH::WeChat);
    FH_CHECK_EQ(commonWx.size(), size_t(1));
    FH_CHECK(!commonWx.empty() && commonWx[0] == c->getWeChatId());

    // 微博“共同关注”
    FH_CHECK(fr.follow(*a, *c));
    FH_CHECK(fr.follow(*b, *c));
    FH_CHECK(fr.follow(*a, *d));
    FH_CHECK(fr.follow(*b, *d));
    const auto commonFollow = fr.commonFollowing(*a, *b);
    FH_CHECK_EQ(commonFollow.size(), size_t(2));
}

// 文档 2.(2) + 6.(3)：跨服务推荐添加好友（微信添加 QQ 推荐好友）
FH_TEST(Doc_2_2_6_3_CrossPlatformRecommendation) {
    UserRegistryFH reg;
    auto a = reg.registerUser("r001", "甲", "2000-01-01", "北京", 2018);
    auto b = reg.registerUser("r002", "乙", "2000-02-02", "上海", 2019);
    auto c = reg.registerUser("r003", "丙", "2000-03-03", "广州", 2020);
    auto e = reg.registerUser("r005", "戊", "2000-05-05", "杭州", 2022);
    auto f = reg.registerUser("r006", "己", "2000-06-06", "成都", 2023);
    reg.bindWeChat(a, "wx-r1");
    reg.bindWeChat(b, "wx-r2");
    reg.bindWeChat(c, "wx-r3");
    reg.bindWeChat(e, "wx-r5");
    FriendRegistryFH fr;

    // 甲的 QQ 好友 = {乙, 丙, 己}；微信好友 = {}（丁戊未加）
    FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*a, *c, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*a, *f, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*b, *c, PlatformKindFH::WeChat));  // 干扰项

    // 本人须已开通来源/目标服务（任务书 6.(3)）：丙有 QQ 好友甲，
    // 但丙未开通 QQ/微信 → 不可依据 QQ 好友推荐微信好友
    FH_CHECK(!fr.isRecommendable(*c, *a, PlatformKindFH::QQ,
                                 PlatformKindFH::WeChat));
    ActivationManagerFH act;
    FH_CHECK(act.activate(*c, PlatformKindFH::QQ));  // 只开通来源
    FH_CHECK(!fr.isRecommendable(*c, *a, PlatformKindFH::QQ,
                                 PlatformKindFH::WeChat));  // 目标未开通
    FH_CHECK(act.activate(*c, PlatformKindFH::WeChat));
    // 甲开通 QQ/微信后，推荐链路可用
    FH_CHECK(act.activate(*a, PlatformKindFH::QQ));
    FH_CHECK(act.activate(*a, PlatformKindFH::WeChat));

    // 推荐列表：依据 QQ 好友在微信可推荐 → {乙, 丙}（己未绑定微信）
    const auto rec = fr.recommendFriendsFrom(*a, reg, PlatformKindFH::QQ,
                                             PlatformKindFH::WeChat);
    FH_CHECK_EQ(rec.size(), size_t(2));

    // 一键添加：微信添加 QQ 推荐好友
    FH_CHECK(fr.addFriendFromRecommendation(*a, *b, PlatformKindFH::QQ,
                                            PlatformKindFH::WeChat));
    FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::WeChat));

    // 已是微信好友 → 不可重复推荐
    FH_CHECK(!fr.isRecommendable(*a, *b, PlatformKindFH::QQ,
                                 PlatformKindFH::WeChat));
    // QQ 非好友不可推荐
    FH_CHECK(!fr.isRecommendable(*a, *e, PlatformKindFH::QQ,
                                 PlatformKindFH::WeChat));
    // 对方缺少微信账号不可推荐
    FH_CHECK(!fr.isRecommendable(*a, *f, PlatformKindFH::QQ,
                                 PlatformKindFH::WeChat));
    // 同平台无“跨服务推荐”
    FH_CHECK(!fr.isRecommendable(*a, *c, PlatformKindFH::QQ,
                                 PlatformKindFH::QQ));
}

// 文档 3.(1)：每个微X 已有 1001~1006 预置群号
FH_TEST(Doc_3_1_PredefinedGroups) {
    GroupRegistryFH gr;
    const std::pair<const char*, PlatformKindFH> expect[] = {
        {"1001", PlatformKindFH::QQ},    {"1002", PlatformKindFH::QQ},
        {"1003", PlatformKindFH::WeChat},{"1004", PlatformKindFH::WeChat},
        {"1005", PlatformKindFH::Weibo}, {"1006", PlatformKindFH::Weibo},
    };
    for (const auto& [id, plat] : expect) {
        const GroupInfoFH* g = gr.findGroup(id);
        FH_CHECK(g != nullptr);
        if (g) {
            FH_CHECK(g->predefined);
            FH_CHECK_EQ(g->platform, plat);
        }
    }
    FH_CHECK_EQ(gr.groupCount(), size_t(6));
}

// 文档 3.(2)：加入群、退出群、挨踢、查询群成员；
// 文档 3.(3)：QQ 群管理员制度 vs 微信群仅群主特权
FH_TEST(Doc_3_2_3_3_GroupJoinKickQueryAndAdmin) {
    UserRegistryFH reg;
    auto owner = reg.registerUser("k001", "群主", "2000-01-01", "北京", 2018);
    auto admin = reg.registerUser("k002", "管理", "2000-02-02", "上海", 2019);
    auto m1 = reg.registerUser("k003", "成员一", "2000-03-03", "广州", 2020);
    auto m2 = reg.registerUser("k004", "成员二", "2000-04-04", "深圳", 2021);
    auto wxOwner = reg.registerUser("k005", "微群主", "2000-05-05", "杭州", 2022);
    auto wxM = reg.registerUser("k006", "微成员", "2000-06-06", "成都", 2023);
    reg.bindWeChat(wxOwner, "wx-k5");
    reg.bindWeChat(wxM, "wx-k6");

    GroupRegistryFH gr;
    // QQ 自建群：申请加入 + 管理员制度
    FH_CHECK(gr.createGroup(*owner, PlatformKindFH::QQ, "踢人测试群"));  // 1007
    FH_CHECK(gr.joinGroup(*admin, PlatformKindFH::QQ, "1007"));
    FH_CHECK(gr.joinGroup(*m1, PlatformKindFH::QQ, "1007"));
    FH_CHECK(gr.joinGroup(*m2, PlatformKindFH::QQ, "1007"));
    FH_CHECK(gr.setGroupAdmin(*owner, *admin, "1007", true));
    FH_CHECK(gr.isAdminOf(*admin, "1007"));
    FH_CHECK(gr.isOwnerOf(*owner, "1007"));

    // 查询群成员
    const std::vector<std::string>* ids = gr.memberIdsOf("1007");
    FH_CHECK(ids != nullptr && ids->size() == 4);

    // 挨踢矩阵（QQ 群：群主 > 管理员 > 普通成员）
    FH_CHECK(!gr.kickMember(*m1, *m2, "1007"));   // 普通成员不可踢
    FH_CHECK(gr.kickMember(*admin, *m2, "1007")); // 管理员可踢普通成员
    FH_CHECK(!gr.kickMember(*admin, *owner, "1007")); // 不可踢群主
    FH_CHECK(gr.kickMember(*owner, *admin, "1007"));  // 群主可踢管理员
    FH_CHECK(!gr.isAdminOf(*admin, "1007"));          // 被踢即摘除管理
    ids = gr.memberIdsOf("1007");
    FH_CHECK(ids != nullptr && ids->size() == 2);

    // 微信群：仅群主可踢（微信群仅有群主为特权账号）
    FH_CHECK(gr.createGroup(*wxOwner, PlatformKindFH::WeChat, "微信踢人群"));  // 1008
    FH_CHECK(gr.inviteIntoGroup(*wxOwner, *wxM, "1008"));
    FH_CHECK(!gr.kickMember(*wxM, *wxOwner, "1008"));  // 成员不可踢
    FH_CHECK(gr.kickMember(*wxOwner, *wxM, "1008"));   // 仅群主可踢
    // 微信群无管理员制度：setGroupAdmin 仅限 QQ 群
    FH_CHECK(gr.inviteIntoGroup(*wxOwner, *wxM, "1008"));
    FH_CHECK(!gr.setGroupAdmin(*wxOwner, *wxM, "1008", true));

    // 微信群管理员制度缺失时群主仍可退出群
    FH_CHECK(gr.leaveGroup(*wxM, "1008"));
}

// 文档 3.(3)：QQ 群可以申请加入，微信群只能推荐加入；
// QQ 群允许临时讨论组（微信群不允许）
FH_TEST(Doc_3_3_JoinRulesAndDiscussionGroupQQOnly) {
    UserRegistryFH reg;
    auto xm = reg.registerUser("j001", "小明", "2000-06-01", "深圳", 2018);
    auto xh = reg.registerUser("j002", "小红", "1999-11-11", "长沙", 2016);
    auto wx3 = reg.registerUser("j003", "小刚", "2001-03-03", "南京", 2022);
    auto noWx = reg.registerUser("j004", "路人", "2001-04-04", "西安", 2023);
    reg.bindWeChat(xm, "wx-j1");
    reg.bindWeChat(xh, "wx-j2");
    reg.bindWeChat(wx3, "wx-j3");

    GroupRegistryFH gr;
    // QQ 群：直接申请加入
    FH_CHECK(gr.joinGroup(*xm, PlatformKindFH::QQ, "1001"));
    // 微信群：直接申请被拒（只能推荐加入）
    FH_CHECK(!gr.joinGroup(*xm, PlatformKindFH::WeChat, "1003"));
    // 官方微信群尚无成员 → 无人可推荐
    FH_CHECK(!gr.inviteIntoGroup(*xm, *xh, "1003"));
    // 自建微信群 → 群内成员可推荐
    FH_CHECK(gr.createGroup(*xm, PlatformKindFH::WeChat, "家人群"));  // 1007
    FH_CHECK(gr.inviteIntoGroup(*xm, *xh, "1007"));
    FH_CHECK(gr.inviteIntoGroup(*xh, *wx3, "1007"));  // 被推荐者亦可推荐他人
    // 未绑定微信号者不可被推荐入微信群
    FH_CHECK(!gr.inviteIntoGroup(*xm, *noWx, "1007"));
    // QQ 群不走推荐路径（申请制）
    FH_CHECK(!gr.inviteIntoGroup(*xm, *xh, "1001"));
    // 容量满员不可推荐
    FH_CHECK(gr.createGroup(*xm, PlatformKindFH::WeChat, "两人小群", 1));  // 1008
    FH_CHECK(!gr.inviteIntoGroup(*xm, *xh, "1008"));

    // 临时讨论组仅存在于 QQ：成员以 QQ 号入册，微信号不参与
    DiscussionGroupFH dg("dg-9", "作业组会", xm->getQQId());
    FH_CHECK(dg.contains(xm->getQQId()));
    FH_CHECK(!dg.contains(xm->getWeChatId()));
    FH_CHECK(dg.invite(*xm, *xh));
    FH_CHECK(dg.contains(xh->getQQId()));
    FH_CHECK(!dg.contains(xh->getWeChatId()));
}

// 文档 3.(3)：QQ 群管理员制度 / 微信群仅群主特权（策略层 · 六步链）
FH_TEST(Doc_3_3_WeChatGroupOwnerOnlyPrivilege) {
    GroupConfigFH cfg(50, false, false, std::chrono::seconds(120));

    // 微信群：仅群主可邀请（推荐加入）；管理员/普通成员均无特权
    auto wOwner = mkUser("o1", "群主");
    auto wAdmin = mkUser("a1", "管理员");
    auto wMember = mkUser("m1", "成员");
    auto wOut = mkUser("x1", "局外人");
    GroupFH wxg("gwx", 1003, "微信群", cfg, make_shared<WeChatPolicyFH>(),
                wOwner);
    FH_CHECK(wxg.inviteMember(wOwner, wAdmin));
    FH_CHECK(wxg.inviteMember(wOwner, wMember));
    FH_CHECK(wxg.setAdmin(wOwner, wAdmin, true));
    FH_CHECK(!wxg.inviteMember(wMember, wOut));
    FH_CHECK(!wxg.inviteMember(wAdmin, wOut));
    FH_CHECK(wxg.inviteMember(wOwner, wOut));
    // 全员禁言同样仅群主
    FH_CHECK(!wxg.setAllMute(wAdmin, true));
    FH_CHECK(wxg.setAllMute(wOwner, true));

    // 对照：QQ 群管理员拥有邀请与禁言特权
    auto qOwner = mkUser("o2", "群主");
    auto qAdmin = mkUser("a2", "管理员");
    auto qOut = mkUser("x2", "局外人");
    GroupFH qqg("gqq", 1001, "QQ群", cfg, make_shared<QQPolicyFH>(), qOwner);
    FH_CHECK(qqg.inviteMember(qOwner, qAdmin));
    FH_CHECK(qqg.setAdmin(qOwner, qAdmin, true));
    FH_CHECK(qqg.inviteMember(qAdmin, qOut));
    FH_CHECK(qqg.setAllMute(qAdmin, true));
}

// 文档 6.(1) + 优化(2)：开通服务情况、群成员信息、好友信息
// “保存到文件 → 容器配置（实例化）时读入 → 析构时写回”
FH_TEST(Doc_6_1_PersistenceRoundTrip) {
    const string friendPath = "persist_friends_rt.tmp";
    const string groupPath = "persist_groups_rt.tmp";
    const string actPath = "persist_activation_rt.tmp";
    std::remove(friendPath.c_str());
    std::remove(groupPath.c_str());
    std::remove(actPath.c_str());

    // —— 作用域一：构建数据，容器析构时写回文件 ——
    {
        UserRegistryFH reg;
        auto a = reg.registerUser("p001", "甲", "2000-01-01", "北京", 2018);
        auto b = reg.registerUser("p002", "乙", "2000-02-02", "上海", 2019);
        reg.bindWeChat(a, "wx-p1");
        reg.bindWeChat(b, "wx-p2");
        reg.setActivationPath(actPath);  // 文件尚不存在：读入为空，析构写回

        ActivationManagerFH act;
        FH_CHECK(act.activate(*a, PlatformKindFH::QQ));
        FH_CHECK(act.activate(*a, PlatformKindFH::WeChat));
        FH_CHECK(act.activate(*b, PlatformKindFH::Weibo));

        FriendRegistryFH fr;
        fr.setPersistencePath(friendPath);  // 文件尚不存在：读入为空
        FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::QQ));
        FH_CHECK(fr.makeFriends(*a, *b, PlatformKindFH::WeChat));
        FH_CHECK(fr.setRemark(*a, *b, PlatformKindFH::QQ, "老同学"));

        GroupRegistryFH gr;
        gr.setPersistencePath(groupPath);
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::QQ, "持久化群"));    // 1007
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::WeChat, "微信持久群"));  // 1008
        FH_CHECK(gr.inviteIntoGroup(*a, *b, "1008"));
        FH_CHECK(gr.sendGroupMessage(*a, PlatformKindFH::QQ, "1007",
                                      MessageKindFH::TEXT, "断电保存"));
        // 离开作用域：三个容器析构 → 写回文件
    }

    // —— 作用域二：全新容器实例化，从文件读入恢复 ——
    {
        UserRegistryFH reg;
        auto a = reg.registerUser("p001", "甲", "2000-01-01", "北京", 2018);
        auto b = reg.registerUser("p002", "乙", "2000-02-02", "上海", 2019);
        reg.bindWeChat(a, "wx-p1");  // 先重建平台账号，再加载关系数据
        reg.bindWeChat(b, "wx-p2");

        // 开通服务情况：实例化时读入
        FH_CHECK(reg.setActivationPath(actPath));
        FH_CHECK(a->isActivated(PlatformKindFH::QQ));
        FH_CHECK(a->isActivated(PlatformKindFH::WeChat));
        FH_CHECK(!a->isActivated(PlatformKindFH::Weibo));
        FH_CHECK(b->isActivated(PlatformKindFH::Weibo));
        FH_CHECK(!b->isActivated(PlatformKindFH::QQ));

        // 好友信息（含备注）
        FriendRegistryFH fr;
        FH_CHECK(fr.setPersistencePath(friendPath));
        FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
        FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::WeChat));
        FH_CHECK_EQ(fr.remarkOf(*a, *b, PlatformKindFH::QQ), string("老同学"));

        // 群成员信息（目录/群主/管理员/成员/聊天记录）与群号续编
        GroupRegistryFH gr;
        FH_CHECK(gr.setPersistencePath(groupPath));
        const GroupInfoFH* g7 = gr.findGroup("1007");
        FH_CHECK(g7 != nullptr);
        if (g7) {
            FH_CHECK_EQ(g7->name, string("持久化群"));
            FH_CHECK_EQ(g7->ownerId, string("p001"));
            FH_CHECK_EQ(g7->chat.size(), size_t(1));
        }
        FH_CHECK(gr.isOwnerOf(*a, "1007"));
        FH_CHECK(gr.isOwnerOf(*a, "1008"));
        const auto* m8 = gr.memberIdsOf("1008");
        FH_CHECK(m8 != nullptr && m8->size() == 2);
        // 自建群号从“现有最大群号+1”继续
        FH_CHECK(gr.createGroup(*a, PlatformKindFH::QQ, "续建群"));
        FH_CHECK(gr.findGroup("1009") != nullptr);
    }

    // —— 作用域三：容器实例化时读入（构造即加载，任务书优化(2)） ——
    {
        UserRegistryFH reg;
        auto a = reg.registerUser("p001", "甲", "2000-01-01", "北京", 2018);
        auto b = reg.registerUser("p002", "乙", "2000-02-02", "上海", 2019);
        reg.bindWeChat(a, "wx-p1");
        reg.bindWeChat(b, "wx-p2");

        // 构造时直接传入存档路径：好友/群注册表自包含，实例化即恢复
        FriendRegistryFH fr(friendPath);
        GroupRegistryFH gr(groupPath);
        FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::QQ));
        FH_CHECK(fr.isFriend(*a, *b, PlatformKindFH::WeChat));
        const GroupInfoFH* g7 = gr.findGroup("1007");
        FH_CHECK(g7 != nullptr);
        if (g7) FH_CHECK_EQ(g7->ownerId, string("p001"));
        FH_CHECK(gr.isOwnerOf(*a, "1007"));
        FH_CHECK(gr.findGroup("1009") != nullptr);  // 作用域二的续建群已写回
    }

    std::remove(friendPath.c_str());
    std::remove(groupPath.c_str());
    std::remove(actPath.c_str());
}

// 文档 5 + 6.(2)：一个服务登录，其它服务自动登录（联动）
FH_TEST(Doc_5_6_2_LoginLinkageAutoOnline) {
    UserRegistryFH reg;
    auto u = reg.registerUser("L001", "联动", "2000-01-01", "北京", 2018);
    reg.bindWeChat(u, "wx-l1");
    ActivationManagerFH act;
    FH_CHECK(act.activate(*u, PlatformKindFH::QQ));
    FH_CHECK(act.activate(*u, PlatformKindFH::WeChat));

    LoginManagerFH login;
    // 登录 QQ → 已开通的微信自动进入在线（简单确认即自动登录）
    FH_CHECK(login.login(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::WeChat));
    FH_CHECK(login.logout(*u, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*u, PlatformKindFH::WeChat));
}

}  // namespace

int main() { return ::fhtest::runAll("requirement-alignment"); }
