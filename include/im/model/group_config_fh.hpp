#pragma once
// ============================================================
// GroupConfigFH —— 群可变配置（作者代号：FH）
// ------------------------------------------------------------
// 字段：人数上限、成员邀请开关、全员禁言、消息撤回时间窗。
// validate() 在构造与变更时调用，非法配置抛出 std::invalid_argument。
// ============================================================
#include <chrono>
#include <cstddef>
#include <stdexcept>

class GroupConfigFH {
public:
    GroupConfigFH() = default;

    GroupConfigFH(std::size_t maxMembers, bool memberInviteEnabled, bool allMuted,
                  std::chrono::seconds recallTimeLimit)
        : maxMembers_(maxMembers),
          memberInviteEnabled_(memberInviteEnabled),
          allMuted_(allMuted),
          recallTimeLimit_(recallTimeLimit) {
        validate();
    }

    // 配置合法性：人数必须为正，撤回时间窗不能为负
    void validate() const {
        if (maxMembers_ == 0 || recallTimeLimit_ < std::chrono::seconds::zero())
            throw std::invalid_argument("GroupConfigFH: invalid group configuration");
    }

    std::size_t getMaxMembers() const noexcept { return maxMembers_; }
    bool isMemberInviteEnabled() const noexcept { return memberInviteEnabled_; }
    bool isAllMuted() const noexcept { return allMuted_; }
    std::chrono::seconds getRecallTimeLimit() const noexcept { return recallTimeLimit_; }

    // QQ 群专属开关：普通成员是否可邀请好友（微信群无此概念）
    void setMemberInviteEnabled(bool enabled) noexcept { memberInviteEnabled_ = enabled; }
    // 全员禁言开关，由 GroupFH::setAllMute 在授权通过后调用
    void setAllMuted(bool allMuted) noexcept { allMuted_ = allMuted; }

    // 撤回时间窗可在群运行期调整（由 GroupFH::setRecallTimeLimit 授权后调用）；
    // 负数属非法配置，validate() 会抛出 std::invalid_argument
    void setRecallTimeLimit(std::chrono::seconds limit) {
        recallTimeLimit_ = limit;
        validate();
    }

private:
    std::size_t maxMembers_{500};                 // 群人数上限
    bool memberInviteEnabled_{false};             // 普通成员邀请开关
    bool allMuted_{false};                        // 全员禁言开关
    std::chrono::seconds recallTimeLimit_{120};   // 消息撤回时间窗（秒）
};
