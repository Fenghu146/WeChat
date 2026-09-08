#pragma once
// ============================================================
// UserRegistryFH —— 用户(自然人)注册中心（作者代号：FH）
// ------------------------------------------------------------
// 职责：维护全部自然人的注册档案，保证号码全局唯一，并提供
// 按 QQ 号 / 微信号的跨平台查询。
// 规则(任务书)：
//   - QQ 与微博共享同一号码：注册即以主号同时获得两平台身份；
//   - 微信号独立，需另行绑定到某自然人，且号码不可与既有微信
//     号重复；
//   - “微信绑定 QQ”在档案上体现为主号归属关系。
// 号码重复注册 / 平台账号缺失将抛出 std::invalid_argument。
// ============================================================
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "im/platform/account_info_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

class UserRegistryFH {
public:
    using ProfilePtr = std::shared_ptr<UserProfileFH>;

    // 注册一个自然人：注册即同时拥有 QQ 与微博身份（同号）
    ProfilePtr registerUser(const std::string& qqId, const std::string& nickname,
                            const std::string& birthday, const std::string& location,
                            int enrollYear) {
        if (byQQ_.count(qqId) > 0)
            throw std::invalid_argument("UserRegistryFH: QQ 号码已被注册");
        auto profile = std::make_shared<UserProfileFH>(
            qqId, nickname, birthday, location, enrollYear);
        users_.push_back(profile);
        byQQ_.emplace(qqId, profile);
        return profile;
    }

    // 为自然人的“微信平台”绑定一个独立微信号
    bool bindWeChat(const ProfilePtr& profile, const std::string& wechatId) {
        if (!profile) return false;
        if (byWx_.count(wechatId) > 0) return false;       // 微信号全局唯一
        if (!profile->bindWeChat(wechatId)) return false;  // 已绑定则失败
        byWx_.emplace(wechatId, profile);
        return true;
    }

    // ---------- 查询 ----------
    ProfilePtr findByQQId(const std::string& qqId) const {
        auto it = byQQ_.find(qqId);
        return it == byQQ_.end() ? nullptr : it->second;
    }
    ProfilePtr findByWeChatId(const std::string& wechatId) const {
        auto it = byWx_.find(wechatId);
        return it == byWx_.end() ? nullptr : it->second;
    }
    std::size_t count() const noexcept { return users_.size(); }

    // 生成某平台上该自然人的账号资料视图（号码缺失则抛异常）
    AccountInfoFH makeAccount(const UserProfileFH& user, PlatformKindFH platform) const {
        std::string accountId;
        switch (platform) {
            case PlatformKindFH::QQ:     accountId = user.getQQId(); break;
            case PlatformKindFH::Weibo:  accountId = user.getWeiboId(); break;
            case PlatformKindFH::WeChat: accountId = user.getWeChatId(); break;
            default:                     break;
        }
        if (accountId.empty())
            throw std::invalid_argument("UserRegistryFH: 该自然人缺少对应平台账号");
        AccountInfoFH account(platform, accountId, user.getNickname(),
                              user.getBirthday(), user.getLocation(),
                              user.getEnrollYear());
        if (platform == PlatformKindFH::WeChat && !user.getQQId().empty())
            account.bindQQ(user.getQQId());  // 微信账号携带“绑定 QQ”
        return account;
    }

private:
    std::vector<ProfilePtr> users_;                     // 全部自然人档案
    std::unordered_map<std::string, ProfilePtr> byQQ_;  // 主号 → 档案
    std::unordered_map<std::string, ProfilePtr> byWx_;  // 微信号 → 档案
};
