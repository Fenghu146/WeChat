#pragma once
// ============================================================
// UserProfileFH —— 平台自然人档案（作者代号：FH）
// ------------------------------------------------------------
// 一个“自然人”对应一份档案，是跨产品的统一身份：
//   - 主号 qqId_：QQ 号码，同时即微博号码（QQ 与微博共享 ID）；
//   - 可选微信号 wechatId_：独立号码，绑定在本档案上
//     （“微信绑定 QQ” 即指微信号属于该主号的自然人）；
//   - 资料（昵称/出生/所在地/注册年份）在自然人间共享展示。
// 本类只负责保存状态并提供查询：
//   - 开通集合 activated_：由 ActivationManagerFH 按资格修改；
//   - 在线集合 online_：由 LoginManagerFH 按联动规则修改。
// ============================================================
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "im/platform/platform_kind_fh.hpp"

class UserProfileFH {
public:
    // qqId 同时充当微博号；wechatId 可缺省（尚未绑定微信号）
    UserProfileFH(std::string qqId, std::string nickname, std::string birthday,
                  std::string location, int enrollYear,
                  std::string wechatId = "")
        : qqId_(std::move(qqId)),
          nickname_(std::move(nickname)),
          birthday_(std::move(birthday)),
          location_(std::move(location)),
          enrollYear_(enrollYear),
          wechatId_(std::move(wechatId)) {
        if (qqId_.empty() || nickname_.empty())
            throw std::invalid_argument("UserProfileFH: 主号与昵称不可为空");
    }

    // ---------- 号码体系（任务书：QQ 与微博共享 ID，微信独立） ----------
    const std::string& getQQId() const noexcept { return qqId_; }
    const std::string& getWeiboId() const noexcept { return qqId_; }  // 共享同号
    const std::string& getWeChatId() const noexcept { return wechatId_; }
    bool hasWeChatAccount() const noexcept { return !wechatId_.empty(); }

    // 为自然人绑定一个微信号（每人至多一个、号码不可为空）
    bool bindWeChat(std::string wechatId) noexcept {
        if (wechatId.empty() || !wechatId_.empty()) return false;
        wechatId_ = std::move(wechatId);
        return true;
    }

    // ---------- 基本资料 ----------
    const std::string& getNickname() const noexcept { return nickname_; }
    const std::string& getBirthday() const noexcept { return birthday_; }
    const std::string& getLocation() const noexcept { return location_; }
    int getEnrollYear() const noexcept { return enrollYear_; }
    int tAge(int currentYear) const noexcept {
        return currentYear > enrollYear_ ? currentYear - enrollYear_ : 0;
    }

    void setNickname(const std::string& nickname) {
        if (!nickname.empty()) nickname_ = nickname;
    }
    void setLocation(const std::string& location) { location_ = location; }

    // ---------- 开通集合（仅由 ActivationManagerFH 调用） ----------
    // 只做集合写入；服务资格校验在 ActivationManagerFH 中完成。
    bool addActivated(PlatformKindFH p) noexcept {
        return activated_.insert(p).second;  // 重复开通返回 false
    }
    bool removeActivated(PlatformKindFH p) noexcept {
        return activated_.erase(p) > 0;
    }
    bool isActivated(PlatformKindFH p) const noexcept {
        return activated_.count(p) > 0;
    }
    const std::set<PlatformKindFH>& activatedPlatforms() const noexcept {
        return activated_;
    }

    // ---------- 在线集合（仅由 LoginManagerFH 调用） ----------
    bool setOnline(PlatformKindFH p, bool on) noexcept {
        if (on) return online_.insert(p).second;
        return online_.erase(p) > 0;
    }
    void clearOnline() noexcept { online_.clear(); }
    bool isOnline(PlatformKindFH p) const noexcept {
        return online_.count(p) > 0;
    }
    const std::set<PlatformKindFH>& onlinePlatforms() const noexcept {
        return online_;
    }

private:
    std::string qqId_;                    // 主号：QQ（兼微博）
    std::string nickname_;                // 昵称
    std::string birthday_;                // 出生时间 yyyy-MM-dd
    std::string location_;                // 所在地
    int enrollYear_ = 0;                  // 注册年份（T 龄）
    std::string wechatId_;                // 微信号（空=未绑定）
    std::set<PlatformKindFH> activated_;  // 已开通的微X 服务
    std::set<PlatformKindFH> online_;     // 当前在线的微X 服务
};
