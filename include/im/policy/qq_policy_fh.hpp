#pragma once
// ============================================================
// QQPolicyFH —— QQ 群策略（作者代号：FH）
// ------------------------------------------------------------
// 只保留 QQ 平台差异（见设计文档 §3.3.2 平台差异表）：
//   INVITE_MEMBER：memberInviteEnabled 开启时普通成员可邀请，否则仅 ADMIN+；
//   SET_ALL_MUTE ：ADMIN+ 可执行。
// 其余规则全部由 AbstractGroupPolicyFH 公共步骤处理。
// ============================================================
#include "im/policy/abstract_group_policy_fh.hpp"

class QQPolicyFH final : public AbstractGroupPolicyFH {
protected:
    bool checkPlatformRule(ActionFH action, const GroupContextFH& context) const override;
};
