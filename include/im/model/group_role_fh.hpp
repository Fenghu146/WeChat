#pragma once
// ============================================================
// GroupRoleFH —— 群内角色枚举（作者代号：FH）
// ------------------------------------------------------------
// 等级规则：OWNER(3) > ADMIN(2) > MEMBER(1)。
// 仅固定三种角色，不为尚未出现的角色预留抽象。
// enum class 不支持隐式比较，故提供等级比较运算符。
// ============================================================
#include <string>

enum class GroupRoleFH { MEMBER = 1, ADMIN = 2, OWNER = 3 };

inline bool operator>(GroupRoleFH lhs, GroupRoleFH rhs) noexcept {
    return static_cast<int>(lhs) > static_cast<int>(rhs);
}

inline bool operator>=(GroupRoleFH lhs, GroupRoleFH rhs) noexcept {
    return static_cast<int>(lhs) >= static_cast<int>(rhs);
}

inline bool operator==(GroupRoleFH lhs, GroupRoleFH rhs) noexcept {
    return static_cast<int>(lhs) == static_cast<int>(rhs);
}

// 供演示程序打印角色的中文名称
inline const char* toZhName(GroupRoleFH role) noexcept {
    switch (role) {
        case GroupRoleFH::OWNER:  return "群主";
        case GroupRoleFH::ADMIN:  return "管理员";
        case GroupRoleFH::MEMBER: return "普通成员";
    }
    return "未知";
}
