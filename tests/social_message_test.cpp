// ============================================================
// social_message_test.cpp —— 社交关系 + 群消息扩展回归测试
// 阶段 C：好友/关注平台隔离、群注册表、QQ 临时讨论组；
// 阶段 D：消息类型/长度/引用能力矩阵、群聊记录上限淘汰。
// ============================================================
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "fh_mini_test.hpp"
#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/social/discussion_group_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_registry_fh.hpp"

namespace {

using std::shared_ptr;
using Ptr = shared_ptr<UserProfileFH>;

struct People {
    UserRegistryFH reg;
    Ptr xm;    // 小明：QQ 10001，绑微信 wx-88-0001
    Ptr hong;  // 小红：QQ 10002，绑微信 wx-88-0002
    Ptr luren; // 路人乙：QQ 10003，未绑微信
    Ptr bing;  // 路人丙：QQ 10004

    People() {
        xm = reg.registerUser("10001", "小明", "2006-01-01", "杭州", 2021);
        hong = reg.registerUser("10002", "小红", "2007-02-02", "北京", 2021);
        luren = reg.registerUser("10003", "路人乙", "2008-03-03", "广州", 2021);
        bing = reg.registerUser("10004", "路人丙", "2009-04-04", "深圳", 2021);
        reg.bindWeChat(xm, "wx-88-0001");
        reg.bindWeChat(hong, "wx-88-0002");
    }
};

bool containsId(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// ---------------- 好友 / 关注（阶段 C） ----------------

FH_TEST(QQAndWeChatFriendsAreMutualAndDeduplicated) {
    People p;
    FriendRegistryFH fr;
    FH_CHECK(fr.makeFriends(*p.xm, *p.hong, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*p.xm, *p.hong, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*p.hong, *p.xm, PlatformKindFH::QQ));  // 双向
    FH_CHECK(!fr.makeFriends(*p.xm, *p.hong, PlatformKindFH::QQ));  // 重复拒绝
    FH_CHECK(!fr.makeFriends(*p.xm, *p.xm, PlatformKindFH::QQ));    // 不能加自己
    FH_CHECK(containsId(fr.friendIds(*p.xm, PlatformKindFH::QQ), "10002"));
    // QQ 好友关系不影响微博（平台隔离）
    FH_CHECK(!fr.isFriend(*p.xm, *p.hong, PlatformKindFH::Weibo));
}

FH_TEST(WeChatFriendshipRequiresBothBound) {
    People p;
    FriendRegistryFH fr;
    FH_CHECK(!fr.makeFriends(*p.xm, *p.luren, PlatformKindFH::WeChat));  // 路人乙未绑微信
    FH_CHECK(fr.makeFriends(*p.xm, *p.hong, PlatformKindFH::WeChat));    // 双方已绑
    FH_CHECK(fr.isFriend(*p.xm, *p.hong, PlatformKindFH::WeChat));
    FH_CHECK(!fr.makeFriends(*p.xm, *p.hong, PlatformKindFH::Weibo));    // 微博只能关注
}

FH_TEST(WeiboFollowIsOneWayNotFriend) {
    People p;
    FriendRegistryFH fr;
    FH_CHECK(fr.follow(*p.xm, *p.hong));       // 小明关注小红
    FH_CHECK(fr.isFollowing(*p.xm, *p.hong));
    FH_CHECK(!fr.isFriend(*p.xm, *p.hong, PlatformKindFH::Weibo));  // 关注 ≠ 好友
    FH_CHECK(!fr.isFollowing(*p.hong, *p.xm));  // 单向：小红没有关注小明
    FH_CHECK(!fr.follow(*p.xm, *p.hong));       // 重复关注拒绝
    FH_CHECK(!fr.follow(*p.xm, *p.xm));         // 不能关注自己
    FH_CHECK(fr.unfollow(*p.xm, *p.hong));
    FH_CHECK(!fr.isFollowing(*p.xm, *p.hong));
}

FH_TEST(UnfriendOnQQDoesNotAffectWeChat) {
    People p;
    FriendRegistryFH fr;
    FH_CHECK(fr.makeFriends(*p.xm, *p.hong, PlatformKindFH::QQ));
    FH_CHECK(fr.makeFriends(*p.xm, *p.hong, PlatformKindFH::WeChat));
    FH_CHECK(fr.unfriend(*p.xm, *p.hong, PlatformKindFH::QQ));
    FH_CHECK(!fr.isFriend(*p.xm, *p.hong, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*p.xm, *p.hong, PlatformKindFH::WeChat));  // 平台隔离
}

// ---------------- 群注册表（阶段 C） ----------------

FH_TEST(PredefinedOfficialGroupsExistOnEveryPlatform) {
    GroupRegistryFH gr;
    FH_CHECK_EQ(gr.groupCount(), std::size_t(6));
    FH_CHECK_EQ(gr.groupsOfPlatform(PlatformKindFH::QQ).size(), std::size_t(2));
    FH_CHECK_EQ(gr.groupsOfPlatform(PlatformKindFH::WeChat).size(), std::size_t(2));
    FH_CHECK_EQ(gr.groupsOfPlatform(PlatformKindFH::Weibo).size(), std::size_t(2));
    const GroupInfoFH* g = gr.findGroup("1003");
    FH_CHECK(g != nullptr);
    if (g) FH_CHECK_EQ(g->platform, PlatformKindFH::WeChat);
}

FH_TEST(JoinGroupGateChecksPlatformAndAccount) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.joinGroup(*p.xm, PlatformKindFH::QQ, "1001"));
    FH_CHECK(!gr.joinGroup(*p.xm, PlatformKindFH::QQ, "1001"));       // 重复入群
    FH_CHECK(!gr.joinGroup(*p.xm, PlatformKindFH::QQ, "1003"));       // 平台不匹配
    FH_CHECK(!gr.joinGroup(*p.xm, PlatformKindFH::WeChat, "1001"));   // 群平台不匹配
    FH_CHECK(!gr.joinGroup(*p.luren, PlatformKindFH::WeChat, "1003"));// 无微信号
    FH_CHECK(gr.joinGroup(*p.xm, PlatformKindFH::WeChat, "1003"));    // 绑定后可入
    FH_CHECK(gr.leaveGroup(*p.xm, "1001"));
    FH_CHECK(!gr.leaveGroup(*p.xm, "1001"));  // 已退出
}

FH_TEST(CreateGroupAutoAssignsNumberFrom1007) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.createGroup(*p.xm, PlatformKindFH::QQ, "自建开发群"));
    const GroupInfoFH* g = gr.findGroup("1007");
    FH_CHECK(g != nullptr);
    if (g) FH_CHECK_EQ(g->ownerId, std::string("10001"));
    FH_CHECK(gr.createGroup(*p.xm, PlatformKindFH::QQ, "再建一个"));
    FH_CHECK(gr.findGroup("1008") != nullptr);
    FH_CHECK(!gr.createGroup(*p.luren, PlatformKindFH::WeChat, "无微信建群"));  // 缺平台账号
    auto userGroups = gr.groupsOfUser(*p.xm);
    bool found1007 = false;
    for (const GroupInfoFH* u : userGroups)
        if (u->groupId == "1007") found1007 = true;
    FH_CHECK(found1007);
}

// ---------------- 群消息扩展（阶段 D） ----------------

FH_TEST(PlatformSupportsKindMatrix) {
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::QQ,
                                                   MessageKindFH::FILE));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat,
                                                    MessageKindFH::FILE));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::WeChat,
                                                   MessageKindFH::IMAGE));
    FH_CHECK(!PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo,
                                                    MessageKindFH::IMAGE));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo,
                                                   MessageKindFH::TEXT));
    FH_CHECK(PlatformMessagePolicyFH::supportsKind(PlatformKindFH::Weibo,
                                                   MessageKindFH::EMOJI));
    FH_CHECK_EQ(PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::QQ),
                std::size_t(8000));
    FH_CHECK_EQ(PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::WeChat),
                std::size_t(5000));
    FH_CHECK_EQ(PlatformMessagePolicyFH::maxTextLength(PlatformKindFH::Weibo),
                std::size_t(1000));
    FH_CHECK(PlatformMessagePolicyFH::supportsReply(PlatformKindFH::QQ));
    FH_CHECK(PlatformMessagePolicyFH::supportsReply(PlatformKindFH::WeChat));
    FH_CHECK(!PlatformMessagePolicyFH::supportsReply(PlatformKindFH::Weibo));
}

FH_TEST(SendGroupMessageEnforcesKindLengthAndReply) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.joinGroup(*p.xm, PlatformKindFH::QQ, "1001"));
    FH_CHECK(gr.joinGroup(*p.xm, PlatformKindFH::WeChat, "1003"));
    FH_CHECK(gr.joinGroup(*p.xm, PlatformKindFH::Weibo, "1005"));

    FH_CHECK(!gr.sendGroupMessage(*p.bing, PlatformKindFH::QQ, "1001",
                                  MessageKindFH::TEXT, "非成员发言"));  // 非成员
    // QQ：全类型支持
    FH_CHECK(gr.sendGroupMessage(*p.xm, PlatformKindFH::QQ, "1001",
                                 MessageKindFH::FILE, "架构图.pdf"));
    // 微信：禁文件，允许图片
    FH_CHECK(!gr.sendGroupMessage(*p.xm, PlatformKindFH::WeChat, "1003",
                                  MessageKindFH::FILE, "文件.docx"));
    FH_CHECK(gr.sendGroupMessage(*p.xm, PlatformKindFH::WeChat, "1003",
                                 MessageKindFH::IMAGE, "晚霞.jpg"));
    // 微博：仅文本/表情；不支持引用
    FH_CHECK(!gr.sendGroupMessage(*p.xm, PlatformKindFH::Weibo, "1005",
                                  MessageKindFH::IMAGE, "风景.jpg"));
    FH_CHECK(gr.sendGroupMessage(*p.xm, PlatformKindFH::Weibo, "1005",
                                 MessageKindFH::EMOJI, "[呲牙]"));
    FH_CHECK(!gr.sendGroupMessage(*p.xm, PlatformKindFH::Weibo, "1005",
                                  MessageKindFH::TEXT, "hello", /*asReply=*/true));
    FH_CHECK(gr.sendGroupMessage(*p.xm, PlatformKindFH::QQ, "1001",
                                 MessageKindFH::TEXT, "收到", /*asReply=*/true));
    // 长度上限
    const std::string overWeibo(1001, 'a');
    FH_CHECK(!gr.sendGroupMessage(*p.xm, PlatformKindFH::Weibo, "1005",
                                  MessageKindFH::TEXT, overWeibo));
    const std::string overQQ(8001, 'b');
    FH_CHECK(!gr.sendGroupMessage(*p.xm, PlatformKindFH::QQ, "1001",
                                  MessageKindFH::TEXT, overQQ));
    FH_CHECK_EQ(gr.chatOf("1001").size(), std::size_t(2));
    FH_CHECK_EQ(gr.chatOf("1005").size(), std::size_t(1));
}

FH_TEST(ChatRecordKeepsAtMostFiftyByEvictingOldest) {
    People p;
    GroupRegistryFH gr;
    FH_CHECK(gr.joinGroup(*p.xm, PlatformKindFH::QQ, "1001"));
    for (int i = 1; i <= 55; ++i) {
        FH_CHECK(gr.sendGroupMessage(*p.xm, PlatformKindFH::QQ, "1001",
                                     MessageKindFH::TEXT, "seq" + std::to_string(i)));
    }
    const auto& chat = gr.chatOf("1001");
    FH_CHECK_EQ(chat.size(), GroupRegistryFH::kMaxChatRecordsFH);  // 50 条封顶
    FH_CHECK_EQ(chat.front().content, std::string("seq6"));  // 淘汰 seq1~seq5 共 5 条
    FH_CHECK_EQ(chat.back().content, std::string("seq55"));
}

// ---------------- QQ 临时讨论组（阶段 C） ----------------

FH_TEST(DiscussionGroupAnyMemberCanInviteAndQuitFreely) {
    People p;
    DiscussionGroupFH dg("d1", "临时讨论组", p.xm->getQQId());
    FH_CHECK_EQ(dg.size(), std::size_t(1));  // 创建者天然在组
    FH_CHECK(dg.invite(*p.xm, *p.hong));     // 发起人邀请
    FH_CHECK(dg.invite(*p.hong, *p.luren));  // 任何成员可邀请（区别于正式群）
    FH_CHECK(dg.invite(*p.luren, *p.bing));  // 普通成员再邀请
    FH_CHECK(!dg.invite(*p.hong, *p.luren)); // 已在组拒绝
    FH_CHECK(!dg.invite(*p.hong, *p.hong));  // 不能邀请自己
    FH_CHECK(dg.quit(*p.hong));              // 自由退组
    FH_CHECK(!dg.quit(*p.hong));
    FH_CHECK(!dg.contains("10002"));
}

FH_TEST(DiscussionGroupOnlyCreatorDisbandThenInert) {
    People p;
    DiscussionGroupFH dg("d2", "方案讨论组", p.xm->getQQId());
    FH_CHECK(dg.invite(*p.xm, *p.luren));
    FH_CHECK(!dg.disband(*p.luren));      // 非发起人不能解散
    FH_CHECK(dg.disband(*p.xm));          // 发起人解散
    FH_CHECK(dg.isDisbanded());
    FH_CHECK_EQ(dg.size(), std::size_t(0));
    FH_CHECK(!dg.invite(*p.xm, *p.hong)); // 解散后不可邀请
    FH_CHECK(!dg.disband(*p.xm));         // 二次解散失败
}

FH_TEST(DiscussionGroupCapacityIsSmall) {
    People p;
    DiscussionGroupFH dg("d3", "容量讨论组", p.xm->getQQId());
    FH_CHECK_EQ(dg.capacity(), DiscussionGroupFH::kCapacity);
    for (int i = 0; i < 19; ++i) {
        Ptr extra = p.reg.registerUser("9900" + std::to_string(i),
                                       "成员" + std::to_string(i),
                                       "2010-01-01", "测试", 2022);
        FH_CHECK(dg.invite(*p.xm, *extra));  // 第 2~20 人均可加入
    }
    FH_CHECK_EQ(dg.size(), std::size_t(20));  // 已达容量上限
    Ptr last = p.reg.registerUser("99999", "最后一人", "2010-01-01", "测试", 2022);
    FH_CHECK(!dg.invite(*p.xm, *last));       // 满员拒绝
}

}  // namespace

int main() { return ::fhtest::runAll("social-message"); }
