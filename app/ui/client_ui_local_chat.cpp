#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 阶段 A：本地正式群（聚合根 GroupFH）会话窗口
// ============================================================

std::string memberLine(const GroupFH& group, const UserPtr& u) {
    const auto& m = group.members().at(u->getId());
    std::string s = " " + std::string(zhRole(m.getRole())) + " " +
                    u->getNickname() + "(" + u->getId() + ") 加入于 " +
                    fmtClock(m.getJoinedAt(), /*withDate=*/true);
    if (m.isMuted()) s += " ［已被禁言］";
    return s;
}

std::vector<UserPtr> sortedMembers(const GroupFH& group) {
    std::vector<UserPtr> list;
    for (const auto& kv : group.members()) list.push_back(kv.second.getUser());
    std::sort(list.begin(), list.end(), [&](const UserPtr& a, const UserPtr& b) {
        const auto ra = group.getRole(a);
        const auto rb = group.getRole(b);
        if (ra != rb) return ra > rb;  // 群主 > 管理员 > 成员
        return a->getId() < b->getId();
    });
    return list;
}

bool hasPriv(const GroupFH& g, const UserPtr& op) {
    const auto role = g.getRole(op);
    return role && (*role == GroupRoleFH::ADMIN || *role == GroupRoleFH::OWNER);
}

std::string localRoleName(const LocalSlot& slot, const UserPtr& op) {
    const auto role = slot.group->getRole(op);
    return role ? zhRole(*role) : "非成员";
}

// —— 各类操作失败时给出“贴近规则”的中文原因 ——
std::string sendFailReason(const LocalSlot& slot, const UserPtr& op) {
    const GroupFH& g = *slot.group;
    if (g.isDisbanded()) return "群已解散，无法发送";
    if (!g.contains(op)) return "你不在该群";
    if (g.isMuted(op) && !hasPriv(g, op))
        return "你被单独禁言，禁言期间不能发言";
    if (g.getConfig().isAllMuted() && !hasPriv(g, op))
        return "全员禁言中，普通成员不能发言";
    return "被群规则拒绝";
}

std::string recallFailReason(const LocalSlot& slot, const UserPtr& op,
                             const std::shared_ptr<MessageFH>& msg) {
    const GroupFH& g = *slot.group;
    if (g.isDisbanded()) return "群已解散，无法操作";
    if (!g.contains(op)) return "你不在该群";
    if (!msg) return "消息不存在";
    if (msg->isRecalled()) return "该消息已撤回，不能重复撤回";
    if (!hasPriv(g, op)) {
        const auto sender = msg->getSender();
        if (!sender || sender->getId() != op->getId())
            return "你只能撤回自己的消息（管理员/群主可撤回任意消息）";
    }
    const auto limit = g.getConfig().getRecallTimeLimit();
    return "已超过可撤回时间窗（本群为 " +
           std::to_string(limit.count()) + " 秒）";
}

std::string inviteFailReason(const LocalSlot& slot, const UserPtr& op,
                             const UserPtr& target) {
    const GroupFH& g = *slot.group;
    if (g.isDisbanded()) return "群已解散";
    if (!g.contains(op)) return "你不在本群，无权邀请";
    if (g.contains(target)) return "对方已是本群成员，不能重复邀请";
    if (g.members().size() >= g.getConfig().getMaxMembers())
        return "群人数已达上限";
    const auto role = g.getRole(op);
    if (slot.platform == PlatformKindFH::QQ) {
        if (!g.getConfig().isMemberInviteEnabled() && role &&
            *role == GroupRoleFH::MEMBER)
            return "QQ 群已关闭普通成员邀请，仅管理员/群主可邀请";
        return "QQ 群拒绝了该邀请";
    }
    if (role && *role == GroupRoleFH::MEMBER)
        return "微信群仅管理员/群主可邀请成员";
    return "微信群拒绝了该邀请";
}

std::string manageMemberFailReason(const LocalSlot& slot, const UserPtr& op,
                                   const UserPtr& target, bool kick) {
    const GroupFH& g = *slot.group;
    if (g.isDisbanded()) return "群已解散";
    if (!g.contains(op)) return "你不在本群";
    if (!g.contains(target)) return "目标不在本群";
    const auto opRole = g.getRole(op);
    const auto tgRole = g.getRole(target);
    if (!opRole || *opRole == GroupRoleFH::MEMBER)
        return kick ? "仅管理员/群主可移除成员"
                    : "仅管理员/群主可设置禁言";
    if (opRole && tgRole && !(*opRole > *tgRole))
        return kick ? "不能移除与自己同级或更高等级成员"
                    : "不能禁言与自己同级或更高等级成员";
    return kick ? "移除被群规则拒绝" : "禁言操作被群规则拒绝";
}

// —— 群设置（二级菜单）：邀请开关 / 撤回时间窗，建群后仍可变更 ——
// 变更走 EDIT_GROUP 授权（管理员及以上），与“全员禁言”是两条不同规则。
void runGroupSettings(LocalSlot& live) {
    GroupFH& grp = *live.group;
    for (;;) {
        const UserPtr meUser = actorFor(g.me, live.platform);

        fh_ui::Screen s("群设置：" + grp.getName());
        drawAccountCard(s);
        s.blank();
        s.section("本群配置");
        s.kv("普通成员可邀请", grp.getConfig().isMemberInviteEnabled()
                                   ? std::string("开启（QQ 概念）")
                                   : std::string("关闭（QQ 概念）"));
        s.kv("全员禁言", grp.getConfig().isAllMuted() ? "开启" : "关闭");
        s.kv("撤回时间窗",
             std::to_string(grp.getConfig().getRecallTimeLimit().count()) + " 秒");
        s.kv("人数上限", std::to_string(grp.getConfig().getMaxMembers()) + " 人");
        s.text("  说明：微信群没有“普通成员可邀请”开关——微信群仅群主可推荐加入。");
        s.blank();
        s.section("可改配置");
        s.menu({{'1', "切换“普通成员可邀请”开关"},
                {'2', "修改撤回时间窗"},
                {'0', "返回"}});
        present(s);
        const char k = waitKey();

        if (k == '0') return;
        if (!meUser) {
            noticeFail("你缺少该平台账号，无法变更群设置。");
            continue;
        }
        if (k == '1') {
            if (live.platform != PlatformKindFH::QQ) {
                noticeFail("微信群没有此开关：微信群仅群主可推荐加入（平台差异）。");
                continue;
            }
            const bool next = !grp.getConfig().isMemberInviteEnabled();
            busy("配置变更");
            if (grp.setMemberInviteEnabled(meUser, next))
                noticeOK(std::string("已") + (next ? "开启" : "关闭") +
                         "“QQ 普通成员可邀请”开关。");
            else
                noticeFail("变更群设置需要管理员及以上身份（或群已解散）。");
            continue;
        }
        if (k == '2') {
            const long sec =
                askNum("  新的撤回时间窗（0~3600 秒，0=不可撤回）> ", 0, 3600);
            if (sec < 0) {
                noticeInfo("已取消");
                continue;
            }
            busy("配置变更");
            if (grp.setRecallTimeLimit(meUser, std::chrono::seconds(sec)))
                noticeOK("撤回时间窗已改为 " + std::to_string(sec) +
                         " 秒（窗口外的消息不可撤回）。");
            else
                noticeFail("变更群设置需要管理员及以上身份（或群已解散）。");
            continue;
        }
        if (k != 0) noticeFail("无效按键：" + std::string(1, k));
    }
}

// —— 以群内其他成员的身份继续操作（验证不同角色的权限差异）——
bool switchActorInGroup(const LocalSlot& live) {
    GroupFH& grp = *live.group;
    const UserPtr cur = actorFor(g.me, live.platform);
    std::vector<std::string> labels;
    std::vector<ProfilePtr> hits;
    for (const auto& u : sortedMembers(grp)) {
        if (cur && u->getId() == cur->getId()) continue;  // 跳过自己
        ProfilePtr p = nullptr;
        for (const auto& q : g.people)
            if (q->platformAccountId(live.platform) == u->getId()) {
                p = q;
                break;
            }
        if (!p) continue;
        labels.push_back(std::string(zhRole(*grp.getRole(u))) + " " +
                         p->getNickname() + "（" + u->getId() + "）");
        hits.push_back(p);
    }
    if (hits.empty()) {
        noticeFail("群内没有其他可切换的演示账号（可先邀请成员入群）。");
        return false;
    }
    const int idx = chooseByLabels("以群内其他成员身份操作", labels,
                                   "  选择后，后续操作将以该成员的身份执行。");
    if (idx < 0) return false;
    g.me = hits[static_cast<std::size_t>(idx)];
    noticeOK("已切换操作身份为 " + g.me->getNickname() +
             "（可直接验证其在群内的权限）。");
    return true;
}

// —— 正式群“更多操作”二级菜单（全部数字键，0 返回会话主界面）——
// 返回 true 表示退回会话主界面；返回 false 表示退出本会话（回到会话列表）。
bool runLocalMore(LocalSlot& live) {
    GroupFH& grp = *live.group;
    for (;;) {
        const UserPtr meUser = actorFor(g.me, live.platform);

        fh_ui::Screen s("更多操作：" + grp.getName() + "（" +
                        platCn(live.platform) + " 管理模式）");
        s.kv("我的身份",
             meUser ? localRoleName(live, meUser) : std::string("非成员"));
        s.kv("成员数", std::to_string(grp.members().size()) + " 人");
        s.kv("群状态", grp.isDisbanded() ? "已解散（仅可返回）" : "正常");
        s.blank();
        s.section("可用操作");
        s.menu({{'1', "切换管理模式"},
                {'2', "转让群主"},
                {'3', "解散群"},
                {'4', "退出本群"},
                {'5', "群设置（邀请开关/撤回窗口）"},
                {'6', "群公告"},
                {'7', "改群名"},
                {'8', "以其他成员身份操作"},
                {'9', "本页操作帮助"},
                {'0', "返回会话"}});
        present(s);
        const char k = waitKey();

        if (k == '0') return true;  // 返回会话主界面
        if (k == '9') {
            fh_ui::Screen h("更多操作 · 说明（阶段 A / 平台差异）");
            h.section("各操作的含义");
            h.kv("切换管理模式",
                 "群成员与消息数据原样保留，仅换绑群策略（任务书 6.(4)：动态变换"
                 "管理特色，数据不受伤害）");
            h.kv("转让群主", "仅群主可操作，原群主降为普通成员（角色属于群成员关系）");
            h.kv("解散群", "仅群主可操作；解散后成员清空、所有操作被拒绝");
            h.kv("退出本群", "普通成员/管理员主动退群；群主须先转让或解散");
            h.kv("群设置", "邀请开关（QQ 概念）与撤回时间窗，需管理员及以上身份");
            h.kv("群公告", "发布/更新群公告，需管理员及以上身份");
            h.kv("改群名", "修改群名，需管理员及以上身份");
            h.kv("切换身份", "以群内其他成员身份操作，便于验证不同角色的权限差异");
            h.prompt("按任意键返回：");
            h.flush();
            waitKey();
            continue;
        }
        if (!meUser) {
            if (k != 0) noticeFail("你缺少该平台账号，无法执行该操作。");
            continue;
        }
        switch (k) {
        case '1': {  // 切换管理模式（成员数据不受伤害）
            const auto next = live.platform == PlatformKindFH::QQ
                                  ? PlatformKindFH::WeChat
                                  : PlatformKindFH::QQ;
            std::shared_ptr<GroupPolicyFH> nextPolicy =
                next == PlatformKindFH::QQ
                    ? std::static_pointer_cast<GroupPolicyFH>(
                          std::make_shared<QQPolicyFH>())
                    : std::static_pointer_cast<GroupPolicyFH>(
                          std::make_shared<WeChatPolicyFH>());
            busy("模式切换");
            const std::size_t before = grp.members().size();
            if (grp.switchPolicy(nextPolicy)) {
                live.platform = next;
                noticeOK("已切换为「" + platCn(next) +
                         "」管理模式，成员数不变（" + std::to_string(before) +
                         " 人），数据未受影响。");
            } else {
                noticeFail("切换失败（群已解散或策略无效）。");
            }
            continue;
        }
        case '2': {  // 转让群主
            std::vector<std::string> labels;
            std::vector<UserPtr> list;
            for (const auto& u : sortedMembers(grp)) {
                if (u->getId() == meUser->getId()) continue;
                labels.push_back(std::string(zhRole(*grp.getRole(u))) + " " +
                                 u->getNickname() + "(" + u->getId() + ")");
                list.push_back(u);
            }
            const int idx = chooseByLabels("选择群主接班人", labels);
            if (idx < 0) continue;
            const UserPtr& target = list[static_cast<std::size_t>(idx)];
            busy("群主转让");
            if (grp.transferOwner(meUser, target))
                noticeOK("群主已转让给 " + target->getNickname() +
                         "，你降为普通成员。");
            else
                noticeFail("仅群主可转让，且不能转让给自己（或群已解散）。");
            continue;
        }
        case '3': {  // 解散群（二次确认）
            if (localRoleName(live, meUser) != std::string("群主")) {
                noticeFail("仅群主可解散群（或群已解散）。");
                continue;
            }
            std::cout << "  再次确认解散群「" << grp.getName()
                      << "」？输入 y 确认，其他任意键取消：";
            if (waitKey() != 'y') {
                noticeInfo("已取消解散");
                continue;
            }
            busy("解散处理");
            if (grp.disband(meUser))
                noticeOK("群已解散，成员全部移出，所有操作被拒绝。");
            else
                noticeFail("解散失败（可能已解散）。");
            continue;
        }
        case '4': {  // 退出本群（G1：任务书 3.(2) 退出群）
            if (localRoleName(live, meUser) == std::string("群主")) {
                noticeFail("群主不能直接退群：请先转让群主（[2]）或解散群（[3]）。");
                continue;
            }
            busy("退群处理");
            if (grp.leaveGroup(meUser)) {
                noticeOK("已退出本群「" + grp.getName() + "」。");
                return false;  // 退群后回到会话列表
            }
            noticeFail("退群失败（你不在群内或群已解散）。");
            continue;
        }
        case '5':
            runGroupSettings(live);
            continue;
        case '6': {  // 发布 / 更新群公告
            auto text = askText("  新群公告内容（直接回车取消）> ");
            if (!text) {
                noticeInfo("已取消发布公告");
                continue;
            }
            busy("公告发布");
            if (grp.publishAnnouncement(meUser, *text))
                noticeOK("群公告已更新。");
            else
                noticeFail("发布公告需要管理员及以上身份（或群已解散）。");
            continue;
        }
        case '7': {  // 修改群名
            auto name = askText("  新群名（直接回车取消）> ");
            if (!name) {
                noticeInfo("已取消改名");
                continue;
            }
            busy("群名修改");
            if (grp.editGroup(meUser, *name))
                noticeOK("群名已改为「" + *name + "」。");
            else
                noticeFail("修改群名需要管理员及以上身份（或群已解散）。");
            continue;
        }
        case '8':
            if (switchActorInGroup(live)) return true;  // 身份生效，回会话主界面
            continue;
        default:
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }
    }
}

void runLocalChat(const LocalSlot& slot) {
    // slot 在 runWorkspace 中传入副本，这里持有可修改引用数组内容——
    // 通过原数组定位，以保证 switchPolicy 等修改可持久化。
    const auto& grp = slot.group;
    LocalSlot& live = *std::find_if(g.locals.begin(), g.locals.end(),
                                    [&](const LocalSlot& s) {
                                        return s.group == grp;
                                    });
    App& sess = g;  // 记录全局会话引用（随后局部 g 指本群聚合根）
    GroupFH& g = *grp;

    for (;;) {
        // 每轮重算“我”：切换操作身份或切换管理模式后即时生效
        const UserPtr meUser = actorFor(sess.me, live.platform);
        const auto& msgs = g.messages();

        fh_ui::Screen s("正式群会话：" + g.getName() + "（" +
                        platCn(live.platform) + " 管理模式）");
        s.kv("群号", std::to_string(g.getGroupNumber()) + "（" + g.getId() + "）");
        s.kv("人数上限", std::to_string(g.getConfig().getMaxMembers()) + " 人");
        s.kv("撤回窗口",
             std::to_string(g.getConfig().getRecallTimeLimit().count()) + " 秒");
        s.kv("全员禁言", g.getConfig().isAllMuted() ? "开启" : "关闭");
        s.kv("我的身份", std::string(localRoleName(live, meUser)) +
                             (meUser && g.isMuted(meUser) ? "（你被禁言）" : ""));
        if (!g.getAnnouncement().empty()) s.kv("群公告", g.getAnnouncement());

        if (g.isDisbanded()) {
            s.blank();
            s.text("  >> 本群已解散，成员已清空，仅可返回。");
            s.blank();
            s.section("可用操作");
            s.menu({{'0', "返回会话列表"}});
            present(s);
            const char k = waitKey();
            if (k == '0') return;
            continue;
        }

        // —— 成员面板与消息面板共享剩余高度：窗口放不下时逐条裁剪并提示 ——
        const auto members = sortedMembers(g);
        const int tailRows = 8;  // 预留：菜单 + 分节标题 + 状态条 + 提示行
        int budget = s.rowsLeft() - tailRows;
        if (budget < 2) budget = 2;

        s.blank();
        s.section("成员 " + std::to_string(members.size()) + "/" +
                  std::to_string(g.getConfig().getMaxMembers()));
        const int memberRows =
            std::min<int>(static_cast<int>(members.size()), std::max(1, budget / 2));
        for (int i = 0; i < memberRows; ++i)
            s.item(memberLine(g, members[static_cast<std::size_t>(i)]));
        if (static_cast<int>(members.size()) > memberRows)
            s.text("    …… 还有 " +
                   std::to_string(members.size() - static_cast<std::size_t>(memberRows)) +
                   " 位成员未显示（放大窗口后可看全）");

        s.blank();
        s.section("消息记录 " + std::to_string(msgs.size()) + " 条");
        const int msgRows = std::min<int>(static_cast<int>(msgs.size()),
                                          std::max(1, budget - memberRows));
        const std::size_t begin =
            msgs.size() > static_cast<std::size_t>(msgRows)
                ? msgs.size() - static_cast<std::size_t>(msgRows)
                : 0;
        if (msgs.empty()) s.item("（暂无消息）");
        if (begin > 0)
            s.text("    …… 更早的 " + std::to_string(begin) + " 条未显示");
        for (std::size_t i = begin; i < msgs.size(); ++i) {
            const auto& m = msgs[i];
            s.item("[" + fmtClock(m->getSentAt()) + "] " +
                   m->getSender()->getNickname() + "：" + m->getContent() +
                   (m->isRecalled() ? "  ［已撤回］" : ""));
        }

        // —— 操作按钮区：数字键为功能键；0 = 返回会话列表（与全局约定一致），
        //     群公告/改群名等低频管理项收进 [8] 更多操作 ——
        s.blank();
        s.section("可用操作");
        if (!meUser) {
            s.text("  提示：当前管理模式为「" + platCn(live.platform) +
                   "」，但你没有该平台账号，仅可浏览本群。");
            s.menu({{'0', "返回会话列表"}});
        } else {
            s.menu({{'1', "发送消息"}, {'2', "撤回消息"}, {'3', "邀请成员"},
                    {'4', "踢出成员"}, {'5', "禁言/解禁"}, {'6', "全员禁言"},
                    {'7', "任命管理员"}, {'8', "更多操作"},
                    {'0', "返回会话列表"}},
                   5);  // 每行 5 项，与原版一致
        }
        present(s);
        const char k = waitKey();
        if (!meUser) {  // 无账号时仅可浏览，0 返回会话列表
            if (k == '0') return;
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }

        switch (k) {
        case '1': {  // 发送消息
            auto text = askText("  消息内容（直接回车取消）> ");
            if (!text) {
                noticeInfo("已取消发送");
                continue;
            }
            auto msg = std::make_shared<MessageFH>(
                "m" + std::to_string(++sess.msgSeq), meUser, *text);
            busy("消息发送");
            if (g.sendMessage(meUser, msg))
                noticeOK("消息已发送。");
            else
                noticeFail(sendFailReason(live, meUser));
            continue;
        }
        case '2': {  // 撤回消息（列最近 10 条，输入序号）
            if (msgs.empty()) {
                noticeFail("当前还没有可撤回的消息");
                continue;
            }
            const std::size_t n = std::min<std::size_t>(10, msgs.size());
            const std::size_t from = msgs.size() - n;
            fh_ui::Screen pick("撤回消息：选择要撤回的那一条");
            pick.section("最近 " + std::to_string(n) + " 条");
            for (std::size_t i = 0; i < n; ++i) {
                const auto& m = msgs[from + i];
                pick.item("[" + std::to_string(i + 1) + "] [" +
                          fmtClock(m->getSentAt()) + "] " +
                          m->getSender()->getNickname() + "：" + m->getContent() +
                          (m->isRecalled() ? "  ［已撤回］" : ""));
            }
            pick.blank();
            pick.prompt("输入要撤回的消息序号（1~" + std::to_string(n) +
                        "，直接回车取消）：");
            pick.flush();
            const long sel = askNum("> ", 1, static_cast<long>(n));
            if (sel < 0) {
                noticeInfo("已取消撤回");
                continue;
            }
            const auto& target = msgs[from + static_cast<std::size_t>(sel - 1)];
            busy("撤回处理");
            if (g.recallMessage(meUser, target->getId()))
                noticeOK("消息已撤回。");
            else
                noticeFail(recallFailReason(live, meUser, target));
            continue;
        }
        case '3': {  // 邀请成员（只能邀请拥有本平台账号的人）
            const auto target = pickProfile(
                /*excludeSelf=*/true, live.platform,
                "选择要邀请的人（" + platCn(live.platform) + " 账号）",
                [&live](const ProfilePtr& p) -> std::string {
                    const auto u = actorFor(p, live.platform);
                    if (!u) return std::string();
                    return live.group->contains(u) ? std::string("  ［已在群内］")
                                                   : std::string("  ［不在群内］");
                });
            if (!target) continue;
            const auto tu = actorFor(target, live.platform);
            busy("邀请处理");
            if (g.inviteMember(meUser, tu))
                noticeOK("已邀请 " + target->getNickname() + " 入群。");
            else
                noticeFail(inviteFailReason(live, meUser, tu));
            continue;
        }
        case '4': {  // 踢出成员
            const auto members = sortedMembers(g);
            std::vector<std::string> labels;
            std::vector<UserPtr> list;
            for (const auto& u : members) {
                if (u->getId() == meUser->getId()) continue;  // 不能操作自己
                labels.push_back(std::string(zhRole(*g.getRole(u))) + " " +
                                 u->getNickname() + "(" + u->getId() + ")");
                list.push_back(u);
            }
            const int idx = chooseByLabels("选择要踢出的成员", labels);
            if (idx < 0) continue;
            const UserPtr& target = list[static_cast<std::size_t>(idx)];
            busy("成员移除");
            if (g.kickMember(meUser, target))
                noticeOK("已将 " + target->getNickname() + " 移出群。");
            else
                noticeFail(manageMemberFailReason(live, meUser, target,
                                                  /*kick=*/true));
            continue;
        }
        case '5': {  // 禁言 / 解除禁言
            const auto members = sortedMembers(g);
            std::vector<std::string> labels;
            std::vector<UserPtr> list;
            for (const auto& u : members) {
                if (u->getId() == meUser->getId()) continue;
                labels.push_back(std::string(zhRole(*g.getRole(u))) + " " +
                                 u->getNickname() + "(" + u->getId() + ")" +
                                 (g.isMuted(u) ? "［已禁言］" : ""));
                list.push_back(u);
            }
            const int idx = chooseByLabels(
                "选择要禁言/解除禁言的成员",
                labels, "  选中后自动取反：已禁言 → 解禁，未禁言 → 禁言。");
            if (idx < 0) continue;
            const UserPtr& target = list[static_cast<std::size_t>(idx)];
            const bool wantMute = !g.isMuted(target);
            busy("禁言设置");
            if (g.muteMember(meUser, target, wantMute))
                noticeOK((wantMute ? "已将 " : "已解除 ") +
                         target->getNickname() + (wantMute ? " 禁言。" : " 的禁言。"));
            else
                noticeFail(manageMemberFailReason(live, meUser, target,
                                                  /*kick=*/false));
            continue;
        }
        case '6': {  // 设置 / 解除全员禁言
            const bool want = !g.getConfig().isAllMuted();
            busy("全员禁言");
            if (g.setAllMute(meUser, want))
                noticeOK(std::string("已") + (want ? "开启" : "解除") +
                         "全员禁言。");
            else {
                if (live.platform == PlatformKindFH::QQ &&
                    localRoleName(live, meUser) == std::string("普通成员"))
                    noticeFail("QQ 群全员禁言需管理员及以上身份。");
                else if (live.platform == PlatformKindFH::WeChat &&
                         localRoleName(live, meUser) != std::string("群主"))
                    noticeFail("微信群全员禁言仅群主可操作（平台差异）。");
                else
                    noticeFail("全员禁言操作被群规则拒绝。");
            }
            continue;
        }
        case '7': {  // 任命 / 撤销管理员（仅群主）
            const auto members = sortedMembers(g);
            std::vector<std::string> labels;
            std::vector<UserPtr> list;
            for (const auto& u : members) {
                const auto role = g.getRole(u);
                if (role == GroupRoleFH::OWNER) continue;  // 群主不可任命
                labels.push_back(std::string(zhRole(*role)) + " " +
                                 u->getNickname() + "(" + u->getId() + ")");
                list.push_back(u);
            }
            const int idx = chooseByLabels(
                "任命 / 撤销管理员",
                labels, "  管理员点选即撤销，普通成员点选即任命。");
            if (idx < 0) continue;
            const UserPtr& target = list[static_cast<std::size_t>(idx)];
            const bool makeAdmin = g.getRole(target) != GroupRoleFH::ADMIN;
            busy("管理员变更");
            if (g.setAdmin(meUser, target, makeAdmin))
                noticeOK((makeAdmin ? "已将 " : "已撤销 ") + target->getNickname() +
                         (makeAdmin ? " 任命为管理员。" : " 的管理员。"));
            else
                noticeFail("仅群主可任命/撤销管理员，或目标已是目标状态。");
            continue;
        }
        case '8': {  // 更多操作：切换管理模式 / 转让 / 解散 / 退群 /
                     // 群设置 / 群公告 / 改群名 / 切换身份
            if (!runLocalMore(live)) return;
            continue;
        }
        case '0':  // 返回会话列表（一级返回，与全局「0 = 返回上级」一致）
            return;
        default:
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }
    }
}
}  // namespace fh_client
