#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 官方群大厅：浏览官方群 / 自建官方群 / 加入并进入
// ============================================================

// 在大厅创建“官方自建群”（群注册表体系，群号自动从 1007 起分配）
void createOfficialGroupInHall() {
    {
        fh_ui::Screen s("创建群：选择平台");
        s.menu({{'1', "QQ"}, {'2', "微信"}, {'3', "微博"}, {'0', "取消"}}, 4);
        s.prompt("请按键选择：");
        s.flush();
    }
    const char pk = waitKey();
    PlatformKindFH pl;
    if (pk == '1')
        pl = PlatformKindFH::QQ;
    else if (pk == '2')
        pl = PlatformKindFH::WeChat;
    else if (pk == '3')
        pl = PlatformKindFH::Weibo;
    else {
        noticeInfo("已取消建群");
        return;
    }
    if (!g.me->hasPlatformAccount(pl)) {
        noticeFail("你没有该平台的账号，不能在此平台建群"
                   "（微信群需先绑定微信号）。");
        return;
    }
    fh_ui::Screen s("创建群：填写群名称");
    s.kv("平台", platCn(pl));
    s.prompt("新群名称（直接回车取消）：");
    s.flush();
    auto name = askText("");
    if (!name) {
        noticeInfo("已取消建群");
        return;
    }
    busy("建群处理");
    if (g.official.createGroup(*g.me, pl, *name)) {
        const auto list = g.official.groupsOfPlatform(pl);
        const std::string nid = list.back().groupId;
        noticeOK("创建成功：群号 " + nid + "，你已自动成为群主。");
        runOfficialChat(nid);
    } else {
        noticeFail("建群失败：群名不能为空或账号缺失。");
    }
}

void runHall() {
    for (;;) {
        fh_ui::Screen s("群大厅 · 官方预置群与自建群");
        s.kv("预置群号", "QQ 1001~1002 / 微信 1003~1004 / 微博 1005~1006");
        s.kv("自建群号", "从 1007 起自动分配");
        s.text("  本大厅属“群注册表”体系；【创建】里的本地正式群属“聚合根”体系"
               "（群号 9000+，具备完整群管理）。");
        const auto& groups = g.official;
        std::vector<std::string> labels;
        std::vector<fh_ui::MenuItem> groupItems;
        std::vector<std::string> hits;   // 只存群号，避免持有内部元素的指针
        for (const auto pl :
             {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo}) {
            for (const GroupInfoFH& gi : groups.groupsOfPlatform(pl)) {
                const std::string myId = g.me->platformAccountId(pl);
                // 按值返回的快照，直接用 isMember 判断，无需自己遍历成员表
                const bool joined = !myId.empty() && groups.isMember(gi.groupId, myId);
                const std::string label =
                    "[" + platCn(pl) + " 群号 " + gi.groupId + "] " + gi.name +
                    (gi.predefined ? "（官方）" : "（自建）") + " 成员 " +
                    std::to_string(gi.memberIds.size()) + "/" +
                    std::to_string(gi.maxMembers) +
                    (joined ? "  ［我已加入］" : "");
                labels.push_back(label);
                groupItems.push_back(fh_ui::MenuItem{
                    static_cast<char>('1' + static_cast<int>(labels.size()) - 1), label});
                hits.push_back(gi.groupId);
            }
        }
        // 动态编号：群序号 1..n，建群键 n+1，避免与群序号冲突
        const std::size_t createKey = labels.size() + 1;
        s.blank();
        s.section("可选群（" + std::to_string(labels.size()) + " 个）");

        if (labels.size() <= 8) {
            s.menu(groupItems, 1);  // 群标签较长，保持一行一个（与原版一致）
            s.menu({{static_cast<char>('0' + createKey), "创建群（群号自动分配）"},
                    {'0', "返回"}},
                   2);
            present(s, "请按键选择：");
            const char k = waitKey();
            if (k == '0') return;
            if (static_cast<std::size_t>(k - '0') == createKey) {
                createOfficialGroupInHall();
                continue;
            }
            if (k >= '1' && k <= static_cast<char>('0' + labels.size())) {
                runOfficialChat(hits[static_cast<std::size_t>(k - '1')]);
                continue;
            }
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }

        for (std::size_t i = 0; i < labels.size(); ++i)
            s.text("    [" + std::to_string(i + 1) + "] " + labels[i]);
        s.text("    [" + std::to_string(createKey) + "] 创建群（群号自动分配）");
        present(s, "输入群序号（直接回车返回）：");
        const long sel = askNum("> ", 1, static_cast<long>(createKey));
        if (sel < 0) return;
        if (static_cast<std::size_t>(sel) == createKey) {
            createOfficialGroupInHall();
            continue;
        }
        runOfficialChat(hits[static_cast<std::size_t>(sel - 1)]);
    }
}
}  // namespace fh_client
