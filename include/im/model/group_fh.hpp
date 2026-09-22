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
//
// 声明与实现分离：方法实现位于 src/model/group_fh.cpp。
// ============================================================
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
            std::shared_ptr<UserFH> owner);

    // ======================= 业务操作：先授权，后变更状态 =======================

    // 发送消息：发送者须为本人；消息标识不能为空且必须唯一（标识重复会
    // 让 recallMessage 命中同名的旧消息）；内容不能为空。
    // 通过禁言等状态检查后追加到消息列表
    bool sendMessage(const std::shared_ptr<UserFH>& op,
                     const std::shared_ptr<MessageFH>& message);
    // 撤回消息：受“归属 + 时间窗 + 已撤回”约束（见 checkStateRules）
    bool recallMessage(const std::shared_ptr<UserFH>& op,
                       const std::string& messageId);
    // 邀请成员：授权通过（平台规则 + 被邀请者不在群内）后再真正入群
    bool inviteMember(const std::shared_ptr<UserFH>& op,
                      const std::shared_ptr<UserFH>& invitee);
    // 踢出成员：授权通过（ADMIN+ 且目标等级更低）后删除成员记录
    bool kickMember(const std::shared_ptr<UserFH>& op,
                    const std::shared_ptr<UserFH>& target);
    // 成员主动退群（任务书 3.(2) 入群 / 退出群 / 挨踢）：
    // 群主不能直接退群——须先转让群主或解散群，避免群处于“无主”状态；
    // 非成员、群已解散一律失败。退群不经过策略授权链（无对应 Action），
    // 由聚合根集中维护不变量。
    bool leaveGroup(const std::shared_ptr<UserFH>& op);
    // 禁言 / 解除禁言：成员状态检查发生在授权链中
    bool muteMember(const std::shared_ptr<UserFH>& op,
                    const std::shared_ptr<UserFH>& target, bool muted = true);
    // 修改群名称：ADMIN+ 可执行，新名称不能为空（全空白同样视为空）
    bool editGroup(const std::shared_ptr<UserFH>& op, const std::string& newName);
    // 发布群公告：ADMIN+ 可执行，内容不能为空（全空白同样视为空）
    bool publishAnnouncement(const std::shared_ptr<UserFH>& op,
                             const std::string& text);
    // 设置 / 解除全员禁言：平台差异（QQ: ADMIN+；微信: 仅群主）
    bool setAllMute(const std::shared_ptr<UserFH>& op, bool enabled);
    // 变更群配置：走 EDIT_GROUP 授权（ADMIN+），用于验证“群设置可动态变更”
    // —— 撤回时间窗（负数非法：返回 false 且配置保持原值，不再向外抛异常）
    bool setRecallTimeLimit(const std::shared_ptr<UserFH>& op,
                            std::chrono::seconds limit);
    // 变更群配置：QQ 普通成员邀请开关（微信群无此概念，语义上恒为“仅群主”）
    bool setMemberInviteEnabled(const std::shared_ptr<UserFH>& op, bool enabled);
    // 任命 / 撤销管理员：仅群主；不能操作群主本人，目标角色不符时返回 false
    bool setAdmin(const std::shared_ptr<UserFH>& op,
                  const std::shared_ptr<UserFH>& target, bool admin);
    // 转让群主：仅群主；原子交换角色 —— 原群主降为 MEMBER，目标升为 OWNER
    bool transferOwner(const std::shared_ptr<UserFH>& op,
                       const std::shared_ptr<UserFH>& target);
    // 解散群组：仅群主；解散后 execute 恒为 false，成员表清空
    bool disband(const std::shared_ptr<UserFH>& op);

    // —— 官方要求：成员数据不受伤害的前提下动态变换管理模式 ——
    bool switchPolicy(const std::shared_ptr<GroupPolicyFH>& newPolicy) noexcept;

    // ======================= 查询接口（不暴露可变集合） =======================

    bool contains(const std::shared_ptr<UserFH>& user) const noexcept;
    // 是否被单员禁言（群外 / 非成员一律视为未禁言）
    bool isMuted(const std::shared_ptr<UserFH>& user) const noexcept;
    // 查询某成员角色；不在群内返回 std::nullopt
    std::optional<GroupRoleFH> getRole(
        const std::shared_ptr<UserFH>& user) const noexcept;

    const std::string& getId() const noexcept;
    unsigned getGroupNumber() const noexcept;
    const std::string& getName() const noexcept;
    const GroupConfigFH& getConfig() const noexcept;
    const std::string& getAnnouncement() const noexcept;
    bool isDisbanded() const noexcept;
    const std::shared_ptr<GroupPolicyFH>& getPolicy() const noexcept;

    const std::unordered_map<std::string, GroupMembershipFH>& members() const noexcept;
    const std::vector<std::shared_ptr<MessageFH>>& messages() const noexcept;

private:
    // 统一执行入口：群已解散则一切操作拒绝；否则交策略做完整授权
    bool execute(ActionFH action, const GroupContextFH& c) const;
    // 邀请内部使用：查重（map 键唯一）+ 人数上限
    bool addMember(const std::shared_ptr<UserFH>& user);
    static const std::string& idOf(const std::shared_ptr<UserFH>& user);
    // 是否为空串或仅由空白字符组成（群名/公告等自由文本的“非空”校验）
    static bool isBlankText(const std::string& s);
    GroupMembershipFH& membershipOf(const std::shared_ptr<UserFH>& user);
    std::shared_ptr<MessageFH> findMessage(const std::string& id) const;

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
