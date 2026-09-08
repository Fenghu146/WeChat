#pragma once
// ============================================================
// AccountInfoFH —— 单个平台账号资料（作者代号：FH）
// ------------------------------------------------------------
// 职责：描述“某个微X 平台上一个账号”的资料，属值对象(Value
// Object)。字段对应任务书用户信息：号码 ID、昵称、出生时间、
// 所在地，并携带注册年份以计算 T 龄。
// 微信平台额外记录其绑定的 QQ 号（任务书：微信独立 ID 可绑
// 定 QQ），非微信平台不可绑定。
// 构造期非法参数（未知平台 / 空号码 / 空昵称）抛异常。
// ============================================================
#include <stdexcept>
#include <string>
#include <utility>

#include "im/platform/platform_kind_fh.hpp"

class AccountInfoFH {
public:
    AccountInfoFH() = default;

    // birthday 格式建议 yyyy-MM-dd；location 为所在地；
    // enrollYear 为注册年份，用于计算 T 龄。
    AccountInfoFH(PlatformKindFH platform, std::string accountId,
                  std::string nickname, std::string birthday,
                  std::string location, int enrollYear)
        : platform_(platform),
          accountId_(std::move(accountId)),
          nickname_(std::move(nickname)),
          birthday_(std::move(birthday)),
          location_(std::move(location)),
          enrollYear_(enrollYear) {
        if (!isValidPlatformFH(platform_))
            throw std::invalid_argument("AccountInfoFH: 未知平台");
        if (accountId_.empty() || nickname_.empty())
            throw std::invalid_argument("AccountInfoFH: 号码与昵称不可为空");
    }

    PlatformKindFH getPlatform() const noexcept { return platform_; }
    const std::string& getAccountId() const noexcept { return accountId_; }
    const std::string& getNickname() const noexcept { return nickname_; }
    const std::string& getBirthday() const noexcept { return birthday_; }
    const std::string& getLocation() const noexcept { return location_; }
    int getEnrollYear() const noexcept { return enrollYear_; }

    void setNickname(const std::string& nickname) {
        if (!nickname.empty()) nickname_ = nickname;
    }
    void setLocation(const std::string& location) { location_ = location; }

    // 微信绑定 QQ 号（仅微信平台允许，号码非空）
    bool bindQQ(const std::string& qqId) noexcept {
        if (platform_ != PlatformKindFH::WeChat || qqId.empty()) return false;
        bindQqId_ = qqId;
        return true;
    }
    const std::string& getBindQqId() const noexcept { return bindQqId_; }
    bool hasBindQQ() const noexcept { return !bindQqId_.empty(); }

    // T 龄：注册年份距当前年份的年数（供展示计算）
    int tAge(int currentYear) const noexcept {
        return currentYear > enrollYear_ ? currentYear - enrollYear_ : 0;
    }

private:
    PlatformKindFH platform_ = PlatformKindFH::QQ;
    std::string accountId_;
    std::string nickname_;
    std::string birthday_;   // 出生时间
    std::string location_;   // 所在地
    int enrollYear_ = 0;     // 注册年份（T 龄依据）
    std::string bindQqId_;   // 微信平台绑定的 QQ 号（其余平台为空）
};
