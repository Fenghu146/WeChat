#pragma once
// ============================================================
// MessageKindFH —— 群消息类型枚举（作者代号：FH）
// ------------------------------------------------------------
// 群消息扩展（阶段 D）：将消息从“纯文本”扩展为多种内容类型，
// 供平台消息策略对“允许发送的类型”做差异化校验。
// 课程简化口径：只覆盖文本/图片/文件/语音/表情五种基础类型。
// ============================================================

enum class MessageKindFH {
    TEXT,   // 文本消息（三平台均支持）
    IMAGE,  // 图片消息
    FILE,   // 文件消息
    VOICE,  // 语音消息
    EMOJI,  // 表情消息
};

// 消息类型中文展示名
inline const char* kindToZhName(MessageKindFH kind) {
    switch (kind) {
        case MessageKindFH::TEXT:  return "文本";
        case MessageKindFH::IMAGE: return "图片";
        case MessageKindFH::FILE:  return "文件";
        case MessageKindFH::VOICE: return "语音";
        case MessageKindFH::EMOJI: return "表情";
        default:                   return "其他";
    }
}
