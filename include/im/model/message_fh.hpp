#pragma once
// ============================================================
// MessageFH —— 群消息实体（作者代号：FH）
// ------------------------------------------------------------
// 字段：消息 ID、发送者、内容、发送时间、是否已撤回。
// 撤回时间窗比较发生在策略层（基于 GroupContextFH::now），
// 本类只提供状态标记方法 recall()。
// ============================================================
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

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

    const std::string& getId() const noexcept { return id_; }
    const std::shared_ptr<UserFH>& getSender() const noexcept { return sender_; }
    const std::string& getContent() const noexcept { return content_; }
    std::chrono::system_clock::time_point getSentAt() const noexcept { return sentAt_; }
    bool isRecalled() const noexcept { return recalled_; }

    // 撤回：由 GroupFH::recallMessage 在授权通过后调用，仅标记一次
    void recall() noexcept { recalled_ = true; }

private:
    std::string id_;
    std::shared_ptr<UserFH> sender_;
    std::string content_;
    std::chrono::system_clock::time_point sentAt_;
    bool recalled_{false};
};
