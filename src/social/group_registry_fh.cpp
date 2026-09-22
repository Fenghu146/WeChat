// ============================================================
// group_registry_fh.cpp —— 群注册表 GroupRegistryFH 的方法实现（作者代号：FH）
// 声明见 include/im/social/group_registry_fh.hpp。
// ============================================================
#include "im/social/group_registry_fh.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <ostream>
#include <utility>

#include "im/message/platform_message_policy_fh.hpp"
#include "im/util/persist_util_fh.hpp"

namespace {
// 是否为空串或仅由空白字符组成（与 GroupFH::isBlankText 同口径）
bool isBlankText(const std::string& s) {
    for (char ch : s)
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') return false;
    return true;
}
}  // namespace

GroupRegistryFH::GroupRegistryFH() {
    seedPredefined();
}

GroupRegistryFH::GroupRegistryFH(const std::string& persistPath) {
    seedPredefined();
    persistPath_ = persistPath;
    loadFromFile(persistPath);
}

GroupRegistryFH::~GroupRegistryFH() {
    if (!persistPath_.empty()) saveToFile(persistPath_);
}

// 配置持久化文件：配置（实例化）时立即读入，此后析构自动写回
bool GroupRegistryFH::setPersistencePath(const std::string& path) {
    persistPath_ = path;
    return loadFromFile(path);
}

bool GroupRegistryFH::joinGroup(const UserProfileFH& user,
                                PlatformKindFH platform,
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

bool GroupRegistryFH::inviteIntoGroup(const UserProfileFH& oper,
                                      const UserProfileFH& target,
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

bool GroupRegistryFH::kickMember(const UserProfileFH& oper,
                                 const UserProfileFH& target,
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

bool GroupRegistryFH::setGroupAdmin(const UserProfileFH& oper,
                                    const UserProfileFH& target,
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

bool GroupRegistryFH::leaveGroup(const UserProfileFH& user,
                                 const std::string& groupId) {
    GroupInfoFH* g = findMutable(groupId);
    if (!g) return false;
    const std::string memberId = user.platformAccountId(g->platform);
    if (!g->ownerId.empty() && g->ownerId == memberId) return false;
    auto& ids = g->memberIds;
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (*it == memberId) {
            ids.erase(it);
            removeId(g->adminIds, memberId);  // 退群同时摘除管理员身份（与被踢一致），
            return true;                      // 否则重新入群会“自动官复原职”
        }
    }
    return false;
}

bool GroupRegistryFH::disbandGroup(const UserProfileFH& owner,
                                   const std::string& groupId) {
    GroupInfoFH* g = findMutable(groupId);
    if (!g || g->predefined) return false;
    const std::string opId = owner.platformAccountId(g->platform);
    if (opId.empty() || g->ownerId.empty() || g->ownerId != opId)
        return false;
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(),
                                 [&](const GroupInfoFH& x) {
                                     return x.groupId == groupId;
                                 }),
                  groups_.end());
    return true;
}

bool GroupRegistryFH::transferOwner(const UserProfileFH& owner,
                                    const UserProfileFH& target,
                                    const std::string& groupId) {
    GroupInfoFH* g = findMutable(groupId);
    if (!g || g->predefined) return false;
    const std::string opId = owner.platformAccountId(g->platform);
    const std::string tgtId = target.platformAccountId(g->platform);
    if (opId.empty() || tgtId.empty() || opId == tgtId) return false;
    if (g->ownerId.empty() || g->ownerId != opId) return false;
    if (!containsMember(*g, tgtId)) return false;
    g->ownerId = tgtId;
    removeId(g->adminIds, tgtId);  // 新任群主不再保留管理员身份
    return true;
}

bool GroupRegistryFH::sendGroupMessage(const UserProfileFH& user,
                                       PlatformKindFH platform,
                                       const std::string& groupId,
                                       MessageKindFH kind,
                                       const std::string& content,
                                       bool asReply) {
    GroupInfoFH* g = findMutable(groupId);
    if (!g || g->platform != platform) return false;
    const std::string senderId = user.platformAccountId(platform);
    if (senderId.empty() || !containsMember(*g, senderId)) return false;
    if (isBlankText(content)) return false;  // 空内容/全空白无业务含义
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

bool GroupRegistryFH::createGroup(const UserProfileFH& owner,
                                  PlatformKindFH platform,
                                  const std::string& name,
                                  std::size_t maxMembers) {
    // 与 loadFromFile 的 parseCount 上界对称：超上限的群建得成功、存档也
    // 写得进去，但重启时整群（含成员与聊天记录）会被静默丢弃 —— 必须在
    // 入口就拒绝。群名同 editGroup 口径拒绝全空白。
    if (isBlankText(name) || maxMembers == 0 ||
        maxMembers > kMaxGroupMembersFH) return false;
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

bool GroupRegistryFH::ensurePredefinedMembers(
    const std::string& groupId, const std::vector<std::string>& memberIds) {
    GroupInfoFH* g = findMutable(groupId);
    if (!g || !g->predefined) return false;
    if (presetInjected_.count(groupId) > 0) return false;
    if (!g->memberIds.empty() || memberIds.empty()) return false;
    bool changed = false;
    for (const std::string& id : memberIds) {
        if (id.empty() || containsMember(*g, id)) continue;
        if (g->memberIds.size() >= g->maxMembers) break;
        g->memberIds.push_back(id);
        changed = true;
    }
    presetInjected_.insert(groupId);
    return changed;
}

// ---------- 查询 ----------
std::optional<GroupInfoFH> GroupRegistryFH::findGroup(
    const std::string& groupId) const {
    for (const GroupInfoFH& g : groups_)
        if (g.groupId == groupId) return g;
    return std::nullopt;
}

std::vector<GroupInfoFH> GroupRegistryFH::groupsOfPlatform(
    PlatformKindFH p) const {
    std::vector<GroupInfoFH> result;
    for (const GroupInfoFH& g : groups_)
        if (g.platform == p) result.push_back(g);
    return result;
}

std::vector<GroupInfoFH> GroupRegistryFH::groupsOfUser(
    const UserProfileFH& user) const {
    std::vector<GroupInfoFH> result;
    for (const GroupInfoFH& g : groups_) {
        const std::string memberId = user.platformAccountId(g.platform);
        if (!memberId.empty() && containsMember(g, memberId))
            result.push_back(g);
    }
    return result;
}

std::vector<std::string> GroupRegistryFH::memberIdsOf(
    const std::string& groupId) const {
    const auto g = findGroup(groupId);
    return g ? g->memberIds : std::vector<std::string>{};
}

bool GroupRegistryFH::isMember(const std::string& groupId,
                               const std::string& memberId) const {
    if (memberId.empty()) return false;
    const auto g = findGroup(groupId);
    return g && containsMember(*g, memberId);
}

std::size_t GroupRegistryFH::memberCount(const std::string& groupId) const {
    const auto g = findGroup(groupId);
    return g ? g->memberIds.size() : 0;
}

bool GroupRegistryFH::isOwnerOf(const UserProfileFH& user,
                                const std::string& groupId) const {
    const auto g = findGroup(groupId);
    if (!g || g->ownerId.empty()) return false;
    return g->ownerId == user.platformAccountId(g->platform);
}

bool GroupRegistryFH::isAdminOf(const UserProfileFH& user,
                                const std::string& groupId) const {
    const auto g = findGroup(groupId);
    if (!g) return false;
    return containsId(g->adminIds, user.platformAccountId(g->platform));
}

std::vector<GroupChatRecordFH> GroupRegistryFH::chatOf(
    const std::string& groupId) const {
    const auto g = findGroup(groupId);
    return g ? g->chat : std::vector<GroupChatRecordFH>{};
}

std::size_t GroupRegistryFH::chatCount(const std::string& groupId) const {
    const auto g = findGroup(groupId);
    return g ? g->chat.size() : 0;
}

std::size_t GroupRegistryFH::groupCount() const noexcept {
    return groups_.size();
}

// ---------- 断电保存（群成员信息） ----------
bool GroupRegistryFH::saveToFile(const std::string& path) const {
    persist_util_fh::AtomicWriterFH writer(path);
    if (!writer.ok()) return false;
    std::ostream& out = writer.stream();
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
    // I 行：已注入过初始成员的预置群号 —— 幂等标记随存档持久化，
    // 否则“注入 → 全员退群 → 重启”会把成员重新塞回，违背幂等承诺
    for (const std::string& gid : presetInjected_)
        out << 'I' << kFieldSepFH << gid << '\n';
    return writer.commit();
}

bool GroupRegistryFH::loadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<GroupInfoFH> loaded;
    std::set<std::string> injected;  // 存档中的预置群注入标记（I 行）
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            std::vector<std::string> f = persist_util_fh::splitFieldsFH(line);
            if (f[0] == "G" && f.size() >= 9) {
                std::size_t maxMembers = 0;
                long long predefined = 0;
                if (f[2].empty() || containsGroupId(loaded, f[2])) continue;
                if (!parseCount(f[5], kMaxGroupMembersFH, maxMembers)) continue;
                if (!parseInt10(f[6], predefined)) continue;
                GroupInfoFH g;
                if (!persist_util_fh::platformFromName(f[1], g.platform))
                    continue;
                g.groupId = f[2];
                g.name = persist_util_fh::unescapeTextFH(f[3]);
                g.ownerId = f[4];
                g.maxMembers = maxMembers;
                g.predefined = (predefined != 0);
                if (!isEmptyListMark(f[7])) splitIds(f[7], g.adminIds);
                if (!isEmptyListMark(f[8])) splitIds(f[8], g.memberIds);
                loaded.push_back(std::move(g));
            } else if (f[0] == "M" && f.size() >= 8 && !loaded.empty()) {
                // 消息记录挂到最近一次出现的群（文件按群序写出）
                GroupInfoFH& g = loaded.back();
                if (g.groupId != f[1]) continue;
                MessageKindFH kind = MessageKindFH::TEXT;
                if (!parseKind(f[2], kind)) continue;  // 越界类型直接丢弃
                GroupChatRecordFH m;
                m.kind = kind;
                m.senderId = f[3];
                m.senderNick = persist_util_fh::unescapeTextFH(f[4]);
                m.content = persist_util_fh::unescapeTextFH(f[5]);
                m.isReply = f[6] == "1";
                m.sentAt = std::chrono::system_clock::time_point(
                    std::chrono::system_clock::duration(
                        std::chrono::system_clock::duration::rep(
                            std::stoll(f[7]))));
                // 存档被手工编辑/部分损坏时可能带超上限记录：载入即裁剪，
                // 与 sendGroupMessage 的淘汰口径一致（保最新、丢最早）
                if (g.chat.size() >= kMaxChatRecordsFH) g.chat.erase(g.chat.begin());
                g.chat.push_back(std::move(m));
            } else if (f[0] == "I" && f.size() >= 2 && !f[1].empty()) {
                // I 行：已注入过初始成员的预置群号（幂等标记）
                injected.insert(f[1]);
            }
        } catch (const std::exception&) {
            continue;  // 跳过损坏行：存档被截断/篡改不应导致启动崩溃
        }
    }
    if (loaded.empty()) return false;  // 无可信内容：保持现状，不覆盖
    groups_ = std::move(loaded);
    presetInjected_ = std::move(injected);  // 注入标记随存档恢复
    rebuildNextGroupNo();
    return true;
}

// ---------- 私有实现 ----------
void GroupRegistryFH::seedPredefined() {
    addPredefined(PlatformKindFH::QQ, "1001", "电影兴趣群");
    addPredefined(PlatformKindFH::QQ, "1002", "篮球同好群");
    addPredefined(PlatformKindFH::WeChat, "1003", "家庭群");
    addPredefined(PlatformKindFH::WeChat, "1004", "同事群");
    addPredefined(PlatformKindFH::Weibo, "1005", "旅行分享群");
    addPredefined(PlatformKindFH::Weibo, "1006", "读书打卡群");
}

void GroupRegistryFH::addPredefined(PlatformKindFH p, const std::string& id,
                                    const std::string& name) {
    GroupInfoFH g;
    g.platform = p;
    g.groupId = id;
    g.name = name;
    g.predefined = true;
    groups_.push_back(std::move(g));
}

bool GroupRegistryFH::containsMember(const GroupInfoFH& g,
                                     const std::string& memberId) {
    for (const std::string& id : g.memberIds)
        if (id == memberId) return true;
    return false;
}

bool GroupRegistryFH::containsId(const std::vector<std::string>& ids,
                                 const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void GroupRegistryFH::removeId(std::vector<std::string>& ids,
                               const std::string& id) {
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
}

void GroupRegistryFH::eraseMember(std::vector<std::string>& ids,
                                  const std::string& id) {
    removeId(ids, id);
}

// 成员/管理员号码以 ',' 连接成一个字段。号码本身可能含 ',' 或 '\'
// （注册中心不限制号码字符集），因此写入时做最小转义（\, 与 \\），
// 否则 "a,b" 这样的号码会在读回时被拆成两个成员。
std::string GroupRegistryFH::joinIds(const std::vector<std::string>& ids) {
    if (ids.empty()) return "-";
    std::string out;
    for (const std::string& id : ids) {
        if (!out.empty()) out.push_back(',');
        for (char ch : id) {
            if (ch == '\\' || ch == ',') out.push_back('\\');
            out.push_back(ch);
        }
    }
    return out;
}

void GroupRegistryFH::splitIds(const std::string& joined,
                               std::vector<std::string>& ids) {
    std::string cur;
    bool escaped = false;
    for (char ch : joined) {
        if (escaped) {
            cur.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == ',') {
            if (!cur.empty()) ids.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (escaped) cur.push_back('\\');  // 行尾孤立反斜杠
    if (!cur.empty()) ids.push_back(cur);
}

// 空列表在存档中的占位符
bool GroupRegistryFH::isEmptyListMark(const std::string& s) { return s == "-"; }

// 严格十进制解析（不接受符号与空白），超过 10 亿视为非法以免溢出
bool GroupRegistryFH::parseInt10(const std::string& s, long long& out) {
    if (s.empty()) return false;
    long long v = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
        if (v > 1000000000LL) return false;
    }
    out = v;
    return true;
}

// 群人数上限：必须是 1 ~ cap 的整数
bool GroupRegistryFH::parseCount(const std::string& s, std::size_t cap,
                                 std::size_t& out) {
    long long v = 0;
    if (!parseInt10(s, v) || v < 1) return false;
    if (static_cast<unsigned long long>(v) > cap) return false;
    out = static_cast<std::size_t>(v);
    return true;
}

// 存档中的消息类型：必须是合法枚举值
bool GroupRegistryFH::parseKind(const std::string& s, MessageKindFH& out) {
    long long v = 0;
    if (!parseInt10(s, v)) return false;
    MessageKindFH kind = static_cast<MessageKindFH>(v);
    if (!isValidKindFH(kind)) return false;
    out = kind;
    return true;
}

bool GroupRegistryFH::containsGroupId(const std::vector<GroupInfoFH>& list,
                                      const std::string& groupId) {
    for (const GroupInfoFH& g : list)
        if (g.groupId == groupId) return true;
    return false;
}

void GroupRegistryFH::rebuildNextGroupNo() {
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

GroupInfoFH* GroupRegistryFH::findMutable(const std::string& groupId) {
    for (GroupInfoFH& g : groups_)
        if (g.groupId == groupId) return &g;
    return nullptr;
}
