// ============================================================
// abstract_policy_test.cpp —— 六步授权链 / 公共状态约束（阶段 A）
// ------------------------------------------------------------
// 直接调用 AbstractGroupPolicyFH 派生策略的 isAllowed()，覆盖
// 六步链的前置校验（上下文/成员关系）与 checkStateRules
// （单员/全员禁言、撤回归属与时间窗边界），不依赖群高层方法。
// ============================================================
#include <chrono>
#include <memory>
#include <string>

#include "fh_mini_test.hpp"
#include "im/context/action_fh.hpp"
#include "im/context/group_context_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"

namespace {

using namespace std::chrono;
using std::make_shared;
using std::shared_ptr;

shared_ptr<UserFH> mk(const std::string& id) {
    return make_shared<UserFH>(id, "昵称" + id);
}

// 直接构造一次授权判断上下文
GroupContextFH ctx(GroupFH& g, const shared_ptr<UserFH>& op,
                   shared_ptr<UserFH> target,
                   shared_ptr<MessageFH> message,
                   system_clock::time_point now = system_clock::now()) {
    GroupContextFH c;
    c.group = &g;
    c.operatorUser = op;
    c.target = std::move(target);
    c.message = std::move(message);
    c.now = now;
    return c;
}

struct Rig {
    shared_ptr<UserFH> owner{mk("o1")};
    shared_ptr<UserFH> admin{mk("a1")};
    shared_ptr<UserFH> member{mk("m1")};
    shared_ptr<UserFH> outsider{mk("x1")};
    GroupConfigFH cfg{500, false, false, seconds(120)};

    Rig() = default;
    explicit Rig(GroupConfigFH c) : cfg(std::move(c)) {}

    shared_ptr<GroupFH> qq() { return ready("g-qq", 1001, make_shared<QQPolicyFH>()); }
    shared_ptr<GroupFH> wx() { return ready("g-wx", 1003, make_shared<WeChatPolicyFH>()); }

private:
    shared_ptr<GroupFH> ready(const char* id, unsigned no,
                              shared_ptr<GroupPolicyFH> policy) {
        auto g = make_shared<GroupFH>(std::string(id), no, std::string("测试群") + id,
                                      cfg, std::move(policy), owner);
        g->inviteMember(owner, admin);
        g->inviteMember(owner, member);
        g->setAdmin(owner, admin, true);
        return g;
    }
};

unsigned msgSeq = 0;
shared_ptr<MessageFH> msgOf(const shared_ptr<UserFH>& sender,
                            system_clock::time_point at) {
    return make_shared<MessageFH>("m" + std::to_string(++msgSeq), sender, "hi", at);
}

// ---------------- 步骤 1：上下文合法性 ----------------

FH_TEST(ValidateContextRejectsMissingPieces) {
    Rig rig;
    auto g = rig.qq();
    QQPolicyFH pol;
    // 缺 group
    GroupContextFH c1;
    c1.operatorUser = rig.owner;
    c1.message = make_shared<MessageFH>("a", rig.owner, "hi");
    FH_CHECK(!pol.isAllowed(ActionFH::SEND_MESSAGE, c1));
    // 缺操作者
    GroupContextFH c2;
    c2.group = g.get();
    c2.message = make_shared<MessageFH>("b", rig.owner, "hi");
    FH_CHECK(!pol.isAllowed(ActionFH::SEND_MESSAGE, c2));
    // 发送/撤回缺消息
    auto c3 = ctx(*g, rig.owner, nullptr, nullptr);
    FH_CHECK(!pol.isAllowed(ActionFH::SEND_MESSAGE, c3));
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, c3));
    // 带 target 的操作缺 target
    auto c4 = ctx(*g, rig.owner, nullptr, nullptr);
    FH_CHECK(!pol.isAllowed(ActionFH::INVITE_MEMBER, c4));
    FH_CHECK(!pol.isAllowed(ActionFH::KICK_MEMBER, c4));
    FH_CHECK(!pol.isAllowed(ActionFH::MUTE_MEMBER, c4));
    FH_CHECK(!pol.isAllowed(ActionFH::ASSIGN_ADMIN, c4));
    FH_CHECK(!pol.isAllowed(ActionFH::TRANSFER_OWNER, c4));
}

// ---------------- 步骤 2：成员关系 ----------------

FH_TEST(MembershipStepRejectsOutsiderAndInvalidTarget) {
    Rig rig;
    auto g = rig.qq();
    QQPolicyFH pol;
    // 群外用户发送被拒
    auto c = ctx(*g, rig.outsider, nullptr,
                 make_shared<MessageFH>("m", rig.outsider, "hi"));
    FH_CHECK(!pol.isAllowed(ActionFH::SEND_MESSAGE, c));
    // 邀请“已在群内”的目标被拒（checkMembership 的 INVITE 分支）
    auto inviteExisting = ctx(*g, rig.admin, rig.member, nullptr);
    FH_CHECK(!pol.isAllowed(ActionFH::INVITE_MEMBER, inviteExisting));
    // 邀请群外目标：成员关系通过，留给平台规则决定（QQ 默认关闭 → 普通成员失败）
    auto inviteOut = ctx(*g, rig.member, rig.outsider, nullptr);
    FH_CHECK(!pol.isAllowed(ActionFH::INVITE_MEMBER, inviteOut));
    auto inviteOutByAdmin = ctx(*g, rig.admin, rig.outsider, nullptr);
    FH_CHECK(pol.isAllowed(ActionFH::INVITE_MEMBER, inviteOutByAdmin));
    // 踢 / 禁言“不在群内”的目标被拒
    auto kickOut = ctx(*g, rig.owner, rig.outsider, nullptr);
    FH_CHECK(!pol.isAllowed(ActionFH::KICK_MEMBER, kickOut));
}

// ---------------- 步骤 5：禁言状态约束 ----------------

FH_TEST(StateRulesBlockMutedAndAllMutedMember) {
    Rig rig;
    auto g = rig.qq();
    QQPolicyFH pol;
    // 单员禁言：成员被禁言后发送被拒，管理员不受影响
    FH_CHECK(g->muteMember(rig.admin, rig.member, true));
    auto mutedSend = ctx(*g, rig.member, nullptr,
                         make_shared<MessageFH>("a", rig.member, "hi"));
    FH_CHECK(!pol.isAllowed(ActionFH::SEND_MESSAGE, mutedSend));
    auto adminSend = ctx(*g, rig.admin, nullptr,
                         make_shared<MessageFH>("b", rig.admin, "hi"));
    FH_CHECK(pol.isAllowed(ActionFH::SEND_MESSAGE, adminSend));
    // 全员禁言：仅特权角色可发言（QQ 管理员可设置全员禁言）
    FH_CHECK(g->setAllMute(rig.admin, true));
    auto allMutedMember = ctx(*g, rig.member, nullptr,
                              make_shared<MessageFH>("c", rig.member, "hi"));
    FH_CHECK(!pol.isAllowed(ActionFH::SEND_MESSAGE, allMutedMember));
    auto allMutedOwner = ctx(*g, rig.owner, nullptr,
                             make_shared<MessageFH>("d", rig.owner, "hi"));
    FH_CHECK(pol.isAllowed(ActionFH::SEND_MESSAGE, allMutedOwner));
}

// ---------------- 步骤 5：撤回归属 + 时间窗边界（精确注入 now） ----------------

FH_TEST(RecallWindowBoundaryIsInclusiveAndOwnershipEnforced) {
    Rig rig;
    auto g = rig.qq();
    QQPolicyFH pol;
    const auto t0 = system_clock::now();          // 消息发送时刻
    auto ownMsg = msgOf(rig.member, t0);          // 成员本人的消息
    auto otherMsg = msgOf(rig.owner, t0);         // 群主的消息

    // 窗口 120s：普通成员撤回本人消息，now = t0+120s（恰好在边界上）应允许
    auto inWindow = ctx(*g, rig.member, nullptr, ownMsg, t0 + seconds(120));
    FH_CHECK(pol.isAllowed(ActionFH::RECALL_MESSAGE, inWindow));
    // 超过边界 1s 即拒绝
    auto outWindow = ctx(*g, rig.member, nullptr, ownMsg, t0 + seconds(121));
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, outWindow));
    // 普通成员不能撤回他人消息（即使管理员在场也不代表可代撤）
    auto othersByMember = ctx(*g, rig.member, nullptr, otherMsg, t0);
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, othersByMember));
    // 管理员可撤回任意消息，但同样受时间窗约束
    auto othersByAdmin = ctx(*g, rig.admin, nullptr, otherMsg, t0);
    FH_CHECK(pol.isAllowed(ActionFH::RECALL_MESSAGE, othersByAdmin));
    auto othersByAdminLate = ctx(*g, rig.admin, nullptr, otherMsg,
                                 t0 + seconds(121));
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, othersByAdminLate));
    // 已撤回消息不可再次撤回
    otherMsg->recall();
    auto recalledAgain = ctx(*g, rig.owner, nullptr, otherMsg, t0);
    FH_CHECK(!pol.isAllowed(ActionFH::RECALL_MESSAGE, recalledAgain));
}

// ---------------- 六步链对非差异操作在两种策略下一视同仁 ----------------

FH_TEST(CommonActionsEqualAcrossPlatforms) {
    Rig rig;
    auto qq = rig.qq();
    auto wx = rig.wx();
    QQPolicyFH qPol;
    WeChatPolicyFH wPol;
    auto sendOf = [&](GroupFH& g, GroupPolicyFH& pol,
                      const shared_ptr<UserFH>& who) {
        auto m = make_shared<MessageFH>("m" + std::to_string(++msgSeq), who, "hi");
        return ctx(g, who, nullptr, m);
    };
    // 普通成员发送：两平台都允许
    FH_CHECK(qPol.isAllowed(ActionFH::SEND_MESSAGE, sendOf(*qq, qPol, rig.member)));
    FH_CHECK(wPol.isAllowed(ActionFH::SEND_MESSAGE, sendOf(*wx, wPol, rig.member)));
    // 管理员编辑群名：两平台都允许；普通成员都不允许
    auto editAdminQ = ctx(*qq, rig.admin, nullptr, nullptr);
    auto editAdminW = ctx(*wx, rig.admin, nullptr, nullptr);
    auto editMemberQ = ctx(*qq, rig.member, nullptr, nullptr);
    auto editMemberW = ctx(*wx, rig.member, nullptr, nullptr);
    FH_CHECK(qPol.isAllowed(ActionFH::EDIT_GROUP, editAdminQ));
    FH_CHECK(wPol.isAllowed(ActionFH::EDIT_GROUP, editAdminW));
    FH_CHECK(!qPol.isAllowed(ActionFH::EDIT_GROUP, editMemberQ));
    FH_CHECK(!wPol.isAllowed(ActionFH::EDIT_GROUP, editMemberW));
    // 解散：仅群主
    auto disbandAdminQ = ctx(*qq, rig.admin, nullptr, nullptr);
    auto disbandOwnerW = ctx(*wx, rig.owner, nullptr, nullptr);
    FH_CHECK(!qPol.isAllowed(ActionFH::DISBAND_GROUP, disbandAdminQ));
    FH_CHECK(wPol.isAllowed(ActionFH::DISBAND_GROUP, disbandOwnerW));
}

}  // namespace

int main() { return ::fhtest::runAll("abstract-policy"); }
