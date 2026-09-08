#pragma once
// ============================================================
// UserFH —— 用户身份类（作者代号：FH）
// ------------------------------------------------------------
// 职责：保存平台内一个用户的最小身份（号码 ID 与昵称）。
// 注意：角色【不属于】UserFH —— 同一用户在不同群可有不同角色，
//       群内角色只存在于 GroupMembershipFH（见 group_membership_fh.hpp）。
// 构造期参数非法（空 ID / 空昵称）抛出 std::invalid_argument。
// ============================================================
#include <stdexcept>
#include <string>
#include <utility>

class UserFH {
public:
    UserFH() = default;

    UserFH(std::string id, std::string nickname)
        : id_(std::move(id)), nickname_(std::move(nickname)) {
        if (id_.empty() || nickname_.empty())
            throw std::invalid_argument("UserFH: id and nickname must not be empty");
    }

    const std::string& getId() const noexcept { return id_; }
    const std::string& getNickname() const noexcept { return nickname_; }

    void setNickname(const std::string& nickname) {
        if (!nickname.empty()) nickname_ = nickname;
    }

private:
    std::string id_;
    std::string nickname_;
};
