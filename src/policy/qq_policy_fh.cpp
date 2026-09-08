// ============================================================
// QQPolicyFH 实现（作者代号：FH）
// QQ 平台差异（对应设计文档 §3.3.2 平台差异表 QQ 列）
// ============================================================
#include "im/policy/qq_policy_fh.hpp"

#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"

bool QQPolicyFH::checkPlatformRule(ActionFH action,
                                   const GroupContextFH& c) const {
    const auto& cfg = c.group->getConfig();
    switch (action) {
    case ActionFH::INVITE_MEMBER:
        // QQ：普通成员邀请由群配置开关控制；关闭时仅 ADMIN+ 可邀请
        return cfg.isMemberInviteEnabled() || isPrivileged(c);
    case ActionFH::SET_ALL_MUTE:
        // QQ：管理员及以上可设置全员禁言
        return isPrivileged(c);
    default:
        return true;  // 无平台差异的操作一律放行（交给公共步骤裁决）
    }
}
