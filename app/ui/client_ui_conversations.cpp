#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 阶段 C：QQ 临时讨论组
// ============================================================

void runDiscChat(std::size_t index) {
    for (;;) {
        auto& disc = g.discs[index];
        if (!disc) {
            noticeFail("讨论组不存在");
            return;
        }
        const bool disbanded = disc->isDisbanded();

        fh_ui::Screen s("QQ 临时讨论组：" + disc->getName());
        s.kv("组标识", disc->getId());
        s.kv("发起人", nickOf(PlatformKindFH::QQ, disc->getCreatorId()));
        s.kv("人数", std::to_string(disc->size()) + "/" +
                        std::to_string(disc->capacity()) + " 人");
        s.kv("状态", disbanded ? "已解散（成员已清空）" : "正常");

        if (disbanded) {
            s.blank();
            s.section("可用操作");
            s.menu({{'0', "返回"}});
            present(s);
            if (waitKey() == '0') return;
            continue;
        }

        const auto& ids = disc->memberIds();
        s.blank();
        s.section("成员 " + std::to_string(ids.size()) + " 人");
        const int budget = std::max(1, s.rowsLeft() - 5);
        const int shown = std::min<int>(static_cast<int>(ids.size()), budget);
        for (int i = 0; i < shown; ++i)
            s.item(nickOf(PlatformKindFH::QQ, ids[static_cast<std::size_t>(i)]));
        if (static_cast<int>(ids.size()) > shown)
            s.text("    …… 还有 " +
                   std::to_string(ids.size() - static_cast<std::size_t>(shown)) +
                   " 人未显示");

        s.blank();
        s.section("可用操作");
        s.menu({{'1', "邀请成员"},
                {'2', "退出讨论组"},
                {'3', "解散讨论组（仅发起人）"},
                {'0', "返回"}},
               4);  // 保持一行，与原版一致
        present(s);
        const char k = waitKey();
        switch (k) {
        case '1': {
            const auto target = pickProfile(
                /*excludeSelf=*/true, PlatformKindFH::QQ,
                "选择要邀请的人（QQ 账号）",
                [&disc](const ProfilePtr& p) -> std::string {
                    return disc->contains(p->getQQId()) ? std::string("  ［已在组内］")
                                                        : std::string("  ［不在组内］");
                });
            if (!target) continue;
            busy("邀请入组");
            if (disc->invite(*g.me, *target))
                noticeOK("已邀请 " + target->getNickname() + " 进入讨论组。");
            else {
                if (disc->contains(target->getQQId()))
                    noticeFail("对方已在讨论组内。");
                else
                    noticeFail("邀请失败：QQ 临时讨论组任何成员都可邀请，"
                               "但要求你本人已在组内且未满员。");
            }
            continue;
        }
        case '2':
            busy("退出处理");
            if (disc->quit(*g.me)) {
                noticeOK("已退出讨论组。");
                return;  // 不再是成员，返回会话列表
            }
            noticeFail("退出失败：你不在该讨论组。");
            continue;
        case '3':
            if (disc->getCreatorId() != g.me->getQQId()) {
                noticeFail("仅发起人可解散讨论组（平台差异点）。");
                continue;
            }
            busy("解散处理");
            disc->disband(*g.me);
            noticeOK("讨论组已解散，成员清空，后续邀请/退出被拒绝。");
            continue;
        case '0':
            return;
        default:
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }
    }
}

// ============================================================
// 我的会话列表：本地正式群 / 官方群 / QQ 讨论组
// ============================================================

enum class ConvKind { Local, Official, Disc };

void openConversation(ConvKind kind, const std::string& key) {
    if (kind == ConvKind::Local) {
        for (std::size_t i = 0; i < g.locals.size(); ++i) {
            if (g.locals[i].group->getId() == key) {
                runLocalChat(g.locals[i]);
                return;
            }
        }
        noticeFail("该正式群已不存在");
        return;
    }
    if (kind == ConvKind::Official) {
        runOfficialChat(key);
        return;
    }
    for (std::size_t i = 0; i < g.discs.size(); ++i) {
        if (g.discs[i]->getId() == key) {
            runDiscChat(i);
            return;
        }
    }
    noticeFail("该讨论组已不存在");
}

void runConversationList() {
    struct Item {
        ConvKind kind;
        std::string key;
        std::string label;
    };
    std::vector<Item> items;

    // 本地正式群：需“我”在该群对应平台有账号，且我是成员
    for (const auto& s : g.locals) {
        if (s.group->isDisbanded()) continue;
        const auto u = actorFor(g.me, s.platform);
        if (!u || !s.group->contains(u)) continue;
        items.push_back(Item{ConvKind::Local, s.group->getId(),
                             "正式群 · " + platCn(s.platform) + " · " +
                                 s.group->getName() + "（群号 " +
                                 std::to_string(s.group->getGroupNumber()) +
                                 "）"});
    }
    // 官方群 / 自建官方群
    for (const GroupInfoFH& gi : g.official.groupsOfUser(*g.me)) {
        items.push_back(Item{ConvKind::Official, gi.groupId,
                             "官方群 · " + platCn(gi.platform) + " · " +
                                 gi.name + "（群号 " + gi.groupId + "）"});
    }
    // QQ 临时讨论组
    for (const auto& d : g.discs) {
        if (d->isDisbanded()) continue;
        if (d->contains(g.me->getQQId()))
            items.push_back(Item{ConvKind::Disc, d->getId(),
                                 "讨论组 · QQ · " + d->getName() + "（" +
                                     std::to_string(d->size()) + " 人）"});
    }

    if (items.empty()) {
        fh_ui::Screen s("我的会话列表");
        s.text("  还没有可进入的会话。可以：");
        s.item("到【官方群大厅】加入一个官方群");
        s.item("或到【创建】里新建一个正式群 / 讨论组");
        present(s, "按任意键返回主界面：");
        waitKey();
        return;
    }
    std::vector<std::string> labels;
    for (const auto& it : items) labels.push_back(it.label);
    const int sel = chooseByLabels(
        "选择要进入的会话", labels,
        "  体系说明：正式群 = 聚合根 GroupFH（群号 9000+，具备完整群管理与策略切换）；"
        "官方/自建群 = 群注册表（群号 1001~1006 / 1007+，承载消息类型差异）；"
        "讨论组 = QQ 临时讨论组（容量小、全员可邀请）。");
    if (sel < 0) return;
    const auto& it = items[static_cast<std::size_t>(sel)];
    openConversation(it.kind, it.key);
}
}  // namespace fh_client
