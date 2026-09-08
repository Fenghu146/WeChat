#pragma once
// ============================================================
// LoginManagerFH —— 登录/退出管理（作者代号：FH）
// ------------------------------------------------------------
// 职责：受理“登录某微X 服务”请求并维护在线状态。
// 规则(任务书第5点)：
//   - 只能登录本人【已开通】的服务，否则登录失败；
//   - 任一服务登录成功后，本人其余已开通服务自动进入登录
//     (在线)状态 —— 体现“一次登录、全家在线”的联动设计；
//   - 退出为单服务操作：仅将该服务置为离线，其余不受影响。
// 本类无状态，在线状态记录在 UserProfileFH::online_ 中。
// ============================================================
#include <set>

#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

class LoginManagerFH {
public:
    LoginManagerFH() = default;

    // 登录指定服务；成功后本人全部已开通服务自动上线
    bool login(UserProfileFH& user, PlatformKindFH platform) const noexcept {
        if (!user.isActivated(platform)) return false;  // 未开通则无法登录
        for (PlatformKindFH each : user.activatedPlatforms())
            user.setOnline(each, true);  // 联动：其余已开通服务一并登录
        return true;
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
