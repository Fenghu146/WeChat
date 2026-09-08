#pragma once
// ============================================================
// GroupChatRecordFH —— 群聊消息记录（值对象，作者代号：FH）
// ------------------------------------------------------------
// 阶段 D 群消息扩展：一条群聊消息的留档（发送者平台账号/昵称、
// 消息类型、内容、是否引用回复、发送时间），供各产品视图渲染
// 与断电保存（任务书优化(7)：类独立文件）。
// ============================================================
#include <chrono>
#include <string>

#include "im/message/message_kind_fh.hpp"

struct GroupChatRecordFH {
    MessageKindFH kind = MessageKindFH::TEXT;   // 消息类型
    std::string senderId;    // 发送者在该平台的账号号码
    std::string senderNick;  // 发送者昵称（供各产品视图渲染）
    std::string content;     // 文本内容 / 资源描述
    bool isReply = false;    // 是否为引用回复（按平台能力校验）
    std::chrono::system_clock::time_point sentAt;
};
