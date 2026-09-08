#pragma once
// ============================================================
// WeChatPolicyFH —— 微信群策略（作者代号：FH）
// ------------------------------------------------------------
// 只保留微信平台差异（见设计文档 §3.3.2 平台差异表）：
//   INVITE_MEMBER：仅 ADMIN+ 可邀请，普通成员禁止；
//   SET_ALL_MUTE ：仅 OWNER 可执行（微信群以群主为核心）。
// ============================================================
#include "im/policy/abstract_group_policy_fh.hpp"

class WeChatPolicyFH final : public AbstractGroupPolicyFH {
protected:
    bool checkPlatformRule(ActionFH action, const GroupContextFH& context) const override;
};
