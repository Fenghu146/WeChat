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
//
// 声明与实现分离：方法实现位于 src/social/group_registry_fh.cpp。
// ============================================================
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "im/message/message_kind_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/social/group_chat_record_fh.hpp"
#include "im/social/group_info_fh.hpp"

class GroupRegistryFH {
public:
    // 构造时预置六个官方群（任务书口径：各微X 预置群号 1001~1006）
    GroupRegistryFH();

    // 实例化时读入（任务书优化(2)字面口径）：预置群先在代码中固化，
    // 随即从文件加载目录（存档含预置群与自建群，加载结果即存档内容）
    explicit GroupRegistryFH(const std::string& persistPath);

    // 断电保存：若配置了持久化文件，析构时写回（任务书优化(2)）
    ~GroupRegistryFH();

    // 配置持久化文件：配置（实例化）时立即读入，此后析构自动写回
    bool setPersistencePath(const std::string& path);

    // 加入某群：须与群平台匹配、该自然人有对应平台账号、未入群、
    // 未满员。平台差异（任务书 3.(3)）：QQ 群可申请加入；微信群
    // 只能推荐加入 —— 直接申请一律拒绝，走 inviteIntoGroup。
    bool joinGroup(const UserProfileFH& user, PlatformKindFH platform,
                   const std::string& groupId);

    // 微信群推荐加入：由群内成员推荐（拉）目标进入微信群。
    // 仅适用于微信群（QQ 群走申请加入）；操作者须已在群内，目标须
    // 有微信号、不在群内且群未满员。
    bool inviteIntoGroup(const UserProfileFH& oper, const UserProfileFH& target,
                         const std::string& groupId);

    // 挨踢（任务书 3.(2)）：把目标成员移出群。
    // QQ 群：群主可踢除自己外任何人；管理员仅可踢普通成员；
    // 微信群：仅有群主为特权账号 —— 仅群主可踢。
    bool kickMember(const UserProfileFH& oper, const UserProfileFH& target,
                    const std::string& groupId);

    // QQ 群管理员制度：仅群主任命/撤销管理员（微信群无管理员制度）
    bool setGroupAdmin(const UserProfileFH& oper, const UserProfileFH& target,
                       const std::string& groupId, bool promote);

    // 退出某群。
    // 群主不能直接退群（须先转让群主或解散群），与聚合根 GroupFH 口径一致，
    // 避免群处于“无主”状态；预置群 ownerId 为空，不受此限。
    bool leaveGroup(const UserProfileFH& user, const std::string& groupId);

    // 解散自建群：仅现群主可操作，预置官方群不可解散。
    // 解散即把该群从群目录移除，此后 findGroup 返回空。
    bool disbandGroup(const UserProfileFH& owner, const std::string& groupId);

    // 转让群主：仅现群主可操作，目标须已在群内；原群主降为普通成员。
    bool transferOwner(const UserProfileFH& owner, const UserProfileFH& target,
                       const std::string& groupId);

    // 群消息扩展（阶段 D）：向群内发一条消息。
    // 校验链：群存在且平台匹配 → 发送者是群成员 → 平台允许该消息类型
    // → 内容长度不超过平台文本上限 → （引用回复时）平台支持引用。
    // 通过后追加消息记录，超过记录上限时淘汰最早的记录。
    bool sendGroupMessage(const UserProfileFH& user, PlatformKindFH platform,
                          const std::string& groupId, MessageKindFH kind,
                          const std::string& content, bool asReply = false);

    // 用户在某平台创建新群：群号自动分配(>=1007)，创建者自动成为群主并入群
    bool createGroup(const UserProfileFH& owner, PlatformKindFH platform,
                     const std::string& name, std::size_t maxMembers = 50);

    // 为预置官方群注入初始成员（演示环境数据注入）：
    // 仅对尚未注入过的预置群、且当前无成员时补齐，按人数上限截断；
    // 返回是否有变更。
    // 幂等：非预置群、已注入过的群、空名单均不动作 —— 因此用户手动退群
    // （哪怕退到空群）后不会被强行加回。这里用“是否注入过”而不是“当前是否
    // 为空”判断，否则所有人退群后再次启动会把成员重新塞回去。
    bool ensurePredefinedMembers(const std::string& groupId,
                                 const std::vector<std::string>& memberIds);

    // ---------- 查询 ----------
    // 设计约定：查询接口一律【按值返回】，返回值不引用内部容器。
    // 原因：群目录是 std::vector<GroupInfoFH>，createGroup 会 push_back、
    // disbandGroup / loadFromFile 会 erase/整体替换 —— 若返回指针或引用，
    // 调用方只要把结果多留一会儿就会悬空（此前 app/ui/client_ui_official_chat.cpp 的解散
    // 提示就踩过）。按值返回让“取出来的东西一直有效”，代价是拷贝若干小对象。
    std::optional<GroupInfoFH> findGroup(const std::string& groupId) const;
    // 某平台下的全部群
    std::vector<GroupInfoFH> groupsOfPlatform(PlatformKindFH p) const;
    // 某自然人的“群列表”（任务书：用户信息含群列表），
    // 按其各平台账号分别匹配已加入的群。
    std::vector<GroupInfoFH> groupsOfUser(const UserProfileFH& user) const;
    // 查询群成员（任务书 3.(2)）：返回该群成员账号号码列表
    std::vector<std::string> memberIdsOf(const std::string& groupId) const;
    // 某自然人是否为某群成员 / 某群成员数（避免为了判断而整表拷贝）
    bool isMember(const std::string& groupId, const std::string& memberId) const;
    std::size_t memberCount(const std::string& groupId) const;
    // 某自然人是否为某群群主
    bool isOwnerOf(const UserProfileFH& user, const std::string& groupId) const;
    // 某自然人是否为某群管理员（QQ 群管理员制度）
    bool isAdminOf(const UserProfileFH& user, const std::string& groupId) const;
    // 每个群的聊天记录条数上限（阶段 D 群消息扩展）
    static constexpr std::size_t kMaxChatRecordsFH = 50;
    // 存档中群人数上限的合法上界（拒绝负数/超界值，防止溢出或超大局）
    static constexpr std::size_t kMaxGroupMembersFH = 100000;
    // 某群的聊天记录（只读快照；群不存在返回空）
    std::vector<GroupChatRecordFH> chatOf(const std::string& groupId) const;
    std::size_t chatCount(const std::string& groupId) const;
    std::size_t groupCount() const noexcept;

    // ---------- 断电保存（群成员信息） ----------
    // 行格式：G 行=群目录；M 行=群聊消息记录；I 行=已注入过初始成员的
    // 预置群号（幂等标记，防止重启后把退群成员重新塞回）。字段以 0x1F 分隔。
    // 采用原子写（先写 .tmp 再替换），写失败时保留原存档并返回 false。
    bool saveToFile(const std::string& path) const;

    // 从文件恢复群目录（文件不存在返回 false，保持现状）。
    // 恢复后自建群号从“现有最大群号+1”继续递增。
    // 容错口径：
    //   - 单行损坏只跳过该行，不影响其它记录；
    //   - 群号非法/重复、人数上限非法、消息类型越界的记录一律丢弃；
    //   - 若整份存档没有解析出任何可用群（空文件 / 被截断 / 格式不符），
    //     返回 false 并保持当前目录，绝不用空目录覆盖（否则 6 个官方
    //     预置群会被静默清空，并在析构时把空目录写回存档）。
    bool loadFromFile(const std::string& path);

private:
    void seedPredefined();
    void addPredefined(PlatformKindFH p, const std::string& id,
                       const std::string& name);
    static bool containsMember(const GroupInfoFH& g,
                               const std::string& memberId);
    static bool containsId(const std::vector<std::string>& ids,
                           const std::string& id);
    static void removeId(std::vector<std::string>& ids, const std::string& id);
    static void eraseMember(std::vector<std::string>& ids,
                            const std::string& id);
    // 成员/管理员号码以 ',' 连接成一个字段。号码本身可能含 ',' 或 '\'
    // （注册中心不限制号码字符集），因此写入时做最小转义（\, 与 \\），
    // 否则 "a,b" 这样的号码会在读回时被拆成两个成员。
    static std::string joinIds(const std::vector<std::string>& ids);
    static void splitIds(const std::string& joined,
                         std::vector<std::string>& ids);
    // 空列表在存档中的占位符
    static bool isEmptyListMark(const std::string& s);
    // 严格十进制解析（不接受符号与空白），超过 10 亿视为非法以免溢出
    static bool parseInt10(const std::string& s, long long& out);
    // 群人数上限：必须是 1 ~ cap 的整数
    static bool parseCount(const std::string& s, std::size_t cap,
                           std::size_t& out);
    // 存档中的消息类型：必须是合法枚举值
    static bool parseKind(const std::string& s, MessageKindFH& out);
    static bool containsGroupId(const std::vector<GroupInfoFH>& list,
                                const std::string& groupId);
    void rebuildNextGroupNo();
    GroupInfoFH* findMutable(const std::string& groupId);

    std::vector<GroupInfoFH> groups_;
    int nextGroupNo_ = 1007;  // 自建群号从 1007 起递增
    std::string persistPath_; // 断电保存文件（空=未启用）
    std::set<std::string> presetInjected_;  // 已注入过初始成员的预置群号
};
