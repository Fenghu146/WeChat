#pragma once
// ============================================================
// WeChatPolicyFH —— 微信群策略（作者代号：FH）
// ------------------------------------------------------------
// 只保留微信平台差异（对照课程任务书 3.(3)）：
//   INVITE_MEMBER：仅 OWNER 可邀请（推荐加入），ADMIN/成员均禁止；
//   SET_ALL_MUTE ：仅 OWNER 可执行；
// 微信群“仅有群主为特权账号” —— 管理员角色在微信群不产生特权
// （从 QQ 群动态切换为微信群时，成员与角色数据保留，但权限按
// 微信口径解释，即“群成员数据不受伤害地变换管理特色”）。
// ============================================================
#include "im/policy/abstract_group_policy_fh.hpp"

class WeChatPolicyFH final : public AbstractGroupPolicyFH {
protected:
    bool checkPlatformRule(ActionFH action, const GroupContextFH& context) const override;
};
