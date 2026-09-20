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
// 官方预置群无群主（ownerId 为空）、不可解散；其初始成员由演示环境在
// 启动加载存档后通过 ensurePredefinedMembers 注入（库层默认仍为空群）。
// ============================================================
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <ostream>
#include <set>
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

    // 退出某群。
    // 群主不能直接退群（须先转让群主或解散群），与聚合根 GroupFH 口径一致，
    // 避免群处于“无主”状态；预置群 ownerId 为空，不受此限。
    bool leaveGroup(const UserProfileFH& user, const std::string& groupId) {
        GroupInfoFH* g = findMutable(groupId);
        if (!g) return false;
        const std::string memberId =
            user.platformAccountId(g->platform);
        if (!g->ownerId.empty() && g->ownerId == memberId) return false;
        auto& ids = g->memberIds;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == memberId) {
                ids.erase(it);
                return true;
            }
        }
        return false;
    }

    // 解散自建群：仅现群主可操作，预置官方群不可解散。
    // 解散即把该群从群目录移除，此后 findGroup 返回空。
    bool disbandGroup(const UserProfileFH& owner, const std::string& groupId) {
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

    // 转让群主：仅现群主可操作，目标须已在群内；原群主降为普通成员。
    bool transferOwner(const UserProfileFH& owner, const UserProfileFH& target,
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

    // 为预置官方群注入初始成员（演示环境数据注入）：
    // 仅对尚未注入过的预置群、且当前无成员时补齐，按人数上限截断；
    // 返回是否有变更。
    // 幂等：非预置群、已注入过的群、空名单均不动作 —— 因此用户手动退群
    // （哪怕退到空群）后不会被强行加回。这里用“是否注入过”而不是“当前是否
    // 为空”判断，否则所有人退群后再次启动会把成员重新塞回去。
    bool ensurePredefinedMembers(const std::string& groupId,
                                 const std::vector<std::string>& memberIds) {
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
    // 存档中群人数上限的合法上界（拒绝负数/超界值，防止溢出或超大局）
    static constexpr std::size_t kMaxGroupMembersFH = 100000;
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
    // 采用原子写（先写 .tmp 再替换），写失败时保留原存档并返回 false。
    bool saveToFile(const std::string& path) const {
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
        return writer.commit();
    }

    // 从文件恢复群目录（文件不存在返回 false，保持现状）。
    // 恢复后自建群号从“现有最大群号+1”继续递增。
    // 容错口径：
    //   - 单行损坏只跳过该行，不影响其它记录；
    //   - 群号非法/重复、人数上限非法、消息类型越界的记录一律丢弃；
    //   - 若整份存档没有解析出任何可用群（空文件 / 被截断 / 格式不符），
    //     返回 false 并保持当前目录，绝不用空目录覆盖（否则 6 个官方
    //     预置群会被静默清空，并在析构时把空目录写回存档）。
    bool loadFromFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::vector<GroupInfoFH> loaded;
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
                    g.chat.push_back(std::move(m));
                }
            } catch (const std::exception&) {
                continue;  // 跳过损坏行：存档被截断/篡改不应导致启动崩溃
            }
        }
        if (loaded.empty()) return false;  // 无可信内容：保持现状，不覆盖
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
    // 成员/管理员号码以 ',' 连接成一个字段。号码本身可能含 ',' 或 '\'
    // （注册中心不限制号码字符集），因此写入时做最小转义（\, 与 \\），
    // 否则 "a,b" 这样的号码会在读回时被拆成两个成员。
    static std::string joinIds(const std::vector<std::string>& ids) {
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
    static void splitIds(const std::string& joined,
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
    static bool isEmptyListMark(const std::string& s) { return s == "-"; }
    // 严格十进制解析（不接受符号与空白），超过 10 亿视为非法以免溢出
    static bool parseInt10(const std::string& s, long long& out) {
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
    static bool parseCount(const std::string& s, std::size_t cap,
                           std::size_t& out) {
        long long v = 0;
        if (!parseInt10(s, v) || v < 1) return false;
        if (static_cast<unsigned long long>(v) > cap) return false;
        out = static_cast<std::size_t>(v);
        return true;
    }
    // 存档中的消息类型：必须是合法枚举值
    static bool parseKind(const std::string& s, MessageKindFH& out) {
        long long v = 0;
        if (!parseInt10(s, v)) return false;
        MessageKindFH kind = static_cast<MessageKindFH>(v);
        if (!isValidKindFH(kind)) return false;
        out = kind;
        return true;
    }
    static bool containsGroupId(const std::vector<GroupInfoFH>& list,
                                const std::string& groupId) {
        for (const GroupInfoFH& g : list)
            if (g.groupId == groupId) return true;
        return false;
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
    std::set<std::string> presetInjected_;  // 已注入过初始成员的预置群号
};
