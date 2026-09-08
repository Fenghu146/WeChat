#pragma once
// ============================================================
// GroupRegistryFH —— 微X 群注册表（作者代号：FH）
// ------------------------------------------------------------
// 职责：维护全部微X 群的群目录（群号/群名/平台/群主/成员），
// 并受理入群、退群、建群与“某用户的群列表”查询。
// 群号规则（预置演示群）：
//   - QQ   预置群：1001、1002；
//   - 微信 预置群：1003、1004；
//   - 微博 预置群：1005、1006；
//   - 用户自建群从 1007 起递增分配，全库唯一。
// 成员以“该平台账号号码”入册（微信群成员=微信号），因此加入
// 微信群的自然人必须已绑定微信号，体现平台账号资格差异。
// ============================================================
#include <cstddef>
#include <string>
#include <vector>

#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"

// 群目录条目（值对象）
struct GroupInfoFH {
    PlatformKindFH platform = PlatformKindFH::QQ;
    std::string groupId;       // 群号
    std::string name;          // 群名
    std::string ownerId;       // 创建者在该平台的账号号码（预置群为空=官方群）
    std::size_t maxMembers = 50;
    std::vector<std::string> memberIds;  // 成员在该平台的账号号码
    bool predefined = false;             // 是否为系统预置群
};

class GroupRegistryFH {
public:
    // 构造时预置六个官方群（任务书口径：各微X 预置群号 1001~1006）
    GroupRegistryFH() {
        addPredefined(PlatformKindFH::QQ, "1001", "电影兴趣群");
        addPredefined(PlatformKindFH::QQ, "1002", "篮球同好群");
        addPredefined(PlatformKindFH::WeChat, "1003", "家庭群");
        addPredefined(PlatformKindFH::WeChat, "1004", "同事群");
        addPredefined(PlatformKindFH::Weibo, "1005", "旅行分享群");
        addPredefined(PlatformKindFH::Weibo, "1006", "读书打卡群");
    }

    // 加入某群：须与群平台匹配、该自然人有对应平台账号、未入群、未满员
    bool joinGroup(const UserProfileFH& user, PlatformKindFH platform,
                   const std::string& groupId) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g || g->platform != platform) return false;
        const std::string memberId = user.platformAccountId(platform);
        if (memberId.empty()) return false;  // 缺该平台账号（如微信未绑定）
        if (containsMember(*g, memberId)) return false;
        if (g->memberIds.size() >= g->maxMembers) return false;
        g->memberIds.push_back(memberId);
        return true;
    }

    // 退出某群
    bool leaveGroup(const UserProfileFH& user, const std::string& groupId) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g) return false;
        const std::string memberId =
            user.platformAccountId(g->platform);
        auto& ids = g->memberIds;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == memberId) {
                ids.erase(it);
                return true;
            }
        }
        return false;
    }

    // 用户在某平台创建新群：群号自动分配(>=1007)，创建者自动成为群主并入群
    bool createGroup(const UserProfileFH& owner, PlatformKindFH platform,
                     const std::string& name, std::size_t maxMembers = 50) {
        if (name.empty() || maxMembers == 0) return false;
        const std::string ownerId = owner.platformAccountId(platform);
        if (ownerId.empty()) return false;
        GroupInfoFH g;
        g.platform = platform;
        g.groupId = std::to_string(nextGroupNo_++);
        g.name = name;
        g.ownerId = ownerId;
        g.maxMembers = maxMembers;
        g.predefined = false;
        g.memberIds.push_back(ownerId);
        groups_.push_back(std::move(g));
        return true;
    }

    // ---------- 查询 ----------
    const GroupInfoFH* findGroup(const std::string& groupId) const {
        for (const GroupInfoFH& g : groups_)
            if (g.groupId == groupId) return &g;
        return nullptr;
    }
    // 某平台下的全部群
    std::vector<const GroupInfoFH*> groupsOfPlatform(PlatformKindFH p) const {
        std::vector<const GroupInfoFH*> result;
        for (const GroupInfoFH& g : groups_)
            if (g.platform == p) result.push_back(&g);
        return result;
    }
    // 某自然人的“群列表”（任务书：用户信息含群列表），
    // 按其各平台账号分别匹配已加入的群。
    std::vector<const GroupInfoFH*> groupsOfUser(const UserProfileFH& user) const {
        std::vector<const GroupInfoFH*> result;
        for (const GroupInfoFH& g : groups_) {
            const std::string memberId =
                user.platformAccountId(g.platform);
            if (!memberId.empty() && containsMember(g, memberId))
                result.push_back(&g);
        }
        return result;
    }
    std::size_t groupCount() const noexcept { return groups_.size(); }

private:
    void addPredefined(PlatformKindFH p, const std::string& id,
                       const std::string& name) {
        GroupInfoFH g;
        g.platform = p;
        g.groupId = id;
        g.name = name;
        g.predefined = true;
        groups_.push_back(std::move(g));
    }
    static bool containsMember(const GroupInfoFH& g,
                               const std::string& memberId) {
        for (const std::string& id : g.memberIds)
            if (id == memberId) return true;
        return false;
    }
    GroupInfoFH* findMutable(const std::string& groupId) {
        for (GroupInfoFH& g : groups_)
            if (g.groupId == groupId) return &g;
        return nullptr;
    }

    std::vector<GroupInfoFH> groups_;
    int nextGroupNo_ = 1007;  // 自建群号从 1007 起递增
};
