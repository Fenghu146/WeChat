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
// 任务书 2.(1)：好友信息支持添加、修改（备注名）、删除、查询；
// 任务书 2.(2) 与 6.(3)：支持“微X 之间共同好友”查询，以及跨服
// 务推荐添加好友（如微信可以添加 QQ 推荐好友）。
// 断电保存（任务书 6.(1)、优化(2)）：好友信息可在容器配置时读
// 入（setPersistencePath）、析构时写回（saveToFile/loadFromFile）。
// 演示规模下关系以线性表存储，查询 O(n)，不做过度设计。
//
// 声明与实现分离：方法实现位于 src/social/friend_registry_fh.cpp。
// ============================================================
#include <cstddef>
#include <string>
#include <vector>

#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/social/friend_ship_fh.hpp"

class FriendRegistryFH {
public:
    FriendRegistryFH() = default;

    // 实例化时读入（任务书优化(2)字面口径）：构造即从文件加载
    explicit FriendRegistryFH(const std::string& persistPath);

    // 断电保存：若配置了持久化文件，析构时写回（任务书优化(2)）
    ~FriendRegistryFH();

    // 配置持久化文件：配置（实例化）时立即读入，此后析构自动写回
    bool setPersistencePath(const std::string& path);

    // QQ / 微信：将两人加为双向好友
    bool makeFriends(const UserProfileFH& a, const UserProfileFH& b,
                     PlatformKindFH platform);

    // 删除 QQ / 微信双向好友（成对删除）
    bool unfriend(const UserProfileFH& a, const UserProfileFH& b,
                  PlatformKindFH platform);

    // 好友信息修改（任务书 2.(1)）：设置好友备注名。
    // 仅双向好友可备注（微博关注不是好友，不支持备注）。
    bool setRemark(const UserProfileFH& owner, const UserProfileFH& peer,
                   PlatformKindFH platform, const std::string& remark);

    // 查询备注名（非好友或未设置返回空串）
    std::string remarkOf(const UserProfileFH& owner, const UserProfileFH& peer,
                         PlatformKindFH platform) const;

    // 微博：a 单向关注 b
    bool follow(const UserProfileFH& a, const UserProfileFH& b);

    // 微博：取消关注
    bool unfollow(const UserProfileFH& a, const UserProfileFH& b);

    // ---------- 查询 ----------
    // 两人在该平台是否互为好友
    bool isFriend(const UserProfileFH& a, const UserProfileFH& b,
                  PlatformKindFH platform) const;
    // a 是否在微博关注了 b
    bool isFollowing(const UserProfileFH& a, const UserProfileFH& b) const;
    // 某人在某平台的“好友”列表（仅双向好友；微博好友为空、用关注查询）
    std::vector<std::string> friendIds(const UserProfileFH& user,
                                       PlatformKindFH platform) const;
    // 某人在微博的“关注”列表
    std::vector<std::string> followingIds(const UserProfileFH& user) const;
    // 共同好友（任务书 2.(2)）：两人在该平台好友列表的交集
    std::vector<std::string> commonFriends(const UserProfileFH& a,
                                           const UserProfileFH& b,
                                           PlatformKindFH platform) const;
    // 微博“共同关注”：两人关注列表的交集
    std::vector<std::string> commonFollowing(const UserProfileFH& a,
                                             const UserProfileFH& b) const;

    // ---------- 跨服务推荐添加好友（任务书 2.(2)、6.(3)） ----------
    // other 是否可由 fromPlatform 的好友关系推荐为 toPlatform 好友：
    //   1) 本人已开通来源与目标服务（任务书 6.(3)：“本人开通的”
    //      服务之间才可互推，未开通的服务不在推荐范围内）；
    //   2) 两平台不同；
    //   3) 双方均有 toPlatform 账号；
    //   4) 在 fromPlatform 已互为好友；
    //   5) 在 toPlatform 尚不是好友。
    bool isRecommendable(const UserProfileFH& user, const UserProfileFH& other,
                         PlatformKindFH fromPlatform,
                         PlatformKindFH toPlatform) const;

    // 依据 fromPlatform 上的好友关系，把 other 添加为 toPlatform 好友
    //（如：微信可以添加 QQ 推荐好友 —— QQ 已是好友 → 微信一键添加）
    bool addFriendFromRecommendation(const UserProfileFH& user,
                                     const UserProfileFH& other,
                                     PlatformKindFH fromPlatform,
                                     PlatformKindFH toPlatform);

    // 推荐列表：遍历注册中心，给出 user 可在 toPlatform 依据
    // fromPlatform 好友关系推荐添加的全部自然人。
    std::vector<const UserProfileFH*> recommendFriendsFrom(
        const UserProfileFH& user, const UserRegistryFH& registry,
        PlatformKindFH fromPlatform, PlatformKindFH toPlatform) const;

    std::size_t edgeCount() const noexcept;

    // ---------- 断电保存（好友信息） ----------
    // 行格式：F ␟ 平台 ␟ ownerId ␟ peerId ␟ mutual ␟ 备注(转义)
    // 采用原子写（先写 .tmp 再替换），写失败时保留原存档并返回 false。
    bool saveToFile(const std::string& path) const;

    // 从文件恢复好友关系（文件不存在返回 false，保持现状）。
    // 容错口径：单行损坏跳过；重复边（同平台同方向）去重；
    // 若文件有内容却一条都没解析出来（被截断/格式不符），返回 false 并
    // 保持当前关系，避免用空集合覆盖既有好友数据。
    bool loadFromFile(const std::string& path);

private:
    // 通用校验：目标平台、双方拥有该平台账号、非自己、无重复
    bool checkTargets(const UserProfileFH& a, const UserProfileFH& b,
                      PlatformKindFH platform) const;
    bool addOneWay(const UserProfileFH& a, const UserProfileFH& b,
                   PlatformKindFH platform);
    bool addBothWays(const UserProfileFH& a, const UserProfileFH& b,
                     PlatformKindFH platform, bool mutual);
    static bool containsEdge(const std::vector<FriendShipFH>& list,
                             const FriendShipFH& e);
    const FriendShipFH* findEdge(const std::string& owner,
                                 const std::string& peer,
                                 PlatformKindFH platform) const;
    FriendShipFH* findMutableEdge(const std::string& owner,
                                  const std::string& peer,
                                  PlatformKindFH platform);
    void eraseEdge(const std::string& owner, const std::string& peer,
                   PlatformKindFH platform);

    std::vector<FriendShipFH> edges_;  // 全部好友/关注关系
    std::string persistPath_;          // 断电保存文件（空=未启用）
};
