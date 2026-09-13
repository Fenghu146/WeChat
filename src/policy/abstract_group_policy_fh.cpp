// ============================================================
// AbstractGroupPolicyFH 实现（作者代号：FH）
// 六步模板方法的具体实现。注意：本文件不得出现
// “if (platform == QQ)”式的分支 —— 平台差异只发生在
// 子类覆写的 checkPlatformRule。
// ============================================================
#include "im/policy/abstract_group_policy_fh.hpp"

#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"

bool AbstractGroupPolicyFH::isAllowed(ActionFH action,
                                      const GroupContextFH& context) const {
    return validateContext(action, context)          // 1. 上下文合法
        && checkMembership(action, context)          // 2. 成员关系
        && checkPermission(action, context)          // 3. 公共角色权限
        && checkTargetPermission(action, context)    // 4. 目标权限
        && checkStateRules(action, context)          // 5. 公共状态约束
        && checkPlatformRule(action, context);       // 6. 平台差异（多态）
}

std::size_t AbstractGroupPolicyFH::getMaxGroupSize(const GroupFH& group) const {
    return group.getConfig().getMaxMembers();  // 默认读取群配置
}

// 步骤 1：上下文合法性 —— 必须的 group/operator/message/target 是否存在
bool AbstractGroupPolicyFH::validateContext(ActionFH action,
                                            const GroupContextFH& context) const {
    if (!context.group || !context.operatorUser) return false;

    const bool needsTarget =
        action == ActionFH::INVITE_MEMBER ||
        action == ActionFH::KICK_MEMBER ||
        action == ActionFH::MUTE_MEMBER ||
        action == ActionFH::TRANSFER_OWNER ||
        action == ActionFH::ASSIGN_ADMIN;
    if (needsTarget && !context.target) return false;

    if ((action == ActionFH::SEND_MESSAGE ||
         action == ActionFH::RECALL_MESSAGE) && !context.message) return false;
    return true;
}

// 步骤 2：成员关系 —— 操作者必须在群内；部分操作对目标成员关系有要求
bool AbstractGroupPolicyFH::checkMembership(ActionFH action,
                                            const GroupContextFH& context) const {
    if (!context.group->contains(context.operatorUser)) return false;
    switch (action) {
    case ActionFH::KICK_MEMBER:
    case ActionFH::MUTE_MEMBER:
    case ActionFH::TRANSFER_OWNER:
    case ActionFH::ASSIGN_ADMIN:
        return context.group->contains(context.target);    // 目标必须在群内
    case ActionFH::INVITE_MEMBER:
        return !context.group->contains(context.target);   // 被邀请者必须当前不在群内
    default:
        return true;
    }
}

// 步骤 3：公共角色权限矩阵（对目标与消息是否满足由后续步骤把关）
bool AbstractGroupPolicyFH::checkPermission(ActionFH action,
                                            const GroupContextFH& context) const {
    const auto role = context.group->getRole(context.operatorUser);
    if (!role) return false;
    switch (action) {
    case ActionFH::SEND_MESSAGE:
    case ActionFH::RECALL_MESSAGE:
    case ActionFH::INVITE_MEMBER:
        return true;                           // 由状态检查与平台规则继续收紧
    case ActionFH::KICK_MEMBER:
    case ActionFH::MUTE_MEMBER:
    case ActionFH::EDIT_GROUP:
    case ActionFH::PUBLISH_ANNOUNCEMENT:
    case ActionFH::SET_ALL_MUTE:
        return *role >= GroupRoleFH::ADMIN;    // ADMIN+ 可执行
    case ActionFH::ASSIGN_ADMIN:
    case ActionFH::TRANSFER_OWNER:
    case ActionFH::DISBAND_GROUP:
        return *role == GroupRoleFH::OWNER;    // 仅群主可执行
    }
    return false;
}

// 步骤 4：目标权限 —— 踢人/禁言不能针对同级或更高角色
bool AbstractGroupPolicyFH::checkTargetPermission(ActionFH action,
                                                  const GroupContextFH& context) const {
    if (action != ActionFH::KICK_MEMBER && action != ActionFH::MUTE_MEMBER)
        return true;
    const auto operatorRole = context.group->getRole(context.operatorUser);
    const auto targetRole = context.group->getRole(context.target);
    // 管理员不能踢/禁言管理员或群主；群主可以操作管理员（群主降级走 transferOwner）
    return operatorRole && targetRole && *operatorRole > *targetRole;
}

// 步骤 5：公共状态约束（两平台一致，置于父类）
bool AbstractGroupPolicyFH::checkStateRules(ActionFH action,
                                            const GroupContextFH& context) const {
    const auto& cfg = context.group->getConfig();
    switch (action) {
    case ActionFH::SEND_MESSAGE:
        // 被单员禁言的普通成员不能发言
        if (context.group->isMuted(context.operatorUser) && !isPrivileged(context)) return false;
        // 全员禁言期间：普通成员不能发言，ADMIN / OWNER 不受影响
        return isPrivileged(context) || !cfg.isAllMuted();
    case ActionFH::RECALL_MESSAGE:
        // 已撤回的消息不可再次撤回
        if (!context.message || context.message->isRecalled()) return false;
        // 普通成员仅限撤回本人消息；ADMIN / OWNER 可撤回任意消息
        if (!isPrivileged(context)) {
            const auto& sender = context.message->getSender();
            if (!sender || !context.operatorUser ||
                sender->getId() != context.operatorUser->getId())
                return false;
        }
        // 撤回受 GroupConfigFH::recallTimeLimit 时间窗约束（含边界：<= 允许）
        return context.now <= context.message->getSentAt() + cfg.getRecallTimeLimit();
    default:
        return true;
    }
}

bool AbstractGroupPolicyFH::isPrivileged(const GroupContextFH& context) const {
    const auto role = context.group->getRole(context.operatorUser);
    return role && (*role == GroupRoleFH::OWNER || *role == GroupRoleFH::ADMIN);
}
