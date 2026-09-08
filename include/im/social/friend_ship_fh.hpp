#pragma once
// ============================================================
// FriendShipFH —— 一条好友/关注关系记录（值对象，作者代号：FH）
// ------------------------------------------------------------
// 按“平台 × 账号对”记录一条社交关系（任务书优化(7)：类独立文件）：
//   - mutual=true  双向好友（QQ/微信，成对存储 A→B 与 B→A）；
//   - mutual=false 单向关注（微博，仅存 A→B）；
//   - remark 仅对双向好友有意义（好友备注名，视角属于 owner）。
// ============================================================
#include <string>

#include "im/platform/platform_kind_fh.hpp"

struct FriendShipFH {
    PlatformKindFH platform = PlatformKindFH::QQ;  // 所在微X 平台
    std::string ownerId;   // 关系发起者在该平台的账号号码
    std::string peerId;    // 对方在该平台的账号号码
    bool mutual = false;   // true=双向好友(QQ/微信)；false=单向关注(微博)
    std::string remark;    // 备注名（可选）

    FriendShipFH() = default;
    FriendShipFH(PlatformKindFH p, std::string owner, std::string peer,
                 bool isMutual, std::string remarkName = {})
        : platform(p),
          ownerId(std::move(owner)),
          peerId(std::move(peer)),
          mutual(isMutual),
          remark(std::move(remarkName)) {}
};
