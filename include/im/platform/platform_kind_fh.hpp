#pragma once
// ============================================================
// PlatformKindFH —— 微X 产品平台枚举（作者代号：FH）
// ------------------------------------------------------------
// 对应课程任务书“多产品(微X)体系”：以 QQ、微信、微博为主，
// 通过 COUNT 哨兵预留后续扩展（如微商/微唱）。
// 关键规则(任务书原文意)：
//   1) QQ 与微博【共享同一用户号码(ID)】；
//   2) 微信使用【独立号码】，但可绑定一个 QQ 号；
//   3) 用户可自选开通 N 个微X 服务。
// ============================================================

enum class PlatformKindFH {
    QQ,      // QQ：老牌即时通信，与微博共享 ID
    WeChat,  // 微信：独立 ID，可绑定 QQ
    Weibo,   // 微博：与 QQ 共享 ID
    COUNT,   // 数量哨兵：新增微X 服务时在 COUNT 前插入
};

// 供 std::set<std::set<PlatformKindFH>> 等容器排序使用
inline bool operator<(PlatformKindFH lhs, PlatformKindFH rhs) noexcept {
    return static_cast<int>(lhs) < static_cast<int>(rhs);
}

// 是否为已定义的合法平台（排除 COUNT 哨兵）
inline bool isValidPlatformFH(PlatformKindFH p) noexcept {
    return p >= PlatformKindFH::QQ && p < PlatformKindFH::COUNT;
}

// 是否为“QQ 同号族”：QQ 与微博共享 ID；微信为独立 ID 族
inline bool isQQFamilyPlatformFH(PlatformKindFH p) noexcept {
    return p == PlatformKindFH::QQ || p == PlatformKindFH::Weibo;
}

// 平台中文展示名
inline const char* toZhName(PlatformKindFH p) {
    switch (p) {
        case PlatformKindFH::QQ:    return "QQ";
        case PlatformKindFH::WeChat: return "微信";
        case PlatformKindFH::Weibo:  return "微博";
        default:                     return "未知服务";
    }
}

// 该平台的号码体系说明（用于演示提示）
inline const char* idRuleZhFH(PlatformKindFH p) {
    return isQQFamilyPlatformFH(p) ? "与 QQ 共享同一号码(ID)" : "独立号码(ID)，可绑定 QQ";
}
