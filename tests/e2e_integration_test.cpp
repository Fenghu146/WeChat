// ============================================================
// e2e_integration_test.cpp —— 端到端真实性场景验证
// ------------------------------------------------------------
// 模拟真实 IM 客户端操作流程，覆盖：
//   1. 群创建 + 邀请 + 发消息 + 撤回 + 全员禁言 + 解除
//   2. 官方群加入/发信/查记录/退群
//   3. QQ 临时讨论组：邀请/退组/解散
//   4. 跨平台好友隔离
//   5. 登录联动与在线状态
//   6. 群解散后状态一致性
//   7. 全员禁言期间权限验证
//   8. 管理模式切换
//   9. 微信群邀请规则
//   10. 群消息记录上限淘汰
// ============================================================
#include <chrono>
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

using namespace std::chrono;
using std::make_shared;
using std::shared_ptr;

shared_ptr<UserFH> mk(const std::string& id, const std::string& nick = "") {
    return make_shared<UserFH>(id, nick.empty() ? ("昵称" + id) : nick);
}

// ================================================================
// 测试 1：完整群生命周期（创建→邀请→发消息→撤回→全员禁言→解除）
// ================================================================

FH_TEST(EndToEnd_GroupLifeCycle) {
    auto owner = mk("o1", "群主");
    auto admin = mk("a1", "管理员");
    auto member = mk("m1", "成员");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("g1", 1001, "项目组群", cfg,
                                   make_shared<QQPolicyFH>(), owner);

    // 群主自动创建，角色为 OWNER
    FH_CHECK_EQ(g->getRole(owner), GroupRoleFH::OWNER);
    FH_CHECK_EQ(g->members().size(), std::size_t(1));

    // 邀请成员
    FH_CHECK(g->inviteMember(owner, admin));
    FH_CHECK(g->inviteMember(owner, member));
    FH_CHECK_EQ(g->members().size(), std::size_t(3));

    // 任命管理员
    FH_CHECK(g->setAdmin(owner, admin, true));
    FH_CHECK_EQ(g->getRole(admin), GroupRoleFH::ADMIN);

    // 发消息
    auto msg1 = make_shared<MessageFH>("m1", member, "大家好！");
    auto msg2 = make_shared<MessageFH>("m2", owner, "欢迎");
    FH_CHECK(g->sendMessage(member, msg1));
    FH_CHECK(g->sendMessage(owner, msg2));
    FH_CHECK_EQ(g->messages().size(), std::size_t(2));

    // 撤回消息
    FH_CHECK(g->recallMessage(member, msg1->getId()));
    FH_CHECK(msg1->isRecalled());
    FH_CHECK(!g->recallMessage(member, msg1->getId()));  // 已撤回不可再次撤回

    // 全员禁言（QQ admin 可设）
    FH_CHECK(g->setAllMute(admin, true));
    FH_CHECK(g->getConfig().isAllMuted());
    // Member 不能发言
    auto msg3 = make_shared<MessageFH>("m3", member, "禁言中发言");
    FH_CHECK(!g->sendMessage(member, msg3));
    // Owner 可以发言
    auto msg4 = make_shared<MessageFH>("m4", owner, "owner 仍可发言");
    FH_CHECK(g->sendMessage(owner, msg4));
    // 解除全员禁言
    FH_CHECK(g->setAllMute(owner, false));
    FH_CHECK(!g->getConfig().isAllMuted());
    // Member 恢复发言
    auto msg5 = make_shared<MessageFH>("m5", member, "解禁后可发言");
    FH_CHECK(g->sendMessage(member, msg5));
    FH_CHECK_EQ(g->messages().size(), std::size_t(4));  // msg1 已撤回不计入
}

// ================================================================
// 测试 2：官方群全流程（加入/发信/查记录/退群）
// ================================================================

FH_TEST(EndToEnd_OfficialGroupFullFlow) {
    UserRegistryFH reg;
    auto xm = reg.registerUser("10001", "小明", "2000-06-01", "深圳", 2018);
    auto xh = reg.registerUser("10002", "小红", "1999-11-11", "长沙", 2016);
    reg.bindWeChat(xm, "wx-88-0001");
    reg.bindWeChat(xh, "wx-88-0002");

    GroupRegistryFH gr;

    // QQ 群：直接申请加入
    FH_CHECK(gr.joinGroup(*xm, PlatformKindFH::QQ, "1001"));
    FH_CHECK(gr.joinGroup(*xh, PlatformKindFH::QQ, "1001"));
    FH_CHECK(!gr.joinGroup(*xm, PlatformKindFH::QQ, "1001"));  // 重复

    // 微信群只能推荐加入（任务书 3.(3)）：直接申请被拒，
    // 须由群内成员通过 inviteIntoGroup 推荐进入。
    FH_CHECK(!gr.joinGroup(*xm, PlatformKindFH::WeChat, "1003"));
    FH_CHECK(gr.createGroup(*xm, PlatformKindFH::WeChat, "老友群"));
    FH_CHECK(gr.inviteIntoGroup(*xm, *xh, "1007"));   // 群主推荐小红
    FH_CHECK(!gr.inviteIntoGroup(*xh, *xh, "1007"));  // 已在群内

    // 微博群：直接申请加入
    FH_CHECK(gr.joinGroup(*xm, PlatformKindFH::Weibo, "1005"));

    // 发送消息
    FH_CHECK(gr.sendGroupMessage(*xm, PlatformKindFH::QQ, "1001",
                                  MessageKindFH::TEXT, "大家好"));
    FH_CHECK(gr.sendGroupMessage(*xh, PlatformKindFH::QQ, "1001",
                                  MessageKindFH::TEXT, "你好小明"));
    // 微信禁文件
    FH_CHECK(!gr.sendGroupMessage(*xm, PlatformKindFH::WeChat, "1007",
                                   MessageKindFH::FILE, "合同.docx"));
    // 微博禁图片
    FH_CHECK(!gr.sendGroupMessage(*xm, PlatformKindFH::Weibo, "1005",
                                   MessageKindFH::IMAGE, "照片.jpg"));
    // 微博支持文本
    FH_CHECK(gr.sendGroupMessage(*xm, PlatformKindFH::Weibo, "1005",
                                  MessageKindFH::TEXT, "旅行攻略"));

    // 查看聊天记录
    const auto& chatQQ = gr.chatOf("1001");
    FH_CHECK_EQ(chatQQ.size(), std::size_t(2));
    FH_CHECK_EQ(chatQQ[0].content, std::string("大家好"));
    FH_CHECK_EQ(chatQQ[1].content, std::string("你好小明"));

    // 非成员不能发言
    auto luren = reg.registerUser("10003", "路人", "2001-01-01", "成都", 2021);
    FH_CHECK(!gr.sendGroupMessage(*luren, PlatformKindFH::QQ, "1001",
                                   MessageKindFH::TEXT, "我也来"));

    // 退群
    FH_CHECK(gr.leaveGroup(*xm, "1001"));
    FH_CHECK(!gr.leaveGroup(*xm, "1001"));  // 已退
    FH_CHECK(!gr.sendGroupMessage(*xm, PlatformKindFH::QQ, "1001",
                                   MessageKindFH::TEXT, "退群后发言"));
}

// ================================================================
// 测试 3：QQ 临时讨论组完整流程
// ================================================================

FH_TEST(EndToEnd_DiscussionGroupFullFlow) {
    UserRegistryFH reg;
    auto alice = reg.registerUser("d001", "Alice", "2000-01-01", "北京", 2018);
    auto bob = reg.registerUser("d002", "Bob", "2000-02-02", "上海", 2019);
    auto carol = reg.registerUser("d003", "Carol", "2000-03-03", "广州", 2020);

    DiscussionGroupFH dg("dg-001", "周末活动", alice->getQQId());

    // 发起人邀请
    FH_CHECK(dg.invite(*alice, *bob));
    FH_CHECK(dg.invite(*bob, *carol));  // 任何成员可邀请
    FH_CHECK_EQ(dg.size(), std::size_t(3));

    // 自由退组
    FH_CHECK(dg.quit(*bob));
    FH_CHECK_EQ(dg.size(), std::size_t(2));
    FH_CHECK(!dg.quit(*bob));  // 已退出

    // 仅发起人可解散
    FH_CHECK(!dg.disband(*carol));
    FH_CHECK(dg.disband(*alice));
    FH_CHECK(dg.isDisbanded());
    FH_CHECK_EQ(dg.size(), std::size_t(0));
    FH_CHECK(!dg.invite(*alice, *bob));
    FH_CHECK(!dg.disband(*alice));
}

// ================================================================
// 测试 4：跨平台好友隔离
// ================================================================

FH_TEST(EndToEnd_FriendIsolation) {
    UserRegistryFH reg;
    auto alice = reg.registerUser("f001", "Alice", "2000-01-01", "北京", 2018);
    auto bob = reg.registerUser("f002", "Bob", "2000-02-02", "上海", 2019);
    reg.bindWeChat(alice, "wx-a");
    reg.bindWeChat(bob, "wx-b");

    FriendRegistryFH fr;

    // QQ 加好友
    FH_CHECK(fr.makeFriends(*alice, *bob, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*alice, *bob, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*bob, *alice, PlatformKindFH::QQ));

    // QQ 好友不影响微信
    FH_CHECK(!fr.isFriend(*alice, *bob, PlatformKindFH::WeChat));

    // 微信加好友
    FH_CHECK(fr.makeFriends(*alice, *bob, PlatformKindFH::WeChat));
    FH_CHECK(fr.isFriend(*alice, *bob, PlatformKindFH::WeChat));

    // QQ 解好友不影响微信
    FH_CHECK(fr.unfriend(*alice, *bob, PlatformKindFH::QQ));
    FH_CHECK(!fr.isFriend(*alice, *bob, PlatformKindFH::QQ));
    FH_CHECK(fr.isFriend(*alice, *bob, PlatformKindFH::WeChat));

    // 微博关注独立
    FH_CHECK(fr.follow(*alice, *bob));
    FH_CHECK(fr.isFollowing(*alice, *bob));
    FH_CHECK(!fr.isFriend(*alice, *bob, PlatformKindFH::Weibo));
    FH_CHECK(fr.unfollow(*alice, *bob));
    FH_CHECK(!fr.isFollowing(*alice, *bob));
}

// ================================================================
// 测试 5：登录联动与在线状态
// ================================================================

FH_TEST(EndToEnd_LoginLinkage) {
    UserRegistryFH reg;
    auto alice = reg.registerUser("l001", "Alice", "2000-01-01", "北京", 2018);
    reg.bindWeChat(alice, "wx-a");

    ActivationManagerFH act;
    LoginManagerFH login;

    FH_CHECK(act.activate(*alice, PlatformKindFH::QQ));
    FH_CHECK(act.activate(*alice, PlatformKindFH::WeChat));

    // 登录 QQ，微信联动上线
    FH_CHECK(login.login(*alice, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*alice, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*alice, PlatformKindFH::WeChat));

    // 单服务退出
    FH_CHECK(login.logout(*alice, PlatformKindFH::QQ));
    FH_CHECK(!login.isOnline(*alice, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*alice, PlatformKindFH::WeChat));

    // 在线时不能取消开通
    FH_CHECK(!act.deactivate(*alice, PlatformKindFH::WeChat));
    FH_CHECK(login.logout(*alice, PlatformKindFH::WeChat));
    FH_CHECK(act.deactivate(*alice, PlatformKindFH::WeChat));
}

// ================================================================
// 测试 6：群解散后状态一致性
// ================================================================

FH_TEST(EndToEnd_GroupDisbandConsistency) {
    auto owner = mk("o1", "Owner");
    auto member = mk("m1", "Member");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gd", 1001, "解散测试", cfg,
                                   make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, member);

    auto msg = make_shared<MessageFH>("m1", member, "最后的消息");
    FH_CHECK(g->sendMessage(member, msg));
    FH_CHECK_EQ(g->messages().size(), std::size_t(1));

    FH_CHECK(g->disband(owner));
    FH_CHECK(g->isDisbanded());
    FH_CHECK_EQ(g->members().size(), std::size_t(0));
    FH_CHECK_EQ(g->messages().size(), std::size_t(1));  // 消息保留

    // 解散后一切拒绝
    FH_CHECK(!g->sendMessage(owner, msg));
    FH_CHECK(!g->recallMessage(owner, "m1"));
    FH_CHECK(!g->inviteMember(owner, mk("x", "X")));
    FH_CHECK(!g->editGroup(owner, "新名"));
    FH_CHECK(!g->publishAnnouncement(owner, "公告"));
    FH_CHECK(!g->setAllMute(owner, true));
    FH_CHECK(!g->muteMember(owner, member, true));
    FH_CHECK(!g->kickMember(owner, member));
    FH_CHECK(!g->setAdmin(owner, member, true));
    FH_CHECK(!g->transferOwner(owner, member));
    FH_CHECK(!g->disband(owner));
    FH_CHECK(!g->switchPolicy(make_shared<WeChatPolicyFH>()));
}

// ================================================================
// 测试 7：全员禁言期间消息权限
// ================================================================

FH_TEST(EndToEnd_AllMutePermissions) {
    auto owner = mk("om1", "Owner");
    auto admin = mk("a1", "Admin");
    auto member = mk("m1", "Member");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gam", 1001, "全员禁言", cfg,
                                   make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->inviteMember(owner, member);
    g->setAdmin(owner, admin, true);

    FH_CHECK(g->setAllMute(admin, true));  // QQ: admin 可设

    auto ownerMsg = make_shared<MessageFH>("o1", owner, "owner 发言");
    auto adminMsg = make_shared<MessageFH>("a1", admin, "admin 发言");
    FH_CHECK(g->sendMessage(owner, ownerMsg));
    FH_CHECK(g->sendMessage(admin, adminMsg));

    auto memberMsg = make_shared<MessageFH>("m1", member, "member 发言");
    FH_CHECK(!g->sendMessage(member, memberMsg));
    FH_CHECK_EQ(g->messages().size(), std::size_t(2));

    FH_CHECK(g->setAllMute(owner, false));
    FH_CHECK(g->sendMessage(member, memberMsg));
    FH_CHECK_EQ(g->messages().size(), std::size_t(3));
}

// ================================================================
// 测试 8：管理模式切换后权限变化
// ================================================================

FH_TEST(EndToEnd_PolicySwitch) {
    auto owner = mk("p1", "Owner");
    auto admin = mk("a1", "Admin");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("ps", 1001, "模式切换", cfg,
                                   make_shared<QQPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->setAdmin(owner, admin, true);

    // QQ 模式：admin 可设全员禁言
    FH_CHECK(g->setAllMute(admin, true));
    FH_CHECK(g->setAllMute(admin, false));

    // 切换到微信模式
    FH_CHECK(g->switchPolicy(make_shared<WeChatPolicyFH>()));
    // 微信模式：admin 不能设
    FH_CHECK(!g->setAllMute(admin, true));
    // owner 仍可
    FH_CHECK(g->setAllMute(owner, true));
    FH_CHECK(g->setAllMute(owner, false));
}

// ================================================================
// 测试 9：微信群仅群主为特权账号（任务书 3.(3)）
// ================================================================

FH_TEST(EndToEnd_WeChatGroupOwnerOnlyPrivilege) {
    auto owner = mk("wo1", "Owner");
    auto admin = mk("a1", "Admin");
    auto member = mk("m1", "Member");
    auto outsider = mk("x1", "Outsider");
    GroupConfigFH cfg(50, false, false, seconds(120));
    auto g = make_shared<GroupFH>("gwx", 1003, "微信邀请", cfg,
                                   make_shared<WeChatPolicyFH>(), owner);
    g->inviteMember(owner, admin);
    g->inviteMember(owner, member);
    g->setAdmin(owner, admin, true);

    FH_CHECK(!g->inviteMember(member, outsider));   // 普通成员禁止
    FH_CHECK(!g->inviteMember(admin, outsider));    // 管理员亦禁止（无特权）
    FH_CHECK(g->inviteMember(owner, outsider));     // 仅群主可推荐加入
    FH_CHECK(g->inviteMember(owner, mk("x2", "X2")));
}

// ================================================================
// 测试 10：群消息记录上限淘汰
// ================================================================

FH_TEST(EndToEnd_ChatRecordEviction) {
    UserRegistryFH reg;
    auto alice = reg.registerUser("e001", "Alice", "2000-01-01", "北京", 2018);

    GroupRegistryFH gr;
    gr.joinGroup(*alice, PlatformKindFH::QQ, "1001");

    for (int i = 1; i <= 55; ++i) {
        FH_CHECK(gr.sendGroupMessage(*alice, PlatformKindFH::QQ, "1001",
                                      MessageKindFH::TEXT, "msg" + std::to_string(i)));
    }

    const auto& chat = gr.chatOf("1001");
    FH_CHECK_EQ(chat.size(), GroupRegistryFH::kMaxChatRecordsFH);
    FH_CHECK_EQ(chat.front().content, std::string("msg6"));
    FH_CHECK_EQ(chat.back().content, std::string("msg55"));
}

}  // namespace

int main() { return ::fhtest::runAll("e2e-integration"); }
