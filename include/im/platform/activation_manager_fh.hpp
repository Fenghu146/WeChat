#pragma once
// ============================================================
// ActivationManagerFH —— 开通管理（作者代号：FH）
// ------------------------------------------------------------
// 职责：按资格规则受理“开通/取消开通某微X 服务”的请求。
// 规则(任务书)：
//   1) 用户可自选开通 N 个微X 服务（各平台开通互相独立）；
//   2) QQ / 微博：自然人注册即拥有共享主号，具备开通资格；
//   3) 微信：必须先绑定微信号，才有资格开通微信服务；
//   4) 重复开通返回失败（幂等）；
//   5) 取消开通时若该服务仍在登录(在线)状态则拒绝，须先退出。
// 本类无状态，全部规则施加于传入的 UserProfileFH。
// ============================================================
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

class ActivationManagerFH {
public:
    ActivationManagerFH() = default;

    // 开通一个微X 服务
    bool activate(UserProfileFH& user, PlatformKindFH platform) const noexcept {
        if (!isValidPlatformFH(platform)) return false;
        if (user.isActivated(platform)) return false;              // 已开通
        if (platform == PlatformKindFH::WeChat &&
            !user.hasWeChatAccount())                             // 微信需先绑定
            return false;
        return user.addActivated(platform);
    }

    // 取消开通一个微X 服务（须先退出该服务登录）
    bool deactivate(UserProfileFH& user, PlatformKindFH platform) const noexcept {
        if (!user.isActivated(platform)) return false;             // 未开通
        if (user.isOnline(platform)) return false;                 // 在线不可取消
        return user.removeActivated(platform);
    }

    // 查询该自然人已开通的服务数量
    int activatedCount(const UserProfileFH& user) const noexcept {
        return static_cast<int>(user.activatedPlatforms().size());
    }
};
