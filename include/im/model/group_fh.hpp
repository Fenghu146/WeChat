#pragma once
// ============================================================
// GroupFH —— 群聚合根（作者代号：FH）
// ------------------------------------------------------------
// 职责：统一管理群内状态（成员 / 消息 / 公告 / 配置 / 策略 / 解散
// 状态），并协调“授权 → 变更状态”的完整业务过程。
//
// 设计要点：
//  1. 本类中【不出现任何 if (platform == QQ)】分支 —— 平台差异由
//     GroupPolicyFH 多态完成，新增平台主要新增 Policy；
//  2. 业务不变量集中维护：成员唯一、人数上限、任意时刻至多一个
//     OWNER、解散后不可操作；
//  3. 每个业务操作 = 构造 GroupContextFH → 策略授权 → 通过才改状态；
//  4. switchPolicy() 支持把本群动态切换为其它类型群的管理模式
//     （官方要求：群成员数据不受伤害，管理模式可变换）。
//
// 成员表以 UserFH::id 为键的 unordered_map 存储：O(1) 查找 + 天然去重。
// ============================================================
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "im/context/action_fh.hpp"
#include "im/context/group_context_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_membership_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/policy/group_policy_fh.hpp"

class GroupFH {
public:
    // —— 构造：校验合法后注入群主，保证任意时刻至多一个 OWNER ——
    GroupFH(std::string id, unsigned groupNumber, std::string name,
            GroupConfigFH config, std::shared_ptr<GroupPolicyFH> policy,
            std::shared_ptr<UserFH> owner)
        : id_(std::move(id)),
          groupNumber_(groupNumber),
          name_(std::move(name)),
          config_(std::move(config)),
          policy_(std::move(policy)) {
        config_.validate();
        if (id_.empty() || name_.empty())
            throw std::invalid_argument("GroupFH: id and name must not be empty");
        if (!policy_) throw std::invalid_argument("GroupFH: policy is required");
        if (!owner || owner->getId().empty())
            throw std::invalid_argument("GroupFH: owner is required");
        // 注意：先取 ID 再 std::move —— emplace 的实参求值顺序不确定，
        // 若先 move 会使 owner 变空，再调用 getId() 解引用空指针。
        const std::string ownerId = owner->getId();
        members_.emplace(ownerId,
                         GroupMembershipFH(std::move(owner), GroupRoleFH::OWNER));
    }

    // ======================= 业务操作：先授权，后变更状态 =======================

    // 发送消息：发送者须为本人；通过禁言等状态检查后追加到消息列表
    bool sendMessage(const std::shared_ptr<UserFH>& op,
                     const std::shared_ptr<MessageFH>& message) {
        if (!message || !message->getSender() || !op ||
            message->getSender()->getId() != op->getId())
            return false;  // 发送者必须与操作者一致
        GroupContextFH c{this, op, nullptr, message,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::SEND_MESSAGE, c)) return false;
        messages_.push_back(message);
        return true;
    }

    // 撤回消息：受“归属 + 时间窗 + 已撤回”约束（见 checkStateRules）
    bool recallMessage(const std::shared_ptr<UserFH>& op,
                       const std::string& messageId) {
        auto message = findMessage(messageId);
        if (!message) return false;
        GroupContextFH c{this, op, nullptr, message,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::RECALL_MESSAGE, c)) return false;
        message->recall();
        return true;
    }

    // 邀请成员：授权通过（平台规则 + 被邀请者不在群内）后再真正入群
    bool inviteMember(const std::shared_ptr<UserFH>& op,
                      const std::shared_ptr<UserFH>& invitee) {
        GroupContextFH c{this, op, invitee, nullptr,
                         std::chrono::system_clock::now()};
        return execute(ActionFH::INVITE_MEMBER, c) && addMember(invitee);
    }

    // 踢出成员：授权通过（ADMIN+ 且目标等级更低）后删除成员记录
    bool kickMember(const std::shared_ptr<UserFH>& op,
                    const std::shared_ptr<UserFH>& target) {
        GroupContextFH c{this, op, target, nullptr,
                         std::chrono::system_clock::now()};
        return execute(ActionFH::KICK_MEMBER, c) &&
               members_.erase(idOf(target)) == 1;
    }

    // 禁言 / 解除禁言：成员状态检查发生在授权链中
    bool muteMember(const std::shared_ptr<UserFH>& op,
                    const std::shared_ptr<UserFH>& target, bool muted = true) {
        GroupContextFH c{this, op, target, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::MUTE_MEMBER, c)) return false;
        membershipOf(target).setMuted(muted);
        return true;
    }

    // 修改群名称：ADMIN+ 可执行，新名称不能为空
    bool editGroup(const std::shared_ptr<UserFH>& op, const std::string& newName) {
        if (newName.empty()) return false;
        GroupContextFH c{this, op, nullptr, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::EDIT_GROUP, c)) return false;
        name_ = newName;
        return true;
    }

    // 发布群公告：ADMIN+ 可执行，内容不能为空
    bool publishAnnouncement(const std::shared_ptr<UserFH>& op,
                             const std::string& text) {
        if (text.empty()) return false;
        GroupContextFH c{this, op, nullptr, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::PUBLISH_ANNOUNCEMENT, c)) return false;
        announcement_ = text;
        return true;
    }

    // 设置 / 解除全员禁言：平台差异（QQ: ADMIN+；微信: 仅群主）
    bool setAllMute(const std::shared_ptr<UserFH>& op, bool enabled) {
        GroupContextFH c{this, op, nullptr, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::SET_ALL_MUTE, c)) return false;
        config_.setAllMuted(enabled);
        return true;
    }

    // 任命 / 撤销管理员：仅群主；不能操作群主本人，目标角色不符时返回 false
    bool setAdmin(const std::shared_ptr<UserFH>& op,
                  const std::shared_ptr<UserFH>& target, bool admin) {
        GroupContextFH c{this, op, target, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::ASSIGN_ADMIN, c)) return false;
        auto& m = membershipOf(target);
        const GroupRoleFH wanted = admin ? GroupRoleFH::ADMIN : GroupRoleFH::MEMBER;
        if (m.getRole() == wanted || m.getRole() == GroupRoleFH::OWNER)
            return false;  // 目标已是该角色，或目标是群主
        m.setRole(wanted);
        return true;
    }

    // 转让群主：仅群主；原子交换角色 —— 原群主降为 MEMBER，目标升为 OWNER
    bool transferOwner(const std::shared_ptr<UserFH>& op,
                       const std::shared_ptr<UserFH>& target) {
        if (!op || !target || op->getId() == target->getId()) return false;  // 不能转让给自己
        GroupContextFH c{this, op, target, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::TRANSFER_OWNER, c)) return false;
        membershipOf(op).setRole(GroupRoleFH::MEMBER);   // 原群主降级
        membershipOf(target).setRole(GroupRoleFH::OWNER);
        return true;
    }

    // 解散群组：仅群主；解散后 execute 恒为 false，成员表清空
    bool disband(const std::shared_ptr<UserFH>& op) {
        GroupContextFH c{this, op, nullptr, nullptr,
                         std::chrono::system_clock::now()};
        if (!execute(ActionFH::DISBAND_GROUP, c)) return false;
        disbanded_ = true;
        members_.clear();
        return true;
    }

    // ======================= 管理模式动态切换 =======================
    // 官方要求：在群成员数据不受伤害的前提下，动态变换为其他类型群的
    // 管理特色。实现即换绑策略对象：成员/消息/配置数据原样保留。
    bool switchPolicy(const std::shared_ptr<GroupPolicyFH>& newPolicy) noexcept {
        if (disbanded_ || !newPolicy) return false;
        policy_ = newPolicy;
        return true;
    }

    // ======================= 查询接口（不暴露可变集合） =======================

    bool contains(const std::shared_ptr<UserFH>& user) const noexcept {
        return user && members_.count(user->getId()) > 0;
    }

    // 是否被单员禁言（群外 / 非成员一律视为未禁言）
    bool isMuted(const std::shared_ptr<UserFH>& user) const noexcept {
        auto it = members_.find(idOf(user));
        return it != members_.end() && it->second.isMuted();
    }

    // 查询某成员角色；不在群内返回 std::nullopt
    std::optional<GroupRoleFH> getRole(
        const std::shared_ptr<UserFH>& user) const noexcept {
        auto it = members_.find(idOf(user));
        if (it == members_.end()) return std::nullopt;
        return it->second.getRole();
    }

    const std::string& getId() const noexcept { return id_; }
    unsigned getGroupNumber() const noexcept { return groupNumber_; }
    const std::string& getName() const noexcept { return name_; }
    const GroupConfigFH& getConfig() const noexcept { return config_; }
    const std::string& getAnnouncement() const noexcept { return announcement_; }
    bool isDisbanded() const noexcept { return disbanded_; }
    const std::shared_ptr<GroupPolicyFH>& getPolicy() const noexcept { return policy_; }

    const std::unordered_map<std::string, GroupMembershipFH>& members() const noexcept {
        return members_;
    }
    const std::vector<std::shared_ptr<MessageFH>>& messages() const noexcept {
        return messages_;
    }

private:
    // 统一执行入口：群已解散则一切操作拒绝；否则交策略做完整授权
    bool execute(ActionFH action, const GroupContextFH& c) const {
        return !disbanded_ && policy_->isAllowed(action, c);
    }

    // 邀请内部使用：查重（map 键唯一）+ 人数上限
    bool addMember(const std::shared_ptr<UserFH>& user) {
        if (!user || user->getId().empty() || contains(user)) return false;
        if (members_.size() >= policy_->getMaxGroupSize(*this)) return false;
        members_.emplace(user->getId(),
                         GroupMembershipFH(user, GroupRoleFH::MEMBER));
        return true;
    }

    static const std::string& idOf(const std::shared_ptr<UserFH>& user) {
        static const std::string empty;
        return user ? user->getId() : empty;
    }

    GroupMembershipFH& membershipOf(const std::shared_ptr<UserFH>& user) {
        return members_.at(idOf(user));
    }

    std::shared_ptr<MessageFH> findMessage(const std::string& id) const {
        for (const auto& m : messages_)
            if (m->getId() == id) return m;
        return nullptr;
    }

    std::string id_;                    // 群标识（字符串主键）
    unsigned groupNumber_{0};           // 平台群号（官方 1001~1006 区间由群注册表分配）
    std::string name_;                  // 群名称
    std::unordered_map<std::string, GroupMembershipFH> members_;  // key: UserFH::id
    std::vector<std::shared_ptr<MessageFH>> messages_;
    GroupConfigFH config_;
    std::shared_ptr<GroupPolicyFH> policy_;  // 可被 switchPolicy 换绑
    std::string announcement_;
    bool disbanded_{false};
};
