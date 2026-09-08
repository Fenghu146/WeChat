#pragma once
// ============================================================
// DiscussionGroupFH —— QQ 临时讨论组（作者代号：FH）
// ------------------------------------------------------------
// 职责：演示 QQ 特有的轻量“临时讨论组”：
//   - 容量小（默认 20 人），不属于正式群号体系；
//   - 创建者为发起人；与正式群“需 ADMIN+ 才能邀请”不同，
//     讨论组【任何成员都能邀请】好友进入（临时协作属性）；
//   - 成员可自由退出；只有发起人能解散；
//   - 微信无“临时讨论组”概念 —— 本类是平台差异的演示载体。
// 组内成员记录 QQ 号（讨论组仅存在于 QQ 平台）。
// ============================================================
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

class DiscussionGroupFH {
public:
    static constexpr std::size_t kCapacity = 20;  // 讨论组人数上限

    DiscussionGroupFH(std::string id, std::string name,
                      std::string creatorQqId)
        : id_(std::move(id)),
          name_(std::move(name)),
          creatorId_(std::move(creatorQqId)) {
        if (id_.empty() || name_.empty() || creatorId_.empty())
            throw std::invalid_argument("DiscussionGroupFH: 讨论组信息非法");
        memberIds_.push_back(creatorId_);  // 创建者天然在组
    }

    // 邀请入组：QQ 讨论组特性——任何在组成员都可邀请新成员
    bool invite(const UserProfileFH& operatorUser,
                const UserProfileFH& target) {
        if (disbanded_) return false;
        const std::string opId = operatorUser.platformAccountId(PlatformKindFH::QQ);
        const std::string targetId = target.platformAccountId(PlatformKindFH::QQ);
        if (opId.empty() || targetId.empty() || opId == targetId) return false;
        if (!contains(opId)) return false;       // 邀请者须在组内
        if (contains(targetId)) return false;    // 成员唯一
        if (memberIds_.size() >= kCapacity) return false;
        memberIds_.push_back(targetId);
        return true;
    }

    // 成员自由退组
    bool quit(const UserProfileFH& user) {
        if (disbanded_) return false;
        const std::string id = user.platformAccountId(PlatformKindFH::QQ);
        for (auto it = memberIds_.begin(); it != memberIds_.end(); ++it) {
            if (*it == id) {
                memberIds_.erase(it);
                return true;
            }
        }
        return false;
    }

    // 解散：仅发起人可执行；解散后一切操作返回 false
    bool disband(const UserProfileFH& operatorUser) {
        if (disbanded_) return false;
        const std::string opId = operatorUser.platformAccountId(PlatformKindFH::QQ);
        if (opId.empty() || opId != creatorId_) return false;
        disbanded_ = true;
        memberIds_.clear();
        return true;
    }

    // ---------- 查询 ----------
    const std::string& getId() const noexcept { return id_; }
    const std::string& getName() const noexcept { return name_; }
    const std::string& getCreatorId() const noexcept { return creatorId_; }
    bool isDisbanded() const noexcept { return disbanded_; }
    std::size_t size() const noexcept { return memberIds_.size(); }
    std::size_t capacity() const noexcept { return kCapacity; }
    bool contains(const std::string& qqId) const {
        for (const std::string& id : memberIds_)
            if (id == qqId) return true;
        return false;
    }
    const std::vector<std::string>& memberIds() const noexcept {
        return memberIds_;
    }

private:
    std::string id_;
    std::string name_;
    std::string creatorId_;             // 发起人 QQ 号
    std::vector<std::string> memberIds_;
    bool disbanded_ = false;
};
