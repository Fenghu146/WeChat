#pragma once
// ============================================================
// GroupRegistryFH —— 微X 群注册表（作者代号：FH）
// ------------------------------------------------------------
// 职责：维护全部微X 群的群目录（群号/群名/平台/群主/管理员/成员），
// 并受理申请入群、推荐入群、退群、挨踢（踢人）、任命管理员、建群
// 与“某用户的群列表”查询。
// 群号规则（预置演示群，任务书 3.(1)）：
//   - QQ   预置群：1001、1002；
//   - 微信 预置群：1003、1004；
//   - 微博 预置群：1005、1006；
//   - 用户自建群从 1007 起递增分配，全库唯一。
// 平台差异（任务书 3.(3)）：
//   - QQ 群可以【申请加入】：joinGroup 直接受理；
//   - 微信群只能【推荐加入】：joinGroup 一律拒绝，须由群内成员
//     通过 inviteIntoGroup 推荐（拉好友进群）；
//   - QQ 群有以群主为核心的管理员制度（setGroupAdmin 任命管理员，
//     挨踢权限：群主 > 管理员 > 普通成员）；
//   - 微信群仅有群主为特权账号（挨踢仅群主，无管理员制度）。
// 成员以“该平台账号号码”入册（微信群成员=微信号），因此加入
// 微信群的自然人必须已绑定微信号，体现平台账号资格差异。
// 阶段 D：每个群附带“群聊消息记录”，发送前用平台消息策略校验
// 类型/长度/引用能力，超过上限淘汰最早记录。
// 断电保存（任务书 6.(1)、优化(2)）：群成员信息可在容器配置时读
// 入（setPersistencePath）、析构时写回（saveToFile/loadFromFile）。
// 官方预置群无群主（ownerId 为空），不受个人管理（不可踢人）。
// ============================================================
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/persist_util_fh.hpp"
#include "im/social/group_chat_record_fh.hpp"
#include "im/social/group_info_fh.hpp"

class GroupRegistryFH {
public:
    // 构造时预置六个官方群（任务书口径：各微X 预置群号 1001~1006）
    GroupRegistryFH() {
        seedPredefined();
    }

    // 实例化时读入（任务书优化(2)字面口径）：预置群先在代码中固化，
    // 随即从文件加载目录（存档含预置群与自建群，加载结果即存档内容）
    explicit GroupRegistryFH(const std::string& persistPath) {
        seedPredefined();
        persistPath_ = persistPath;
        loadFromFile(persistPath);
    }

    // 断电保存：若配置了持久化文件，析构时写回（任务书优化(2)）
    ~GroupRegistryFH() {
        if (!persistPath_.empty()) saveToFile(persistPath_);
    }

    // 配置持久化文件：配置（实例化）时立即读入，此后析构自动写回
    bool setPersistencePath(const std::string& path) {
        persistPath_ = path;
        return loadFromFile(path);
    }

    // 加入某群：须与群平台匹配、该自然人有对应平台账号、未入群、
    // 未满员。平台差异（任务书 3.(3)）：QQ 群可申请加入；微信群
    // 只能推荐加入 —— 直接申请一律拒绝，走 inviteIntoGroup。
    bool joinGroup(const UserProfileFH& user, PlatformKindFH platform,
                   const std::string& groupId) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g || g->platform != platform) return false;
        if (platform == PlatformKindFH::WeChat) return false;
        const std::string memberId = user.platformAccountId(platform);
        if (memberId.empty()) return false;  // 缺该平台账号（如微信未绑定）
        if (containsMember(*g, memberId)) return false;
        if (g->memberIds.size() >= g->maxMembers) return false;
        g->memberIds.push_back(memberId);
        return true;
    }

    // 微信群推荐加入：由群内成员推荐（拉）目标进入微信群。
    // 仅适用于微信群（QQ 群走申请加入）；操作者须已在群内，目标须
    // 有微信号、不在群内且群未满员。
    bool inviteIntoGroup(const UserProfileFH& oper, const UserProfileFH& target,
                         const std::string& groupId) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g || g->platform != PlatformKindFH::WeChat) return false;
        const std::string opId = oper.platformAccountId(PlatformKindFH::WeChat);
        const std::string tgtId = target.platformAccountId(PlatformKindFH::WeChat);
        if (opId.empty() || tgtId.empty() || opId == tgtId) return false;
        if (!containsMember(*g, opId)) return false;   // 操作者须为群成员
        if (containsMember(*g, tgtId)) return false;
        if (g->memberIds.size() >= g->maxMembers) return false;
        g->memberIds.push_back(tgtId);
        return true;
    }

    // 挨踢（任务书 3.(2)）：把目标成员移出群。
    // QQ 群：群主可踢除自己外任何人；管理员仅可踢普通成员；
    // 微信群：仅有群主为特权账号 —— 仅群主可踢。
    bool kickMember(const UserProfileFH& oper, const UserProfileFH& target,
                    const std::string& groupId) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g) return false;
        const std::string opId = oper.platformAccountId(g->platform);
        const std::string tgtId = target.platformAccountId(g->platform);
        if (opId.empty() || tgtId.empty() || opId == tgtId) return false;
        if (!containsMember(*g, opId) || !containsMember(*g, tgtId))
            return false;
        const bool opOwner = !g->ownerId.empty() && g->ownerId == opId;
        const bool opAdmin = containsId(g->adminIds, opId);
        bool allowed = false;
        if (g->platform == PlatformKindFH::WeChat) {
            allowed = opOwner;  // 微信群仅群主
        } else if (opOwner) {
            allowed = true;     // QQ 群主
        } else if (opAdmin) {
            // QQ 管理员：不能踢群主或其他管理员
            allowed = g->ownerId != tgtId && !containsId(g->adminIds, tgtId);
        }
        if (!allowed) return false;
        removeId(g->adminIds, tgtId);      // 被踢者同时摘除管理员身份
        eraseMember(g->memberIds, tgtId);
        return true;
    }

    // QQ 群管理员制度：仅群主任命/撤销管理员（微信群无管理员制度）
    bool setGroupAdmin(const UserProfileFH& oper, const UserProfileFH& target,
                       const std::string& groupId, bool promote) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g || g->platform != PlatformKindFH::QQ) return false;
        const std::string opId = oper.platformAccountId(PlatformKindFH::QQ);
        const std::string tgtId = target.platformAccountId(PlatformKindFH::QQ);
        if (opId.empty() || tgtId.empty() || opId == tgtId) return false;
        if (g->ownerId.empty() || g->ownerId != opId) return false;
        if (!containsMember(*g, tgtId)) return false;
        if (promote) {
            if (!containsId(g->adminIds, tgtId)) g->adminIds.push_back(tgtId);
        } else {
            removeId(g->adminIds, tgtId);
        }
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

    // 群消息扩展（阶段 D）：向群内发一条消息。
    // 校验链：群存在且平台匹配 → 发送者是群成员 → 平台允许该消息类型
    // → 内容长度不超过平台文本上限 → （引用回复时）平台支持引用。
    // 通过后追加消息记录，超过记录上限时淘汰最早的记录。
    bool sendGroupMessage(const UserProfileFH& user, PlatformKindFH platform,
                          const std::string& groupId, MessageKindFH kind,
                          const std::string& content, bool asReply = false) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g || g->platform != platform) return false;
        const std::string senderId = user.platformAccountId(platform);
        if (senderId.empty() || !containsMember(*g, senderId)) return false;
        if (!PlatformMessagePolicyFH::supportsKind(platform, kind)) return false;
        if (content.size() > PlatformMessagePolicyFH::maxTextLength(platform))
            return false;
        if (asReply && !PlatformMessagePolicyFH::supportsReply(platform))
            return false;
        if (g->chat.size() >= kMaxChatRecordsFH) g->chat.erase(g->chat.begin());
        g->chat.push_back(GroupChatRecordFH{kind, senderId, user.getNickname(),
                                            content, asReply,
                                            std::chrono::system_clock::now()});
        return true;
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
    // 查询群成员（任务书 3.(2)）：返回该群成员账号号码列表
    const std::vector<std::string>* memberIdsOf(const std::string& groupId) const {
        const GroupInfoFH* g = findGroup(groupId);
        return g ? &g->memberIds : nullptr;
    }
    // 某自然人是否为某群群主
    bool isOwnerOf(const UserProfileFH& user, const std::string& groupId) const {
        const GroupInfoFH* g = findGroup(groupId);
        if (!g || g->ownerId.empty()) return false;
        return g->ownerId == user.platformAccountId(g->platform);
    }
    // 某自然人是否为某群管理员（QQ 群管理员制度）
    bool isAdminOf(const UserProfileFH& user, const std::string& groupId) const {
        const GroupInfoFH* g = findGroup(groupId);
        if (!g) return false;
        return containsId(g->adminIds, user.platformAccountId(g->platform));
    }
    // 每个群的聊天记录条数上限（阶段 D 群消息扩展）
    static constexpr std::size_t kMaxChatRecordsFH = 50;
    // 某群的聊天记录（只读；群不存在返回空）
    const std::vector<GroupChatRecordFH>& chatOf(
        const std::string& groupId) const {
        for (const GroupInfoFH& g : groups_)
            if (g.groupId == groupId) return g.chat;
        return emptyChat();
    }
    std::size_t groupCount() const noexcept { return groups_.size(); }

    // ---------- 断电保存（群成员信息） ----------
    // 行格式：G 行=群目录；M 行=群聊消息记录。字段以 0x1F 分隔。
    bool saveToFile(const std::string& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        using persist_util_fh::kFieldSepFH;
        using persist_util_fh::escapeTextFH;
        for (const GroupInfoFH& g : groups_) {
            out << 'G' << kFieldSepFH
                << persist_util_fh::platformName(g.platform) << kFieldSepFH
                << g.groupId << kFieldSepFH
                << escapeTextFH(g.name) << kFieldSepFH
                << g.ownerId << kFieldSepFH
                << g.maxMembers << kFieldSepFH
                << (g.predefined ? 1 : 0) << kFieldSepFH
                << joinIds(g.adminIds) << kFieldSepFH
                << joinIds(g.memberIds) << '\n';
            for (const GroupChatRecordFH& m : g.chat) {
                out << 'M' << kFieldSepFH
                    << g.groupId << kFieldSepFH
                    << static_cast<int>(m.kind) << kFieldSepFH
                    << m.senderId << kFieldSepFH
                    << escapeTextFH(m.senderNick) << kFieldSepFH
                    << escapeTextFH(m.content) << kFieldSepFH
                    << (m.isReply ? 1 : 0) << kFieldSepFH
                    << m.sentAt.time_since_epoch().count() << '\n';
            }
        }
        return true;
    }

    // 从文件恢复群目录（文件不存在返回 false，保持现状）。
    // 恢复后自建群号从“现有最大群号+1”继续递增。
    bool loadFromFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::vector<GroupInfoFH> loaded;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::vector<std::string> f = persist_util_fh::splitFieldsFH(line);
            if (f[0] == "G" && f.size() >= 9) {
                GroupInfoFH g;
                if (!persist_util_fh::platformFromName(f[1], g.platform))
                    continue;
                g.groupId = f[2];
                g.name = persist_util_fh::unescapeTextFH(f[3]);
                g.ownerId = f[4];
                g.maxMembers = static_cast<std::size_t>(std::stoul(f[5]));
                g.predefined = f[6] == "1";
                if (f[7].size() == 1 && f[7][0] == '-') {  // 空列表占位
                } else if (!f[7].empty()) {
                    splitIds(f[7], g.adminIds);
                }
                if (f[8].size() == 1 && f[8][0] == '-') {
                } else if (!f[8].empty()) {
                    splitIds(f[8], g.memberIds);
                }
                loaded.push_back(std::move(g));
            } else if (f[0] == "M" && f.size() >= 8 && !loaded.empty()) {
                // 消息记录挂到最近一次出现的群（文件按群序写出）
                GroupInfoFH& g = loaded.back();
                if (g.groupId != f[1]) continue;
                GroupChatRecordFH m;
                m.kind = static_cast<MessageKindFH>(std::stoi(f[2]));
                m.senderId = f[3];
                m.senderNick = persist_util_fh::unescapeTextFH(f[4]);
                m.content = persist_util_fh::unescapeTextFH(f[5]);
                m.isReply = f[6] == "1";
                m.sentAt = std::chrono::system_clock::time_point(
                    std::chrono::system_clock::duration(
                        std::chrono::system_clock::duration::rep(
                            std::stoll(f[7]))));
                g.chat.push_back(std::move(m));
            }
        }
        groups_ = std::move(loaded);
        rebuildNextGroupNo();
        return true;
    }

private:
    void seedPredefined() {
        addPredefined(PlatformKindFH::QQ, "1001", "电影兴趣群");
        addPredefined(PlatformKindFH::QQ, "1002", "篮球同好群");
        addPredefined(PlatformKindFH::WeChat, "1003", "家庭群");
        addPredefined(PlatformKindFH::WeChat, "1004", "同事群");
        addPredefined(PlatformKindFH::Weibo, "1005", "旅行分享群");
        addPredefined(PlatformKindFH::Weibo, "1006", "读书打卡群");
    }
    static const std::vector<GroupChatRecordFH>& emptyChat() {
        static const std::vector<GroupChatRecordFH> empty;
        return empty;
    }
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
    static bool containsId(const std::vector<std::string>& ids,
                           const std::string& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    static void removeId(std::vector<std::string>& ids, const std::string& id) {
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    }
    static void eraseMember(std::vector<std::string>& ids,
                            const std::string& id) {
        removeId(ids, id);
    }
    static std::string joinIds(const std::vector<std::string>& ids) {
        if (ids.empty()) return "-";
        std::string out;
        for (const std::string& id : ids) {
            if (!out.empty()) out.push_back(',');
            out += id;
        }
        return out;
    }
    static void splitIds(const std::string& joined,
                         std::vector<std::string>& ids) {
        std::string cur;
        for (char ch : joined) {
            if (ch == ',') {
                if (!cur.empty()) ids.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) ids.push_back(cur);
    }
    void rebuildNextGroupNo() {
        int maxNo = 1006;  // 预置群号上界
        for (const GroupInfoFH& g : groups_) {
            try {
                int no = std::stoi(g.groupId);
                if (no > maxNo) maxNo = no;
            } catch (...) {
                // 非数字群号忽略（不影响自建群号分配）
            }
        }
        nextGroupNo_ = maxNo + 1;
    }
    GroupInfoFH* findMutable(const std::string& groupId) {
        for (GroupInfoFH& g : groups_)
            if (g.groupId == groupId) return &g;
        return nullptr;
    }

    std::vector<GroupInfoFH> groups_;
    int nextGroupNo_ = 1007;  // 自建群号从 1007 起递增
    std::string persistPath_; // 断电保存文件（空=未启用）
};
