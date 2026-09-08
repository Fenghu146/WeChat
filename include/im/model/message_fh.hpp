#pragma once
// ============================================================
// MessageFH —— 群消息实体（作者代号：FH）
// ------------------------------------------------------------
// 字段：消息 ID、发送者、内容、消息类型、发送时间、是否已撤回。
// 撤回时间窗比较发生在策略层（基于 GroupContextFH::now），
// 本类只提供状态标记方法 recall()。
// 阶段 D 扩展：消息携带 MessageKindFH 类型（缺省为文本），
// 平台的类型/长度/引用能力校验见 im/message/platform_message_policy_fh.hpp。
// ============================================================
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "im/message/message_kind_fh.hpp"
#include "im/model/user_fh.hpp"

class MessageFH {
public:
    MessageFH() = default;

    // sentAt 缺省为当前系统时间；测试需要复现时间窗边界时可显式传入固定时刻
    MessageFH(std::string id, std::shared_ptr<UserFH> sender, std::string content,
              std::chrono::system_clock::time_point sentAt =
                  std::chrono::system_clock::now())
        : id_(std::move(id)),
          sender_(std::move(sender)),
          content_(std::move(content)),
          sentAt_(sentAt) {
        if (id_.empty() || content_.empty() || !sender_)
            throw std::invalid_argument(
                "MessageFH: id, content and sender are required");
    }

    // 阶段 D 扩展：构造时可声明消息类型（缺省仍为文本，不破坏旧调用）
    MessageFH(std::string id, std::shared_ptr<UserFH> sender,
              std::string content, MessageKindFH kind)
        : MessageFH(std::move(id), std::move(sender), std::move(content)) {
        kind_ = kind;
    }

    const std::string& getId() const noexcept { return id_; }
    const std::shared_ptr<UserFH>& getSender() const noexcept { return sender_; }
    const std::string& getContent() const noexcept { return content_; }
    MessageKindFH getKind() const noexcept { return kind_; }
    const char* kindZhName() const noexcept { return kindToZhName(kind_); }
    std::chrono::system_clock::time_point getSentAt() const noexcept { return sentAt_; }
    bool isRecalled() const noexcept { return recalled_; }

    // 撤回：由 GroupFH::recallMessage 在授权通过后调用，仅标记一次
    void recall() noexcept { recalled_ = true; }

private:
    std::string id_;
    std::shared_ptr<UserFH> sender_;
    std::string content_;
    MessageKindFH kind_{MessageKindFH::TEXT};
    std::chrono::system_clock::time_point sentAt_;
    bool recalled_{false};
};
