// ============================================================
// WeChatPolicyFH —— 微信群策略（作者代号：FH）
// ------------------------------------------------------------
// 只保留微信平台差异（对照课程任务书 3.(3)：
// “QQ 群有以群主为核心的管理员制度，而微信群仅有群主为特权账号”）：
//   INVITE_MEMBER ：仅 OWNER 可邀请（推荐加入），ADMIN/成员均禁止；
//   SET_ALL_MUTE  ：仅 OWNER 可执行；
//   KICK_MEMBER   ：仅 OWNER 可执行（管理员不是特权账号）；
//   MUTE_MEMBER   ：仅 OWNER 可执行（同上）。
// 收窄 KICK/MUTE 的原因：GroupFH::switchPolicy 可以把 QQ 群动态切成微信
// 模式，而成员表里的 ADMIN 身份会原样保留。若不在平台钩子里收窄，就会出现
// “切换后管理员仍能踢人/禁言”，与注册表层的微信群口径
// （GroupRegistryFH::kickMember：仅群主）自相矛盾。
// 注意：EDIT_GROUP / PUBLISH_ANNOUNCEMENT 仍按本项目既有约定视为
// 「平台无关的公共操作」（见 abstract_policy_test 的
// CommonActionsEqualAcrossPlatforms），不在本平台钩子里收窄。
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
    default:
        // 无平台差异的操作一律放行（交给公共步骤裁决）。
        // EDIT_GROUP / PUBLISH_ANNOUNCEMENT 被本项目定义为「平台无关的
        // 公共操作」（见 abstract_policy_test 的 CommonActionsEqualAcrossPlatforms），
        // 群主口径的踢人限制在群注册表层（GroupRegistryFH::kickMember）亦有实现。
        return true;
    }
}
