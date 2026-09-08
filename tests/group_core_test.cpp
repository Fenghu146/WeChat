// ============================================================
// group_core_test.cpp —— 领域实体 + GroupFH 聚合根回归测试（阶段 A）
// 覆盖：构造校验、单群主、成员唯一/人数上限、禁言状态、撤回归属与
// 时间窗、任免管理员、转让群主、解散后不可操作、管理模式切换。
// ============================================================
#include <chrono>
#include <memory>
#include <string>

#include "fh_mini_test.hpp"
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

// 每个用例的独立测试环境：owner/admin/member 三成员群 + 一名群外路人
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
        g->setAdmin(owner, admin, true);  // owner / admin / member
        return g;
    }
};

shared_ptr<MessageFH> msgs(const std::string& id, const shared_ptr<UserFH>& sender,
                           const std::string& content = "hi") {
    return make_shared<MessageFH>(id, sender, content);
}

// ---------------- 1. 构造期校验（非法参数抛异常） ----------------

FH_TEST(InvalidEntityArgumentsThrow) {
    bool threw = false;
    try { UserFH u("", "n"); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { UserFH u("id", ""); } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);

    threw = false;
    try { GroupConfigFH bad(0, false, false, seconds(120)); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { GroupConfigFH bad(500, false, false, seconds(-1)); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);

    auto owner = mk("o1");
    auto cfg = GroupConfigFH(50, false, false, seconds(120));
    threw = false;
    try { GroupFH g("", 1, "群", cfg, make_shared<QQPolicyFH>(), owner); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { GroupFH g("g1", 1, "", cfg, make_shared<QQPolicyFH>(), owner); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { GroupFH g("g1", 1, "群", cfg, nullptr, owner); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
    threw = false;
    try { GroupFH g("g1", 1, "群", cfg, make_shared<QQPolicyFH>(), nullptr); }
    catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

// ---------------- 2. 单群主与角色归属 ----------------

FH_TEST(OwnerAssignedOnConstructionAndStaysUnique) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK_EQ(g->members().size(), std::size_t(3));
    auto r = g->getRole(rig.owner);
    FH_CHECK(r.has_value());
    if (r) FH_CHECK_EQ(*r, GroupRoleFH::OWNER);
    // 群主本人不可再以成员身份入群
    FH_CHECK(!g->inviteMember(rig.owner, rig.owner));
    // 增加一名普通成员后群内仍只有一个 OWNER
    FH_CHECK(g->inviteMember(rig.owner, rig.outsider));
    FH_CHECK_EQ(g->getRole(rig.outsider), GroupRoleFH::MEMBER);
    FH_CHECK_EQ(g->getRole(rig.owner), GroupRoleFH::OWNER);
}

// ---------------- 3. 成员唯一性与人数上限 ----------------

FH_TEST(DuplicateMemberCannotJoinTwice) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->inviteMember(rig.owner, rig.admin));   // 已在群内
    FH_CHECK(!g->inviteMember(rig.owner, rig.member));  // 已在群内
    FH_CHECK_EQ(g->members().size(), std::size_t(3));
}

FH_TEST(MemberLimitIsEnforced) {
    Rig rig;
    GroupConfigFH cfg(3, false, false, seconds(120));  // 上限 3：owner+2
    auto g = make_shared<GroupFH>("g1", 1001, "小群", cfg,
                                  make_shared<QQPolicyFH>(), rig.owner);
    FH_CHECK(g->inviteMember(rig.owner, rig.member));   // 第 2 人
    FH_CHECK(g->inviteMember(rig.owner, rig.outsider)); // 第 3 人：已满
    auto extra = mk("o9");
    FH_CHECK(!g->inviteMember(rig.owner, extra));       // 满员拒绝
    FH_CHECK_EQ(g->members().size(), std::size_t(3));
    // 无 ID 用户不可入群
    FH_CHECK(!g->inviteMember(rig.owner, make_shared<UserFH>()));
}

// ---------------- 4. 群外用户被拒绝 ----------------

FH_TEST(OutsideUserRejectedForAllOperations) {
    Rig rig;
    auto g = rig.qq();
    auto msg = msgs("o1", rig.outsider);
    FH_CHECK(!g->sendMessage(rig.outsider, msg));
    FH_CHECK(!g->inviteMember(rig.outsider, rig.member));
    FH_CHECK(!g->kickMember(rig.outsider, rig.member));
    FH_CHECK(!g->editGroup(rig.outsider, "改名"));
    FH_CHECK(!g->publishAnnouncement(rig.outsider, "公告"));
    FH_CHECK(!g->setAllMute(rig.outsider, true));
    FH_CHECK(!g->setAdmin(rig.outsider, rig.member, true));
    FH_CHECK(!g->transferOwner(rig.outsider, rig.admin));
    FH_CHECK(!g->disband(rig.outsider));
    FH_CHECK(!g->recallMessage(rig.outsider, "any-id"));
}

// ---------------- 5. 发消息：发送者必须本人 ----------------

FH_TEST(SendMessageMustBeSentByItself) {
    Rig rig;
    auto g = rig.qq();
    auto forged = msgs("f1", rig.owner);           // 消息发送者是 owner
    FH_CHECK(!g->sendMessage(rig.member, forged)); // 成员替别人发：拒绝
    FH_CHECK_EQ(g->messages().size(), std::size_t(0));
    auto own = msgs("f2", rig.member);
    FH_CHECK(g->sendMessage(rig.member, own));
    FH_CHECK_EQ(g->messages().size(), std::size_t(1));
}

// ---------------- 6. 禁言：单员禁言 + 全员禁言 均只约束普通成员 ----------------

FH_TEST(MutedMemberCannotSpeakUntilUnmuted) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->muteMember(rig.member, rig.admin, true));   // 普通成员无权限
    FH_CHECK(g->muteMember(rig.admin, rig.member, true));    // 管理员可禁言成员
    FH_CHECK(g->isMuted(rig.member));
    FH_CHECK(!g->sendMessage(rig.member, msgs("s1", rig.member)));  // 被禁言不能发言
    FH_CHECK(g->sendMessage(rig.admin, msgs("s2", rig.admin)));     // 管理员不受影响
    FH_CHECK(g->muteMember(rig.admin, rig.member, false));          // 解除禁言
    FH_CHECK(!g->isMuted(rig.member));
    FH_CHECK(g->sendMessage(rig.member, msgs("s3", rig.member)));
}

FH_TEST(AllMuteBlocksMemberButNotPrivileged) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(g->sendMessage(rig.member, msgs("a1", rig.member)));
    FH_CHECK(!g->setAllMute(rig.member, true));   // 成员无权
    FH_CHECK(g->setAllMute(rig.admin, true));     // QQ：管理员可开启
    FH_CHECK(g->getConfig().isAllMuted());
    FH_CHECK(!g->sendMessage(rig.member, msgs("a2", rig.member)));  // 全员禁言中
    FH_CHECK(g->sendMessage(rig.admin, msgs("a3", rig.admin)));
    FH_CHECK(g->sendMessage(rig.owner, msgs("a4", rig.owner)));
    FH_CHECK(g->setAllMute(rig.admin, false));    // 解除
    FH_CHECK(g->sendMessage(rig.member, msgs("a5", rig.member)));
}

// ---------------- 7. 同级 / 更高角色保护 ----------------

FH_TEST(PeerProtectionCannotTargetSameOrHigherRole) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->muteMember(rig.admin, rig.admin, true));   // 同级
    FH_CHECK(!g->muteMember(rig.admin, rig.owner, true));   // 更高
    FH_CHECK(!g->kickMember(rig.admin, rig.admin));         // 同级
    FH_CHECK(!g->kickMember(rig.admin, rig.owner));         // 更高
    FH_CHECK(g->kickMember(rig.owner, rig.admin));          // 群主可操作管理员
    FH_CHECK_EQ(g->members().size(), std::size_t(2));
}

// ---------------- 8. 撤回：归属 + 特权 + 已撤回 + 时间窗 ----------------

FH_TEST(RecallRespectsOwnershipAndPrivilege) {
    Rig rig;
    auto g = rig.qq();
    auto byOwner = msgs("r1", rig.owner, "群主消息");
    auto byMember = msgs("r2", rig.member, "成员消息");
    FH_CHECK(g->sendMessage(rig.owner, byOwner));
    FH_CHECK(g->sendMessage(rig.member, byMember));
    // 普通成员不能撤回他人消息
    FH_CHECK(!g->recallMessage(rig.member, "r1"));
    // 普通成员可撤回本人消息
    FH_CHECK(g->recallMessage(rig.member, "r2"));
    FH_CHECK(byMember->isRecalled());
    // 已撤回消息不可再次撤回（管理员也不行）
    FH_CHECK(!g->recallMessage(rig.admin, "r2"));
    // 管理员可撤回任意成员消息
    FH_CHECK(g->recallMessage(rig.admin, "r1"));
    FH_CHECK(byOwner->isRecalled());
    // 不存在的消息 ID
    FH_CHECK(!g->recallMessage(rig.owner, "not-exist"));
}

FH_TEST(RecallExpiresAfterTimeWindow) {
    Rig rig;
    auto g = rig.qq();
    auto now = system_clock::now();
    // 发送一条“已超窗”的消息（窗口 120s，消息比现在早 121s）
    auto stale = make_shared<MessageFH>("old", rig.owner, "很旧的消息",
                                        now - seconds(121));
    FH_CHECK(g->sendMessage(rig.owner, stale));  // 发送只看禁言，不查时间
    FH_CHECK(!g->recallMessage(rig.owner, "old"));   // 群主也受时间窗约束
    auto fresh = msgs("new", rig.owner, "新消息");
    FH_CHECK(g->sendMessage(rig.owner, fresh));
    FH_CHECK(g->recallMessage(rig.owner, "new"));    // 窗口内可撤回
}

// ---------------- 9. 改群名 / 公告（ADMIN+） ----------------

FH_TEST(EditAndAnnouncementRequireAdmin) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->editGroup(rig.member, "新名字"));
    FH_CHECK(!g->editGroup(rig.admin, ""));   // 空名拒绝
    FH_CHECK(g->editGroup(rig.admin, "新名字"));
    FH_CHECK_EQ(g->getName(), std::string("新名字"));

    FH_CHECK(!g->publishAnnouncement(rig.member, "公告"));
    FH_CHECK(!g->publishAnnouncement(rig.admin, ""));  // 空内容拒绝
    FH_CHECK(g->publishAnnouncement(rig.owner, "欢迎加入"));
    FH_CHECK_EQ(g->getAnnouncement(), std::string("欢迎加入"));
}

// ---------------- 10. 任免管理员（仅群主，不能操作群主） ----------------

FH_TEST(AssignAdminOnlyByOwner) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->setAdmin(rig.admin, rig.member, true));  // 管理员无权任命
    FH_CHECK(g->setAdmin(rig.owner, rig.member, true));   // 群主任命成功
    FH_CHECK_EQ(g->getRole(rig.member), GroupRoleFH::ADMIN);
    FH_CHECK(!g->setAdmin(rig.owner, rig.member, true));  // 已是管理员，幂等拒绝
    FH_CHECK(g->setAdmin(rig.owner, rig.member, false));  // 撤销
    FH_CHECK_EQ(g->getRole(rig.member), GroupRoleFH::MEMBER);
    FH_CHECK(!g->setAdmin(rig.owner, rig.owner, true));   // 不能操作群主
    FH_CHECK_EQ(g->getRole(rig.owner), GroupRoleFH::OWNER);
}

// ---------------- 11. 转让群主（原子交换） ----------------

FH_TEST(TransferOwnerAtomicallySwapsRoles) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->transferOwner(rig.member, rig.admin));  // 成员无权
    FH_CHECK(!g->transferOwner(rig.admin, rig.member));  // 管理员无权
    FH_CHECK(!g->transferOwner(rig.owner, rig.owner));   // 不能转让给自己
    FH_CHECK(g->transferOwner(rig.owner, rig.admin));
    FH_CHECK_EQ(g->getRole(rig.admin), GroupRoleFH::OWNER);
    FH_CHECK_EQ(g->getRole(rig.owner), GroupRoleFH::MEMBER);
    // 群内仍只有一个 OWNER
    int ownerCount = 0;
    for (const auto& kv : g->members())
        if (kv.second.getRole() == GroupRoleFH::OWNER) ++ownerCount;
    FH_CHECK_EQ(ownerCount, 1);
    // 原群主已不是群主：不能解散；新群主可以
    FH_CHECK(!g->disband(rig.owner));
    FH_CHECK(g->disband(rig.admin));
}

// ---------------- 12. 解散后不可操作 ----------------

FH_TEST(DisbandBlocksEveryOperation) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(!g->disband(rig.member));
    FH_CHECK(!g->disband(rig.admin));
    FH_CHECK(g->disband(rig.owner));
    FH_CHECK(g->isDisbanded());
    FH_CHECK_EQ(g->members().size(), std::size_t(0));
    // 此后一切操作拒绝
    FH_CHECK(!g->sendMessage(rig.owner, msgs("d1", rig.owner)));
    FH_CHECK(!g->inviteMember(rig.owner, rig.outsider));
    FH_CHECK(!g->editGroup(rig.owner, "复活"));
    FH_CHECK(!g->publishAnnouncement(rig.owner, "复活公告"));
    FH_CHECK(!g->setAllMute(rig.owner, true));
    FH_CHECK(!g->muteMember(rig.owner, rig.member, true));
    FH_CHECK(!g->kickMember(rig.owner, rig.member));
    FH_CHECK(!g->setAdmin(rig.owner, rig.admin, true));
    FH_CHECK(!g->recallMessage(rig.owner, "any"));
    FH_CHECK(!g->switchPolicy(make_shared<WeChatPolicyFH>()));
    FH_CHECK(!g->disband(rig.owner));  // 再次解散也失败
}

// ---------------- 13. 管理模式切换（数据不受伤害） ----------------

FH_TEST(SwitchPolicyKeepsDataAndChangesPrivileges) {
    Rig rig;
    auto g = rig.qq();
    FH_CHECK(g->setAllMute(rig.admin, true));   // QQ：管理员可全员禁言
    FH_CHECK(g->setAllMute(rig.admin, false));
    FH_CHECK(g->switchPolicy(make_shared<WeChatPolicyFH>()));
    FH_CHECK(!g->switchPolicy(nullptr));        // 空策略拒绝
    // 微信模式：管理员不能再设置全员禁言，仅群主可以
    FH_CHECK(!g->setAllMute(rig.admin, true));
    FH_CHECK(g->setAllMute(rig.owner, true));
    FH_CHECK(g->setAllMute(rig.owner, false));
    // 成员数据与既有消息未受伤害
    FH_CHECK_EQ(g->members().size(), std::size_t(3));
    auto m = msgs("sw", rig.member);
    FH_CHECK(g->sendMessage(rig.member, m));
    FH_CHECK_EQ(g->messages().size(), std::size_t(1));
}

// ---------------- 14. 人数上限读取配置（不硬编码） ----------------

FH_TEST(MaxGroupSizeReadsConfig) {
    Rig rig;
    GroupConfigFH cfg(88, false, false, seconds(120));
    auto g = make_shared<GroupFH>("g1", 1001, "配置群", cfg,
                                  make_shared<QQPolicyFH>(), rig.owner);
    FH_CHECK_EQ(g->getPolicy()->getMaxGroupSize(*g), std::size_t(88));
}

// ---------------- 15. 成员关系记录入群时刻（joinedAt） ----------------

FH_TEST(MembershipRecordsJoinTime) {
    Rig rig;
    auto g = rig.qq();
    const auto before = system_clock::now() - minutes(5);
    const auto after = system_clock::now() + minutes(5);
    auto it = g->members().find(rig.owner->getId());
    FH_CHECK(it != g->members().end());
    if (it != g->members().end()) {
        const auto joined = it->second.getJoinedAt();
        FH_CHECK(joined >= before);  // 建群时记录，不应早于测试开始前很久
        FH_CHECK(joined <= after);
    }
    // 群主转让后，新群主的关系仍保留原入群时刻
    FH_CHECK(g->transferOwner(rig.owner, rig.admin));
    auto it2 = g->members().find(rig.admin->getId());
    FH_CHECK(it2 != g->members().end());
    if (it2 != g->members().end()) {
        FH_CHECK(it2->second.getJoinedAt() >= before);
        FH_CHECK(it2->second.getJoinedAt() <= after);
    }
}

}  // namespace

int main() { return ::fhtest::runAll("group-core"); }
