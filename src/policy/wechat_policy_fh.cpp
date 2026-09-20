// ============================================================
// WeChatPolicyFH —— 微信群策略（作者代号：FH）
// ------------------------------------------------------------
// 只保留微信平台差异（对照课程任务书 3.(3)：
// “QQ 群有以群主为核心的管理员制度，而微信群仅有群主为特权账号”）：
//   INVITE_MEMBER：仅 OWNER 可邀请（推荐加入），ADMIN/成员均禁止；
//   SET_ALL_MUTE ：仅 OWNER 可执行；
//   其余管理操作（踢人等）在注册表层同样遵循“仅群主”口径。
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
    default:
        // 无平台差异的操作一律放行（交给公共步骤裁决）。
        // 注意：EDIT_GROUP / PUBLISH_ANNOUNCEMENT / KICK_MEMBER 等
        // 被本项目定义为“平台无关的公共操作”（见 abstract_policy_test
        // 的 CommonActionsEqualAcrossPlatforms），群主口径的踢人限制在
        // 群注册表层（GroupRegistryFH::kickMember）实现。
        return true;
    }
}
