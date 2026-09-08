#pragma once
// ============================================================
// GroupMembershipFH —— 群成员关系对象（作者代号：FH）
// ------------------------------------------------------------
// 用户加入某个群后，群内为该用户保存一份“关系对象”，
// 角色（GroupRoleFH）与禁言状态只属于这里，不属于 UserFH。
// 这正是“同一用户在不同群可有不同角色”的建模基础。
// ============================================================
#include <memory>
#include <stdexcept>
#include <utility>

#include "im/model/group_role_fh.hpp"
#include "im/model/user_fh.hpp"

class GroupMembershipFH {
public:
    GroupMembershipFH() = default;

    GroupMembershipFH(std::shared_ptr<UserFH> user,
                      GroupRoleFH role = GroupRoleFH::MEMBER,
                      bool muted = false)
        : user_(std::move(user)), role_(role), muted_(muted) {
        if (!user_) throw std::invalid_argument("GroupMembershipFH: user is required");
    }

    const std::shared_ptr<UserFH>& getUser() const noexcept { return user_; }
    GroupRoleFH getRole() const noexcept { return role_; }
    bool isMuted() const noexcept { return muted_; }

    void setRole(GroupRoleFH role) noexcept { role_ = role; }
    void setMuted(bool muted) noexcept { muted_ = muted; }

private:
    std::shared_ptr<UserFH> user_;
    GroupRoleFH role_{GroupRoleFH::MEMBER};
    bool muted_{false};
};
