#pragma once
// ============================================================
// AbstractGroupPolicyFH —— 抽象群策略（作者代号：FH）
// ------------------------------------------------------------
// Template Method：isAllowed() 为 final 模板方法，固定六步授权链，
// 两平台完全一致的规则全部上提到本类，避免子类重复实现：
//
//   1) validateContext      上下文合法性（群 / 操作者 / target / message）
//   2) checkMembership      成员关系（操作者在群内、目标在/不在群）
//   3) checkPermission      公共角色权限矩阵
//   4) checkTargetPermission 目标权限（不能操作同级或更高角色）
//   5) checkStateRules      公共状态约束（禁言、撤回时间窗与归属）
//   6) checkPlatformRule    平台差异（唯一允许子类实现的钩子）
//
// 规则：子类（QQPolicyFH / WeChatPolicyFH）只实现最后一步。
// ============================================================
#include <cstddef>

#include "im/context/action_fh.hpp"
#include "im/context/group_context_fh.hpp"
#include "im/policy/group_policy_fh.hpp"

class AbstractGroupPolicyFH : public GroupPolicyFH {
public:
    ~AbstractGroupPolicyFH() override = default;

    // final 模板方法：固定判断顺序，子类不可覆写
    bool isAllowed(ActionFH action, const GroupContextFH& context) const final;

    // 默认解释：读取群配置的 maxMembers（平台无硬上限）
    std::size_t getMaxGroupSize(const GroupFH& group) const override;

protected:
    // 唯一允许子类实现的钩子：平台差异规则
    virtual bool checkPlatformRule(ActionFH action,
                                   const GroupContextFH& context) const = 0;

    // —— 公共授权步骤：子类只调用、不覆写 ——
    bool validateContext(ActionFH action, const GroupContextFH& context) const;
    bool checkMembership(ActionFH action, const GroupContextFH& context) const;
    bool checkPermission(ActionFH action, const GroupContextFH& context) const;
    bool checkTargetPermission(ActionFH action, const GroupContextFH& context) const;
    bool checkStateRules(ActionFH action, const GroupContextFH& context) const;

    // 是否管理员/群主（特权角色）
    bool isPrivileged(const GroupContextFH& context) const;
};
