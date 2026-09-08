// ============================================================
// WeChatPolicyFH 实现（作者代号：FH）
// 微信平台差异（对应设计文档 §3.3.2 平台差异表 WeChat 列）
// ============================================================
#include "im/policy/wechat_policy_fh.hpp"

#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"

bool WeChatPolicyFH::checkPlatformRule(ActionFH action,
                                       const GroupContextFH& c) const {
    switch (action) {
    case ActionFH::INVITE_MEMBER:
        // 微信：仅 ADMIN+ 可邀请，普通成员禁止（微信群只能推荐加入的体现）
        return isPrivileged(c);
    case ActionFH::SET_ALL_MUTE:
        // 微信：仅群主可设置全员禁言（微信群以群主为特权账号）
        return c.group->getRole(c.operatorUser) == GroupRoleFH::OWNER;
    default:
        return true;  // 无平台差异的操作一律放行（交给公共步骤裁决）
    }
}
