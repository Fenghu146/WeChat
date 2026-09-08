#pragma once
// ============================================================
// GroupContextFH —— 一次权限判断所需的最小上下文（作者代号：FH）
// ------------------------------------------------------------
// 只保存单次授权所需字段：群指针（非拥有）、操作者、目标、
// 相关消息、判断时间。不采用万能参数对象或任意参数字典；
// 业务参数（新群名、禁言开关等）由 GroupFH 方法直接处理。
// ============================================================
#include <chrono>
#include <memory>

#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"

class GroupFH;  // 前向声明，避免循环包含；group 为“非拥有”指针

struct GroupContextFH {
    GroupFH* group{};                              // 非拥有指针，避免循环引用
    std::shared_ptr<UserFH> operatorUser;          // 操作者（不允许为空）
    std::shared_ptr<UserFH> target;                // 操作目标（部分操作可为空）
    std::shared_ptr<MessageFH> message;            // 相关消息（部分操作可为空）
    std::chrono::system_clock::time_point now{};   // 判断时刻，由 GroupFH 注入
};
