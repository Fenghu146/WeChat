#pragma once
// ============================================================
// ActionFH —— 需要被策略判断的操作类型（作者代号：FH）
// ------------------------------------------------------------
// 冻结接口之一：11 种群操作。新增操作需同步维护策略框架
// 与权限矩阵（见设计文档 §3.3）。
// ============================================================

enum class ActionFH {
    SEND_MESSAGE,            // 发送消息
    RECALL_MESSAGE,          // 撤回消息
    INVITE_MEMBER,           // 邀请成员
    KICK_MEMBER,             // 踢出成员
    MUTE_MEMBER,             // 禁言 / 解除禁言成员
    EDIT_GROUP,              // 修改群名称
    PUBLISH_ANNOUNCEMENT,    // 发布群公告
    SET_ALL_MUTE,            // 设置 / 解除全员禁言
    ASSIGN_ADMIN,            // 任命 / 撤销管理员（仅群主）
    TRANSFER_OWNER,          // 转让群主
    DISBAND_GROUP            // 解散群组
};
