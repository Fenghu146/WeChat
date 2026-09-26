#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 阶段 C/D：官方群 / 自建群（群注册表）会话窗口
// ============================================================

// 注意：群注册表的查询接口已改为【按值返回】，这里拿到的是独立快照，可以放心
// 在会话里持有；群目录的结构性变更（建群 / 解散）不会让它失效。
std::optional<GroupInfoFH> findOfficial(const std::string& groupId) {
    return g.official.findGroup(groupId);
}

// 官方群发送失败原因：与 PlatformMessagePolicyFH 规则一致
std::string officialSendReason(const GroupInfoFH& infoRef,
                               const ProfilePtr& op, MessageKindFH kind,
                               const std::string& content, bool asReply) {
    const auto pl = infoRef.platform;
    const auto senderId = op->platformAccountId(pl);
    if (senderId.empty())
        return "你没有该平台的账号，无法在" + platCn(pl) + "群发言";
    bool isMember = false;
    for (const auto& id : infoRef.memberIds)
        if (id == senderId) isMember = true;
    if (!isMember) return "你尚未加入本群，请先 [加入群]";
    if (!PlatformMessagePolicyFH::supportsKind(pl, kind)) {
        switch (pl) {
            case PlatformKindFH::QQ: break;  // QQ 全支持，不会走到
            case PlatformKindFH::WeChat:
                return platCn(pl) + "群禁止发送【文件】（课程简化口径）";
            case PlatformKindFH::Weibo:
                return platCn(pl) + "群仅支持文本/表情，不能发送该类型";
            default: break;
        }
        return "该平台不允许此消息类型";
    }
    if (content.size() > PlatformMessagePolicyFH::maxTextLength(pl))
        return "内容超过" + platCn(pl) + "群文本上限（" +
               std::to_string(PlatformMessagePolicyFH::maxTextLength(pl)) +
               " 字）";
    if (asReply && !PlatformMessagePolicyFH::supportsReply(pl))
        return platCn(pl) + "群不支持引用回复";
    return {};
}

// groupId 按值传递：调用方常写成 runOfficialChat(hits[i])，若按 const& 绑定到
// 注册表内部元素，一旦本会话内解散该群就会悬空引用。
void runOfficialChat(std::string groupId) {
    for (;;) {
        const auto info = findOfficial(groupId);   // 按值返回，可安全持有
        if (!info) {
            noticeFail("群不存在（可能已被移除）。");
            return;
        }
        const auto pl = info->platform;
        const bool hasAcct = g.me->hasPlatformAccount(pl);
        const std::string myId = g.me->platformAccountId(pl);
        bool inGroup = false;
        if (hasAcct)
            for (const auto& id : info->memberIds)
                if (id == myId) inGroup = true;
        const bool isOwner = g.official.isOwnerOf(*g.me, groupId);

        fh_ui::Screen s("群聊天：" + info->name + "（" + platCn(pl) +
                        " 官方/自建群）");
        s.kv("群号", info->groupId);
        s.kv("成员", std::to_string(info->memberIds.size()) + "/" +
                        std::to_string(info->maxMembers) + " 人");
        s.kv("群主", info->ownerId.empty() ? std::string("（官方预置群）")
                                           : nickOf(pl, info->ownerId));
        s.kv("我的状态", inGroup ? "已在群内" : "未加入");
        if (!hasAcct)
            s.text("  注意：你缺少该平台的账号，需先到【账号中心】处理。");

        // 该引用只在本次迭代的渲染阶段使用：下面的分支一旦改动群目录
        // （建群 / 解散）就必须 continue/return，不得再读 info 或 chat。
        const auto& chat = info->chat;
        const int tailRows = 5;
        int budget = s.rowsLeft() - tailRows;
        if (budget < 2) budget = 2;

        // —— 成员面板 ——
        s.blank();
        s.section("群成员 " + std::to_string(info->memberIds.size()) + " 人");
        if (info->memberIds.empty()) s.item("（空）");
        const int memberRows = std::min<int>(
            static_cast<int>(info->memberIds.size()), std::max(1, budget / 2));
        for (int i = 0; i < memberRows; ++i)
            s.item(nickOf(pl, info->memberIds[static_cast<std::size_t>(i)]));
        if (static_cast<int>(info->memberIds.size()) > memberRows)
            s.text("    …… 还有 " +
                   std::to_string(info->memberIds.size() -
                                  static_cast<std::size_t>(memberRows)) +
                   " 人未显示");

        // —— 群消息记录（按该产品视图渲染，阶段 D）——
        s.blank();
        s.section("聊天记录（" + platCn(pl) + " 视图渲染，" +
                  std::to_string(chat.size()) + " 条）");
        const int msgRows = std::min<int>(static_cast<int>(chat.size()),
                                          std::max(1, budget - memberRows));
        const std::size_t begin =
            chat.size() > static_cast<std::size_t>(msgRows)
                ? chat.size() - static_cast<std::size_t>(msgRows)
                : 0;
        if (chat.empty()) s.item("（暂无记录）");
        if (begin > 0)
            s.text("    …… 更早的 " + std::to_string(begin) + " 条未显示");
        for (std::size_t i = begin; i < chat.size(); ++i) {
            const auto& r = chat[i];
            s.item(PlatformMessagePolicyFH::render(pl, r.senderNick, r.content,
                                                   r.kind, fmtClock(r.sentAt)) +
                   (r.isReply ? "（引用回复）" : ""));
        }

        s.blank();
        s.section("可用操作");
        std::vector<fh_ui::MenuItem> ops{
            {'1', "发送消息"}, {'2', "加入本群"}, {'3', "退出本群"}};
        if (pl == PlatformKindFH::WeChat)  // 微信群只能推荐加入 → 补推荐入口
            ops.push_back({'4', "推荐好友入群"});
        if (isOwner) {  // 自建群群主专属：解散 / 转让
            ops.push_back({'5', "解散本群"});
            ops.push_back({'6', "转让群主"});
        }
        ops.push_back({'0', "返回"});
        s.menu(ops, static_cast<int>(ops.size()));
        present(s);
        const char k = waitKey();

        switch (k) {
        case '1': {  // 发送消息：类型 + 内容 + 引用
            if (!inGroup) {
                noticeFail("尚未加入本群，请先按 [2] 加入。");
                continue;
            }
            {
                fh_ui::Screen kind("发送消息：选择消息类型");
                kind.menu({{'1', "文本"}, {'2', "图片"}, {'3', "文件"},
                           {'4', "语音"}, {'5', "表情"}, {'0', "取消"}},
                          6);  // 保持一行，与原版一致
                kind.prompt("请按键选择：");
                kind.flush();
            }
            const char tk = waitKey();
            MessageKindFH kind = MessageKindFH::TEXT;
            std::string kindName;
            switch (tk) {
                case '1': kind = MessageKindFH::TEXT; kindName = "文本"; break;
                case '2': kind = MessageKindFH::IMAGE; kindName = "图片"; break;
                case '3': kind = MessageKindFH::DOCUMENT; kindName = "文件"; break;
                case '4': kind = MessageKindFH::VOICE; kindName = "语音"; break;
                case '5': kind = MessageKindFH::EMOJI; kindName = "表情"; break;
                case '0':
                default: noticeInfo("已取消发送"); continue;
            }
            const std::string what =
                kind == MessageKindFH::IMAGE || kind == MessageKindFH::DOCUMENT
                    ? "（如图片名/文件名）"
                    : "";
            // 表单提示由 askText 自带输出；不再复用本轮已整帧输出的 s：
            // 复用会把旧帧（含旧状态条）整屏重刷一遍，造成提示/结果重复出现
            auto text = askText("输入" + kindName + "消息" + what +
                               "内容（直接回车取消）：");
            if (!text) {
                noticeInfo("已取消发送");
                continue;
            }
            bool wantReply = false;
            if (kind == MessageKindFH::TEXT) {
                std::cout << "  作为引用回复发送？[y]是 / [n]否：" << std::flush;
                wantReply = waitKey() == 'y';
            }
            busy("消息发送");
            const std::string reason =
                officialSendReason(*info, g.me, kind, *text, wantReply);
            if (!reason.empty()) {
                noticeFail(reason);
                continue;
            }
            if (g.official.sendGroupMessage(*g.me, pl, groupId, kind, *text,
                                            wantReply))
                noticeOK("消息已发送（" + platCn(pl) + " 记录自动更新）。");
            else
                noticeFail("消息发送失败，请检查平台规则。");
            continue;
        }
        case '2': {  // 加入本群
            busy("入群处理");
            if (g.official.joinGroup(*g.me, pl, groupId))
                noticeOK("已加入「" + info->name + "」。");
            else {
                if (!hasAcct)
                    noticeFail("你没有该平台的账号"
                               "（微信群需先绑定微信号）。");
                else if (pl == PlatformKindFH::WeChat)
                    noticeFail("微信群只能推荐加入：不受理直接申请，"
                               "请由群内成员推荐你入群。");
                else
                    noticeFail("加入失败：可能已在群内或群已满员。");
            }
            continue;
        }
        case '3': {  // 退出本群
            busy("退群处理");
            if (g.official.leaveGroup(*g.me, groupId)) {
                noticeOK("已退出「" + info->name + "」。");
                return;  // 退群后回到大厅/列表
            }
            if (isOwner)
                noticeFail("群主不能直接退群：请先转让群主（[6]）或解散本群（[5]）。");
            else
                noticeFail("退群失败：可能你并不在该群。");
            continue;
        }
        case '4': {  // 推荐好友入群（仅微信群：由群内成员推荐）
            if (pl != PlatformKindFH::WeChat) {
                noticeFail("仅微信群需「推荐加入」，QQ/微博群请按 [2] 申请加入。");
                continue;
            }
            if (!inGroup) {
                noticeFail("你不在本群：须先由群内成员推荐你入群，之后才能推荐他人。");
                continue;
            }
            auto target = pickProfile(
                true, PlatformKindFH::WeChat, "选择要推荐入群的好友",
                [&](const ProfilePtr& p) {
                    const std::string id =
                        p->platformAccountId(PlatformKindFH::WeChat);
                    const bool already =
                        std::find(info->memberIds.begin(), info->memberIds.end(),
                                  id) != info->memberIds.end();
                    return already ? std::string("  ［已在群内］")
                                   : std::string("  ［可推荐］");
                });
            if (!target) continue;
            busy("推荐入群");
            if (g.official.inviteIntoGroup(*g.me, *target, groupId)) {
                noticeOK("已推荐 " + target->getNickname() + " 进入「" +
                         info->name + "」。");
            } else {
                // 对照 inviteIntoGroup 的拒绝分支给出具体原因，
                // 不再让用户在三个条件里自己猜
                const std::string tgtId =
                    target->platformAccountId(PlatformKindFH::WeChat);
                const bool alreadyIn =
                    std::find(info->memberIds.begin(), info->memberIds.end(),
                              tgtId) != info->memberIds.end();
                if (alreadyIn)
                    noticeFail("推荐失败：" + target->getNickname() +
                               " 已在本群内。");
                else if (info->memberIds.size() >= info->maxMembers)
                    noticeFail("推荐失败：本群已满员（上限 " +
                               std::to_string(info->maxMembers) + " 人）。");
                else
                    noticeFail("推荐失败：对方须已绑定微信号。");
            }
            continue;
        }
        case '5': {  // 解散本群（仅群主）
            fh_ui::Screen c("解散本群 · 二次确认");
            c.text("  解散后本群将从群列表移除、不可恢复。确认解散「" +
                   info->name + "」？");
            c.menu({{'y', "确认解散"}, {'n', "取消"}});
            c.prompt("请按键选择：");
            c.flush();
            if (waitKey() != 'y') {
                noticeInfo("已取消。");
                continue;
            }
            busy("解散处理");
            // info 是注册表的按值快照，解散（从目录移除）不会让它失效，
            // 因此这里可以安全地用它的群名拼提示。
            if (g.official.disbandGroup(*g.me, groupId)) {
                noticeOK("已解散「" + info->name + "」，该群已从群列表移除。");
                return;
            }
            noticeFail("解散失败：仅群主可解散自建群。");
            continue;
        }
        case '6': {  // 转让群主（仅群主）
            std::vector<ProfilePtr> members;
            std::vector<std::string> labels;
            for (const auto& id : info->memberIds) {
                if (id == myId) continue;
                for (const auto& p : g.people)
                    if (p->platformAccountId(pl) == id) {
                        members.push_back(p);
                        labels.push_back(p->getNickname() + "（" + id + "）");
                        break;
                    }
            }
            if (members.empty()) {
                noticeFail("群内没有其他成员可转让。");
                continue;
            }
            const int idx =
                chooseByLabels("选择新群主（转让后你降为普通成员）", labels);
            if (idx < 0) {
                noticeInfo("已取消。");
                continue;
            }
            busy("转让处理");
            if (g.official.transferOwner(
                    *g.me, *members[static_cast<std::size_t>(idx)], groupId))
                noticeOK("已把群主转让给 " +
                         members[static_cast<std::size_t>(idx)]->getNickname() +
                         "。");
            else
                noticeFail("转让失败：仅群主可转让，且目标须在群内。");
            continue;
        }
        case '0':
            return;
        default:
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }
    }
}
}  // namespace fh_client
