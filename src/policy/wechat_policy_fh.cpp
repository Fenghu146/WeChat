// ============================================================
// WeChatPolicyFH —— 微信群策略（作者代号：FH）
// ------------------------------------------------------------
// 平台差异口径（对照课程任务书 3.(3)：
// “QQ 群有以群主为核心的管理员制度，而微信群仅有群主为特权账号”）：
//   公共步骤判定为 ADMIN+ 的管理动作，在微信群一律收窄为【仅群主】：
//     INVITE_MEMBER、SET_ALL_MUTE、KICK_MEMBER、MUTE_MEMBER、
//     EDIT_GROUP、PUBLISH_ANNOUNCEMENT；
//   其中 EDIT_GROUP 同时是 setRecallTimeLimit / setMemberInviteEnabled
//   的授权入口，因此微信群主之外无人能改这些群配置。
// 仅群主可执行的动作（ASSIGN_ADMIN / TRANSFER_OWNER / DISBAND_GROUP）
// 由公共步骤直接判定，无需在本钩子重复。
// 与注册表层一致：GroupRegistryFH::kickMember 对微信群同样是
// “仅群主”（allowed = opOwner），两处口径现已统一。
// 注意：GroupFH::switchPolicy 可以把 QQ 群动态切到微信模式，而成员表里的
// ADMIN 身份会原样保留；若不在本钩子收窄，切换后管理员会继续享有特权，
// 与任务书措辞及注册表层结论都矛盾。
// ============================================================
#include "im/policy/wechat_policy_fh.hpp"

#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"

bool WeChatPolicyFH::checkPlatformRule(ActionFH action,
                                       const GroupContextFH& context) const {
    const auto role = context.group->getRole(context.operatorUser);
    const bool isOwner = role && *role == GroupRoleFH::OWNER;
    switch (action) {
    case ActionFH::INVITE_MEMBER:
        // 微信：仅群主可推荐加入；管理员/普通成员均禁止
        //（任务书：微信群仅有群主为特权账号）
        return isOwner;
    case ActionFH::SET_ALL_MUTE:
        // 微信：仅群主可设置全员禁言（微信群以群主为特权账号）
        return isOwner;
    case ActionFH::KICK_MEMBER:
    case ActionFH::MUTE_MEMBER:
        // 微信：没有管理员制度 —— 踢人 / 禁言同样只有群主可执行
        return isOwner;
    case ActionFH::EDIT_GROUP:
    case ActionFH::PUBLISH_ANNOUNCEMENT:
        // 微信：改群名 / 发公告同样属于“特权账号”动作，仅群主可执行
        return isOwner;
    default:
        // 其余动作无平台差异（发言、撤回等由公共步骤按角色裁决）
        return true;
    }
}

// 微信群“仅有群主为特权账号”：全员禁言豁免与代撤他人消息两项特权
// 同样收窄为仅群主 —— 从 QQ 群切换而来、仍保留 ADMIN 角色的成员
// 不再享有这两项待遇（与本策略头注释的任务书 3.(3) 口径一致）。
bool WeChatPolicyFH::isPrivileged(const GroupContextFH& context) const {
    const auto role = context.group->getRole(context.operatorUser);
    return role && *role == GroupRoleFH::OWNER;
}
