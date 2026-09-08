#pragma once
// ============================================================
// FriendRegistryFH —— 好友 / 关注注册表（作者代号：FH）
// ------------------------------------------------------------
// 职责：管理自然人之间在某微X 平台上的社交关系。关系【按平台
// 隔离】—— 两个自然人在 QQ 的好友关系不影响微信/微博关系。
// 任务书口径：用户信息含“好友列表”，且多产品好友体系按平台
// 各自独立。
// 平台差异（规则在本类集中表达）：
//   - QQ / 微信：双向好友（加为好友后双方互为好友，需双方都有
//     该平台账号，如微信须已绑定微信号）；
//   - 微博：单向“关注”模型，关注 ≠ 好友；
//   - 不允许对自己添加关系；同一对关系重复添加返回 false。
// 演示规模下关系以线性表存储，查询 O(n)，不做过度设计。
// ============================================================
#include <algorithm>
#include <string>
#include <vector>

#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

// 一条好友/关注关系记录（值对象）
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

class FriendRegistryFH {
public:
    FriendRegistryFH() = default;

    // QQ / 微信：将两人加为双向好友
    bool makeFriends(const UserProfileFH& a, const UserProfileFH& b,
                     PlatformKindFH platform) {
        if (platform == PlatformKindFH::Weibo) return false;  // 微博请走关注
        return addBothWays(a, b, platform, /*mutual=*/true);
    }

    // 删除 QQ / 微信双向好友（成对删除）
    bool unfriend(const UserProfileFH& a, const UserProfileFH& b,
                  PlatformKindFH platform) {
        const std::string aId = a.platformAccountId(platform);
        const std::string bId = b.platformAccountId(platform);
        if (aId.empty() || bId.empty() || aId == bId) return false;
        const FriendShipFH* edge = findEdge(aId, bId, platform);
        if (!edge || !edge->mutual) return false;  // 不是双向好友
        eraseEdge(aId, bId, platform);
        eraseEdge(bId, aId, platform);
        return true;
    }

    // 微博：a 单向关注 b
    bool follow(const UserProfileFH& a, const UserProfileFH& b) {
        if (a.platformAccountId(PlatformKindFH::Weibo).empty() ||
            b.platformAccountId(PlatformKindFH::Weibo).empty())
            return false;
        return addOneWay(a, b, PlatformKindFH::Weibo);
    }

    // 微博：取消关注
    bool unfollow(const UserProfileFH& a, const UserProfileFH& b) {
        const std::string aId = a.platformAccountId(PlatformKindFH::Weibo);
        const std::string bId = b.platformAccountId(PlatformKindFH::Weibo);
        if (aId.empty() || bId.empty() || aId == bId) return false;
        const FriendShipFH* edge = findEdge(aId, bId, PlatformKindFH::Weibo);
        if (!edge || edge->mutual) return false;
        eraseEdge(aId, bId, PlatformKindFH::Weibo);
        return true;
    }

    // ---------- 查询 ----------
    // 两人在该平台是否互为好友
    bool isFriend(const UserProfileFH& a, const UserProfileFH& b,
                  PlatformKindFH platform) const {
        const std::string aId = a.platformAccountId(platform);
        const std::string bId = b.platformAccountId(platform);
        const FriendShipFH* edge = findEdge(aId, bId, platform);
        return edge && edge->mutual;
    }
    // a 是否在微博关注了 b
    bool isFollowing(const UserProfileFH& a, const UserProfileFH& b) const {
        const std::string aId = a.platformAccountId(PlatformKindFH::Weibo);
        const std::string bId = b.platformAccountId(PlatformKindFH::Weibo);
        return findEdge(aId, bId, PlatformKindFH::Weibo) != nullptr;
    }
    // 某人在某平台的“好友”列表（仅双向好友；微博好友为空、用关注查询）
    std::vector<std::string> friendIds(const UserProfileFH& user,
                                       PlatformKindFH platform) const {
        const std::string id = user.platformAccountId(platform);
        std::vector<std::string> result;
        for (const FriendShipFH& e : edges_)
            if (e.platform == platform && e.ownerId == id && e.mutual)
                result.push_back(e.peerId);
        return result;
    }
    // 某人在微博的“关注”列表
    std::vector<std::string> followingIds(const UserProfileFH& user) const {
        const std::string id = user.platformAccountId(PlatformKindFH::Weibo);
        std::vector<std::string> result;
        for (const FriendShipFH& e : edges_)
            if (e.platform == PlatformKindFH::Weibo && e.ownerId == id &&
                !e.mutual)
                result.push_back(e.peerId);
        return result;
    }
    std::size_t edgeCount() const noexcept { return edges_.size(); }

private:
    // 通用校验：目标平台、双方拥有该平台账号、非自己、无重复
    bool checkTargets(const UserProfileFH& a, const UserProfileFH& b,
                      PlatformKindFH platform) const {
        if (!isValidPlatformFH(platform) ||
            platform == PlatformKindFH::COUNT)
            return false;
        const std::string aId = a.platformAccountId(platform);
        const std::string bId = b.platformAccountId(platform);
        if (aId.empty() || bId.empty() || aId == bId) return false;
        return findEdge(aId, bId, platform) == nullptr;
    }

    bool addOneWay(const UserProfileFH& a, const UserProfileFH& b,
                   PlatformKindFH platform) {
        if (!checkTargets(a, b, platform)) return false;
        edges_.emplace_back(platform, a.platformAccountId(platform),
                            b.platformAccountId(platform), /*mutual=*/false);
        return true;
    }

    bool addBothWays(const UserProfileFH& a, const UserProfileFH& b,
                     PlatformKindFH platform, bool mutual) {
        if (!checkTargets(a, b, platform)) return false;
        const std::string aId = a.platformAccountId(platform);
        const std::string bId = b.platformAccountId(platform);
        edges_.emplace_back(platform, aId, bId, mutual);
        edges_.emplace_back(platform, bId, aId, mutual);
        return true;
    }

    const FriendShipFH* findEdge(const std::string& owner,
                                 const std::string& peer,
                                 PlatformKindFH platform) const {
        for (const FriendShipFH& e : edges_)
            if (e.platform == platform && e.ownerId == owner &&
                e.peerId == peer)
                return &e;
        return nullptr;
    }

    void eraseEdge(const std::string& owner, const std::string& peer,
                   PlatformKindFH platform) {
        edges_.erase(
            std::remove_if(edges_.begin(), edges_.end(),
                           [&](const FriendShipFH& e) {
                               return e.platform == platform &&
                                      e.ownerId == owner && e.peerId == peer;
                           }),
            edges_.end());
    }

    std::vector<FriendShipFH> edges_;  // 全部好友/关注关系
};
