// ============================================================
// qq_wechat_policy_test.cpp —— QQ / 微信平台差异矩阵（阶段 A）
// 对应分工文档 C 的平台差异测试：普通成员邀请、admin 设全员禁言、
// 全员禁言下成员/管理员发言、人数上限读取配置。
// ============================================================
#include <chrono>
#include <memory>
#include <string>

#include "fh_mini_test.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
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

// 同一批用户 + 同一配置，分别套 QQ 策略与微信策略
struct GroupPair {
    shared_ptr<UserFH> owner{mk("o1")};
    shared_ptr<UserFH> admin{mk("a1")};
    shared_ptr<UserFH> member{mk("m1")};
    shared_ptr<UserFH> outsider{mk("x1")};
    shared_ptr<GroupFH> qq;
    shared_ptr<GroupFH> wx;

    explicit GroupPair(bool memberInviteOn = false) {
        auto cfg = GroupConfigFH(500, memberInviteOn, false, seconds(120));
        qq = makeGroup("g-qq", 1001, cfg, make_shared<QQPolicyFH>());
        wx = makeGroup("g-wx", 1003, cfg, make_shared<WeChatPolicyFH>());
    }

private:
    shared_ptr<GroupFH> makeGroup(const char* id, unsigned no,
                                  const GroupConfigFH& cfg,
                                  shared_ptr<GroupPolicyFH> policy) {
        auto g = make_shared<GroupFH>(std::string(id), no,
                                      std::string("测试群") + id, cfg,
                                      std::move(policy), owner);
        g->inviteMember(owner, admin);
        g->inviteMember(owner, member);
        g->setAdmin(owner, admin, true);
        return g;
    }
};

shared_ptr<MessageFH> msgs(const std::string& id,
                           const shared_ptr<UserFH>& sender) {
    return make_shared<MessageFH>(id, sender, "hi");
}

// QQ：普通成员邀请由群配置开关决定
FH_TEST(QQMemberInviteDependsOnConfig) {
    GroupPair off(false);
    FH_CHECK(!off.qq->inviteMember(off.member, off.outsider));  // 开关关闭：失败
    FH_CHECK(off.qq->inviteMember(off.admin, off.outsider));    // 管理员不受影响
    auto newcomer = mk("x9");                                   // 全新的人
    FH_CHECK(off.qq->inviteMember(off.owner, newcomer));        // 群主可邀请新人

    GroupPair on(true);
    FH_CHECK(on.qq->inviteMember(on.member, on.outsider));      // 开关开启：成员可邀请
}

// 微信：普通成员永远不能邀请，仅 ADMIN+
FH_TEST(WeChatMemberInviteAlwaysForbidden) {
    GroupPair off(false);
    GroupPair on(true);   // 即使配置开关注入微信群也无效
    FH_CHECK(!off.wx->inviteMember(off.member, off.outsider));
    FH_CHECK(!on.wx->inviteMember(on.member, on.outsider));
    FH_CHECK(on.wx->inviteMember(on.admin, on.outsider));       // 管理员可邀请
}

// 全员禁言设置权限差异：QQ=ADMIN+；微信=仅群主
FH_TEST(SetAllMutePlatformDifference) {
    GroupPair pair;
    FH_CHECK(pair.qq->setAllMute(pair.admin, true));            // QQ 管理员：成功
    FH_CHECK(pair.qq->getConfig().isAllMuted());
    FH_CHECK(!pair.wx->setAllMute(pair.admin, true));           // 微信管理员：失败
    FH_CHECK(!pair.wx->getConfig().isAllMuted());
    FH_CHECK(pair.wx->setAllMute(pair.owner, true));            // 微信群主：成功
    FH_CHECK(pair.wx->getConfig().isAllMuted());
}

// 全员禁言期间：普通成员两平台都不能发言；管理员两平台都能发言
FH_TEST(AllMuteAffectsBothPlatformsEqually) {
    GroupPair pair;
    FH_CHECK(pair.qq->setAllMute(pair.owner, true));
    FH_CHECK(pair.wx->setAllMute(pair.owner, true));
    FH_CHECK(!pair.qq->sendMessage(pair.member, msgs("a1", pair.member)));
    FH_CHECK(!pair.wx->sendMessage(pair.member, msgs("a2", pair.member)));
    FH_CHECK(pair.qq->sendMessage(pair.admin, msgs("a3", pair.admin)));
    FH_CHECK(pair.wx->sendMessage(pair.admin, msgs("a4", pair.admin)));
    FH_CHECK(pair.qq->sendMessage(pair.owner, msgs("a5", pair.owner)));
}

// 人数上限解释：两平台都读取 GroupConfigFH，不硬编码
FH_TEST(MaxGroupSizeSameAcrossPlatformsFromConfig) {
    GroupPair pair;
    FH_CHECK_EQ(pair.qq->getPolicy()->getMaxGroupSize(*pair.qq), std::size_t(500));
    FH_CHECK_EQ(pair.wx->getPolicy()->getMaxGroupSize(*pair.wx), std::size_t(500));
}

}  // namespace

int main() { return ::fhtest::runAll("qq-wechat-policy"); }
