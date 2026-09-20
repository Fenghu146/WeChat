#pragma once
// ============================================================
// LoginManagerFH —— 登录/退出管理（作者代号：FH）
// ------------------------------------------------------------
// 职责：受理“登录某微X 服务”请求并维护在线状态。
// 规则(任务书第5点)：
//   - 只能登录本人【已开通】的服务，否则登录失败；
//   - 「各微 X 之间只要有一个服务登录，则其它服务【简单确认后】视为自动登录」：
//     · login() 只把【目标服务】置为在线（“一个服务登录”）；
//     · confirmLink() 即任务书所述“简单确认”——确认后把本人全部已开通
//       服务一并视为登录（在线），落实“简单确认后视为自动登录”。
//   - 退出为单服务操作：仅将该服务置为离线，其余不受影响。
// 本类无状态，在线状态记录在 UserProfileFH::online_ 中。
// ============================================================
#include <set>

#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

class LoginManagerFH {
public:
    LoginManagerFH() = default;

    // 登录指定服务：仅目标服务上线（任务书第5点“一个服务登录”）。
    // 前提：该服务已开通；否则返回 false。
    // 其余已开通服务不会在此自动上线，须由调用方显式调用 confirmLink()。
    bool login(UserProfileFH& user, PlatformKindFH platform) const noexcept {
        if (!user.isActivated(platform)) return false;  // 未开通则无法登录
        user.setOnline(platform, true);
        return true;
    }

    // 简单确认（任务书第5点“简单确认后视为自动登录”）：
    // 确认后，本人全部已开通服务视为已登录（在线）。
    // 返回本次新上线的服务数量（已在线的计 0；无已开通服务返回 0）。
    int confirmLink(UserProfileFH& user) const noexcept {
        int newly = 0;
        for (PlatformKindFH each : user.activatedPlatforms())
            if (user.setOnline(each, true)) ++newly;
        return newly;
    }

    // 单服务退出登录
    bool logout(UserProfileFH& user, PlatformKindFH platform) const noexcept {
        if (!user.isOnline(platform)) return false;
        return user.setOnline(platform, false);
    }

    // 退出全部服务（如主动下线场景）
    void logoutAll(UserProfileFH& user) const noexcept { user.clearOnline(); }

    bool isOnline(const UserProfileFH& user, PlatformKindFH platform) const noexcept {
        return user.isOnline(platform);
    }
    const std::set<PlatformKindFH>& onlinePlatforms(const UserProfileFH& user) const noexcept {
        return user.onlinePlatforms();
    }
};
