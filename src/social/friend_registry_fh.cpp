// ============================================================
// friend_registry_fh.cpp —— 好友注册表 FriendRegistryFH 的方法实现（作者代号：FH）
// 声明见 include/im/social/friend_registry_fh.hpp。
// ============================================================
#include "im/social/friend_registry_fh.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <ostream>
#include <utility>

#include "im/util/persist_util_fh.hpp"

// 实例化时读入（任务书优化(2)字面口径）：构造即从文件加载
FriendRegistryFH::FriendRegistryFH(const std::string& persistPath) {
    persistPath_ = persistPath;
    loadFromFile(persistPath);
}

// 断电保存：若配置了持久化文件，析构时写回（任务书优化(2)）
FriendRegistryFH::~FriendRegistryFH() {
    if (!persistPath_.empty()) saveToFile(persistPath_);
}

// 配置持久化文件：配置（实例化）时立即读入，此后析构自动写回
bool FriendRegistryFH::setPersistencePath(const std::string& path) {
    persistPath_ = path;
    return loadFromFile(path);
}

// QQ / 微信：将两人加为双向好友
bool FriendRegistryFH::makeFriends(const UserProfileFH& a,
                                   const UserProfileFH& b,
                                   PlatformKindFH platform) {
    if (platform == PlatformKindFH::Weibo) return false;  // 微博请走关注
    return addBothWays(a, b, platform, /*mutual=*/true);
}

// 删除 QQ / 微信双向好友（成对删除）
bool FriendRegistryFH::unfriend(const UserProfileFH& a, const UserProfileFH& b,
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

// 好友信息修改（任务书 2.(1)）：设置好友备注名。
// 仅双向好友可备注（微博关注不是好友，不支持备注）。
bool FriendRegistryFH::setRemark(const UserProfileFH& owner,
                                 const UserProfileFH& peer,
                                 PlatformKindFH platform,
                                 const std::string& remark) {
    if (platform == PlatformKindFH::Weibo) return false;
    const std::string oId = owner.platformAccountId(platform);
    const std::string pId = peer.platformAccountId(platform);
    if (oId.empty() || pId.empty() || oId == pId) return false;
    FriendShipFH* edge = findMutableEdge(oId, pId, platform);
    if (!edge || !edge->mutual) return false;
    edge->remark = remark;
    return true;
}

// 查询备注名（非好友或未设置返回空串）
std::string FriendRegistryFH::remarkOf(const UserProfileFH& owner,
                                       const UserProfileFH& peer,
                                       PlatformKindFH platform) const {
    const std::string oId = owner.platformAccountId(platform);
    const std::string pId = peer.platformAccountId(platform);
    const FriendShipFH* edge = findEdge(oId, pId, platform);
    return (edge && edge->mutual) ? edge->remark : std::string();
}

// 微博：a 单向关注 b
bool FriendRegistryFH::follow(const UserProfileFH& a, const UserProfileFH& b) {
    if (a.platformAccountId(PlatformKindFH::Weibo).empty() ||
        b.platformAccountId(PlatformKindFH::Weibo).empty())
        return false;
    return addOneWay(a, b, PlatformKindFH::Weibo);
}

// 微博：取消关注
bool FriendRegistryFH::unfollow(const UserProfileFH& a,
                                const UserProfileFH& b) {
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
bool FriendRegistryFH::isFriend(const UserProfileFH& a,
                                const UserProfileFH& b,
                                PlatformKindFH platform) const {
    const std::string aId = a.platformAccountId(platform);
    const std::string bId = b.platformAccountId(platform);
    const FriendShipFH* edge = findEdge(aId, bId, platform);
    return edge && edge->mutual;
}

// a 是否在微博关注了 b
bool FriendRegistryFH::isFollowing(const UserProfileFH& a,
                                   const UserProfileFH& b) const {
    const std::string aId = a.platformAccountId(PlatformKindFH::Weibo);
    const std::string bId = b.platformAccountId(PlatformKindFH::Weibo);
    return findEdge(aId, bId, PlatformKindFH::Weibo) != nullptr;
}

// 某人在某平台的“好友”列表（仅双向好友；微博好友为空、用关注查询）
std::vector<std::string> FriendRegistryFH::friendIds(
    const UserProfileFH& user, PlatformKindFH platform) const {
    const std::string id = user.platformAccountId(platform);
    std::vector<std::string> result;
    for (const FriendShipFH& e : edges_)
        if (e.platform == platform && e.ownerId == id && e.mutual)
            result.push_back(e.peerId);
    return result;
}

// 某人在微博的“关注”列表
std::vector<std::string> FriendRegistryFH::followingIds(
    const UserProfileFH& user) const {
    const std::string id = user.platformAccountId(PlatformKindFH::Weibo);
    std::vector<std::string> result;
    for (const FriendShipFH& e : edges_)
        if (e.platform == PlatformKindFH::Weibo && e.ownerId == id && !e.mutual)
            result.push_back(e.peerId);
    return result;
}

// 共同好友（任务书 2.(2)）：两人在该平台好友列表的交集
std::vector<std::string> FriendRegistryFH::commonFriends(
    const UserProfileFH& a, const UserProfileFH& b,
    PlatformKindFH platform) const {
    std::vector<std::string> fa = friendIds(a, platform);
    std::vector<std::string> fb = friendIds(b, platform);
    std::sort(fa.begin(), fa.end());
    std::sort(fb.begin(), fb.end());
    std::vector<std::string> result;
    std::set_intersection(fa.begin(), fa.end(), fb.begin(), fb.end(),
                          std::back_inserter(result));
    return result;
}

// 微博“共同关注”：两人关注列表的交集
std::vector<std::string> FriendRegistryFH::commonFollowing(
    const UserProfileFH& a, const UserProfileFH& b) const {
    std::vector<std::string> fa = followingIds(a);
    std::vector<std::string> fb = followingIds(b);
    std::sort(fa.begin(), fa.end());
    std::sort(fb.begin(), fb.end());
    std::vector<std::string> result;
    std::set_intersection(fa.begin(), fa.end(), fb.begin(), fb.end(),
                          std::back_inserter(result));
    return result;
}

bool FriendRegistryFH::isRecommendable(const UserProfileFH& user,
                                       const UserProfileFH& other,
                                       PlatformKindFH fromPlatform,
                                       PlatformKindFH toPlatform) const {
    if (fromPlatform == toPlatform) return false;
    if (!user.isActivated(fromPlatform) || !user.isActivated(toPlatform))
        return false;  // 本人须已开通来源与目标服务（6.(3)）
    if (!user.hasPlatformAccount(toPlatform) ||
        !other.hasPlatformAccount(toPlatform))
        return false;
    if (!isFriend(user, other, fromPlatform)) return false;
    return !isFriend(user, other, toPlatform);
}

// 依据 fromPlatform 上的好友关系，把 other 添加为 toPlatform 好友
//（如：微信可以添加 QQ 推荐好友 —— QQ 已是好友 → 微信一键添加）
bool FriendRegistryFH::addFriendFromRecommendation(
    const UserProfileFH& user, const UserProfileFH& other,
    PlatformKindFH fromPlatform, PlatformKindFH toPlatform) {
    if (!isRecommendable(user, other, fromPlatform, toPlatform)) return false;
    return makeFriends(user, other, toPlatform);
}

// 推荐列表：遍历注册中心，给出 user 可在 toPlatform 依据
// fromPlatform 好友关系推荐添加的全部自然人。
std::vector<const UserProfileFH*> FriendRegistryFH::recommendFriendsFrom(
    const UserProfileFH& user, const UserRegistryFH& registry,
    PlatformKindFH fromPlatform, PlatformKindFH toPlatform) const {
    std::vector<const UserProfileFH*> result;
    for (const auto& other : registry.allProfiles()) {
        if (other.get() == &user) continue;
        if (isRecommendable(user, *other, fromPlatform, toPlatform))
            result.push_back(other.get());
    }
    return result;
}

std::size_t FriendRegistryFH::edgeCount() const noexcept {
    return edges_.size();
}

// ---------- 断电保存（好友信息） ----------
bool FriendRegistryFH::saveToFile(const std::string& path) const {
    persist_util_fh::AtomicWriterFH writer(path);
    if (!writer.ok()) return false;
    std::ostream& out = writer.stream();
    for (const FriendShipFH& e : edges_) {
        out << 'F'
            << persist_util_fh::kFieldSepFH
            << persist_util_fh::platformName(e.platform)
            << persist_util_fh::kFieldSepFH
            << e.ownerId
            << persist_util_fh::kFieldSepFH
            << e.peerId
            << persist_util_fh::kFieldSepFH
            << (e.mutual ? 1 : 0)
            << persist_util_fh::kFieldSepFH
            << persist_util_fh::escapeTextFH(e.remark) << '\n';
    }
    return writer.commit();
}

// 从文件恢复好友关系（文件不存在返回 false，保持现状）。
// 容错口径：单行损坏跳过；重复边（同平台同方向）去重；
// 若文件有内容却一条都没解析出来（被截断/格式不符），返回 false 并
// 保持当前关系，避免用空集合覆盖既有好友数据。
bool FriendRegistryFH::loadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<FriendShipFH> loaded;
    std::string line;
    bool sawContent = false;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        sawContent = true;
        std::vector<std::string> f = persist_util_fh::splitFieldsFH(line);
        if (f.size() < 6 || f[0] != "F") continue;
        FriendShipFH e;
        if (!persist_util_fh::platformFromName(f[1], e.platform)) continue;
        if (f[2].empty() || f[3].empty()) continue;  // 端点缺失的边无意义
        e.ownerId = f[2];
        e.peerId = f[3];
        e.mutual = f[4] == "1";
        e.remark = persist_util_fh::unescapeTextFH(f[5]);
        if (containsEdge(loaded, e)) continue;  // 去重，防止重复计数
        loaded.push_back(std::move(e));
    }
    if (loaded.empty() && sawContent) return false;  // 有内容但全不可用：保持现状
    edges_ = std::move(loaded);
    return true;
}

// ---------- 私有实现 ----------
// 通用校验：目标平台、双方拥有该平台账号、非自己、无重复
bool FriendRegistryFH::checkTargets(const UserProfileFH& a,
                                    const UserProfileFH& b,
                                    PlatformKindFH platform) const {
    if (!isValidPlatformFH(platform) || platform == PlatformKindFH::COUNT)
        return false;
    const std::string aId = a.platformAccountId(platform);
    const std::string bId = b.platformAccountId(platform);
    if (aId.empty() || bId.empty() || aId == bId) return false;
    return findEdge(aId, bId, platform) == nullptr;
}

bool FriendRegistryFH::addOneWay(const UserProfileFH& a,
                                 const UserProfileFH& b,
                                 PlatformKindFH platform) {
    if (!checkTargets(a, b, platform)) return false;
    edges_.emplace_back(platform, a.platformAccountId(platform),
                        b.platformAccountId(platform), /*mutual=*/false);
    return true;
}

bool FriendRegistryFH::addBothWays(const UserProfileFH& a,
                                   const UserProfileFH& b,
                                   PlatformKindFH platform, bool mutual) {
    if (!checkTargets(a, b, platform)) return false;
    const std::string aId = a.platformAccountId(platform);
    const std::string bId = b.platformAccountId(platform);
    // 反向边可能因存档残缺而单独存在：已有则不再重复插入，
    // 否则会凭空多出一条重复关系。
    if (findEdge(bId, aId, platform)) return false;
    edges_.emplace_back(platform, aId, bId, mutual);
    edges_.emplace_back(platform, bId, aId, mutual);
    return true;
}

bool FriendRegistryFH::containsEdge(const std::vector<FriendShipFH>& list,
                                    const FriendShipFH& e) {
    for (const FriendShipFH& x : list)
        if (x.platform == e.platform && x.ownerId == e.ownerId &&
            x.peerId == e.peerId)
            return true;
    return false;
}

const FriendShipFH* FriendRegistryFH::findEdge(const std::string& owner,
                                               const std::string& peer,
                                               PlatformKindFH platform) const {
    for (const FriendShipFH& e : edges_)
        if (e.platform == platform && e.ownerId == owner && e.peerId == peer)
            return &e;
    return nullptr;
}

FriendShipFH* FriendRegistryFH::findMutableEdge(const std::string& owner,
                                                const std::string& peer,
                                                PlatformKindFH platform) {
    for (FriendShipFH& e : edges_)
        if (e.platform == platform && e.ownerId == owner && e.peerId == peer)
            return &e;
    return nullptr;
}

void FriendRegistryFH::eraseEdge(const std::string& owner,
                                 const std::string& peer,
                                 PlatformKindFH platform) {
    edges_.erase(
        std::remove_if(edges_.begin(), edges_.end(),
                       [&](const FriendShipFH& e) {
                           return e.platform == platform &&
                                  e.ownerId == owner && e.peerId == peer;
                       }),
        edges_.end());
}
