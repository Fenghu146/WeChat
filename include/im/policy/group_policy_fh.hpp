#pragma once
// ============================================================
// GroupPolicyFH —— 平台群策略接口（作者代号：FH）
// ------------------------------------------------------------
// 冻结点：平台差异只允许通过本接口及其实现类表达。
// GroupFH 依赖本接口而非具体平台，因此新增平台主要是“新增
// 一个 Policy 实现”，而不是修改 GroupFH（Strategy Pattern）。
// ============================================================
#include <cstddef>

#include "im/context/action_fh.hpp"
#include "im/context/group_context_fh.hpp"

class GroupFH;  // 前向声明

class GroupPolicyFH {
public:
    virtual ~GroupPolicyFH() = default;

    // 一次完整授权判断：某操作在给定上下文下是否被允许
    virtual bool isAllowed(ActionFH action, const GroupContextFH& context) const = 0;

    // 群人数上限的解释点：默认读取群配置；若平台有硬上限，
    // 子类可覆写为 min(configuredLimit, platformLimit)。
    virtual std::size_t getMaxGroupSize(const GroupFH& group) const = 0;
};
