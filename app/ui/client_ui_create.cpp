#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 创建：本地正式群（聚合根，阶段 A） / QQ 临时讨论组
// ============================================================

void runCreateScreen() {
    for (;;) {
        fh_ui::Screen s("创建群 / 讨论组");
        s.section("三种“群”的区别");
        s.item("本地正式群 = 聚合根 GroupFH，支持完整群管理（邀请/踢人/禁言/"
               "任命/转让/解散等），按 QQ / 微信群策略工作");
        s.item("QQ 临时讨论组 = 容量 20 的轻量组，任何成员可邀请、成员自由退、"
               "仅发起人可解散");
        s.item("官方自建群 = 在【官方群大厅】创建，群号自动分配（从 1007 起）");
        s.blank();
        s.section("可用操作");
        s.menu({{'1', "创建本地正式群（QQ/微信）"},
                {'2', "创建 QQ 临时讨论组"},
                {'0', "返回"}});
        present(s);
        const char k = waitKey();

        if (k == '1') {
            {
                fh_ui::Screen p("创建正式群：选择平台");
                p.menu({{'1', "QQ 群"}, {'2', "微信群"}, {'0', "取消"}});
                p.prompt("请按键选择（平台决定使用哪套群策略）：");
                p.flush();
            }
            const char pk = waitKey();
            PlatformKindFH pl;
            if (pk == '1')
                pl = PlatformKindFH::QQ;
            else if (pk == '2')
                pl = PlatformKindFH::WeChat;
            else {
                noticeInfo("已取消");
                continue;
            }
            if (!g.me->hasPlatformAccount(pl)) {
                noticeFail("你缺少该平台账号，无法在该平台建群并担任群主"
                           "（微信群请先绑定微信号）。");
                continue;
            }
            fh_ui::Screen f("创建正式群：填写信息");
            f.kv("平台", platCn(pl));
            f.kv("群主", g.me->getNickname());
            f.prompt("新群名称（直接回车取消）：");
            f.flush();
            auto name = askText("");
            if (!name) {
                noticeInfo("已取消");
                continue;
            }
            bool memberInvite = false;
            if (pl == PlatformKindFH::QQ) {
                f.prompt("是否开启“QQ 普通成员可邀请”开关？[y]开启 / [n]关闭：");
                f.flush();
                memberInvite = waitKey() == 'y';
            }
            f.prompt("群人数上限（默认 30，直接回车使用默认）：");
            f.flush();
            const auto cap = askNum("", 1, 500);
            const std::size_t maxMembers =
                cap < 0 ? 30 : static_cast<std::size_t>(cap);
            busy("建群处理");
            ++g.localSeq;
            const auto policy = pl == PlatformKindFH::QQ
                                    ? std::shared_ptr<GroupPolicyFH>(
                                          std::make_shared<QQPolicyFH>())
                                    : std::shared_ptr<GroupPolicyFH>(
                                          std::make_shared<WeChatPolicyFH>());
            try {
                auto grp = std::make_shared<GroupFH>(
                    "lg-" + std::to_string(g.localSeq),
                    static_cast<unsigned>(9000 + g.localSeq), *name,
                    GroupConfigFH(maxMembers, memberInvite, false,
                                  std::chrono::seconds(120)),
                    policy, actorFor(g.me, pl));
                LocalSlot slot{pl, grp};
                g.locals.push_back(slot);
                noticeOK("正式群「" + *name + "」已创建，你为群主"
                         "（管理模式=" + platCn(pl) + "）。可在【我的会话】进入。");
            } catch (const std::exception& e) {
                noticeFail(std::string("建群异常：") + e.what());
            }
            continue;
        }
        if (k == '2') {
            if (!g.me->hasPlatformAccount(PlatformKindFH::QQ)) {
                noticeFail("QQ 临时讨论组需要 QQ 账号（QQ/微博注册即拥有）。");
                continue;
            }
            fh_ui::Screen d("创建 QQ 临时讨论组");
            d.kv("发起人", g.me->getNickname());
            d.kv("容量", "20 人（成员可互邀、自由退，仅发起人可解散）");
            d.prompt("讨论组名称（直接回车取消）：");
            d.flush();
            auto name = askText("");
            if (!name) {
                noticeInfo("已取消");
                continue;
            }
            busy("建组处理");
            auto disc = std::make_shared<DiscussionGroupFH>(
                "disc-" + std::to_string(g.discs.size() + 1), *name,
                g.me->getQQId());
            g.discs.push_back(disc);
            noticeOK("讨论组「" + *name + "」已创建（仅 QQ 平台概念）。");
            continue;
        }
        if (k == '0') return;
        if (k != 0) noticeFail("无效按键：" + std::string(1, k));
    }
}
}  // namespace fh_client
