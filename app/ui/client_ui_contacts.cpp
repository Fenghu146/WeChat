#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 阶段 C：通讯录 —— QQ/微信双向好友 + 微博单向关注
// ============================================================

// 三个好友列表共享一个行预算：窗口放不下时按列表顺序裁剪，并在末尾提示剩余条数。
void drawFriendLists(fh_ui::Screen& s, int budget) {
    const auto& p = g.me;
    auto findProfile = [](PlatformKindFH pl,
                          const std::string& id) -> ProfilePtr {
        for (const auto& q : g.people)
            if (q->platformAccountId(pl) == id) return q;
        return nullptr;
    };

    struct List {
        std::string title;
        PlatformKindFH pl;
        std::vector<std::string> ids;
    };
    std::vector<List> lists;
    lists.push_back(List{"QQ 好友", PlatformKindFH::QQ,
                         g.friends.friendIds(*p, PlatformKindFH::QQ)});
    lists.push_back(List{"微信好友", PlatformKindFH::WeChat,
                         g.friends.friendIds(*p, PlatformKindFH::WeChat)});
    lists.push_back(List{"微博关注（单向）", PlatformKindFH::Weibo,
                         g.friends.followingIds(*p)});

    int left = std::max(6, budget);
    for (std::size_t li = 0; li < lists.size(); ++li) {
        const auto& L = lists[li];
        const int share =
            std::max(1, left / static_cast<int>(lists.size() - li));
        const int show =
            std::min<int>(static_cast<int>(L.ids.size()), share);

        s.blank();
        s.section(L.title + " " + std::to_string(L.ids.size()) + " 人");
        if (L.ids.empty()) s.item("（空）");
        for (int i = 0; i < show; ++i) {
            const std::string& id = L.ids[static_cast<std::size_t>(i)];
            std::string line = nickOf(L.pl, id);
            if (ProfilePtr q = findProfile(L.pl, id)) {
                const std::string rk = g.friends.remarkOf(*p, *q, L.pl);
                if (!rk.empty()) line += "（备注：" + rk + "）";
            }
            s.item(line);
        }
        if (static_cast<int>(L.ids.size()) > show)
            s.text("    …… 还有 " +
                   std::to_string(L.ids.size() - static_cast<std::size_t>(show)) +
                   " 人未显示（放大窗口后可看全）");

        left -= 1 + (show > 0 ? show : 1);
        if (left < 1) left = 1;
    }
}

void runContacts() {
    for (;;) {
        fh_ui::Screen s("通讯录 · 好友与关注（阶段 C · 按平台隔离）");
        drawFriendLists(s, s.rowsLeft() - 7);  // 预留：菜单 + 状态条 + 提示行

        s.blank();
        s.section("可用操作");
        s.menu({{'1', "添加QQ好友"}, {'2', "添加微信好友"}, {'3', "微博关注"},
                {'4', "删除QQ好友"}, {'5', "删除微信好友"}, {'6', "取消微博关注"},
                {'7', "修改好友备注"}, {'8', "查询共同好友"},
                {'9', "跨服务推荐添加"}, {'0', "返回"}});
        present(s);
        const char k = waitKey();

        ProfilePtr target = nullptr;
        auto needPick = [&](PlatformKindFH pl, const std::string& title,
                            bool markRelation = false) -> bool {
            std::function<std::string(const ProfilePtr&)> annotate;
            if (markRelation)
                annotate = [pl](const ProfilePtr& p) { return friendMark(p, pl); };
            target = pickProfile(/*excludeSelf=*/true, pl, title, annotate);
            return target != nullptr;
        };
        switch (k) {
        case '1':
            if (needPick(PlatformKindFH::QQ, "  选择要添加为 QQ 好友的人> ", true)) {
                busy("好友处理");
                if (g.friends.makeFriends(*g.me, *target, PlatformKindFH::QQ))
                    noticeOK("与 " + target->getNickname() + " 已成为 QQ 好友（双向）。");
                else
                    noticeFail("添加失败：你与 " + target->getNickname() +
                               " 在 QQ 已存在好友关系（双向好友不能重复添加）。");
            }
            continue;
        case '2':
            if (needPick(PlatformKindFH::WeChat, "  选择要添加为微信好友的人> ", true)) {
                busy("好友处理");
                if (g.friends.makeFriends(*g.me, *target, PlatformKindFH::WeChat))
                    noticeOK("与 " + target->getNickname() + " 已成为微信好友（双方均需绑定微信）。");
                else
                    noticeFail("添加失败：你与 " + target->getNickname() +
                               " 在微信已存在好友关系（重复添加无效）。");
            }
            continue;
        case '3':
            if (needPick(PlatformKindFH::Weibo, "  选择要微博关注的人> ", true)) {
                busy("关注处理");
                if (g.friends.follow(*g.me, *target))
                    noticeOK("已关注 " + target->getNickname() + "（微博单向关注）。");
                else
                    noticeFail("关注失败：你已关注 " + target->getNickname() +
                               "（微博为单向关注，不能重复关注）。");
            }
            continue;
        case '4':
            if (needPick(PlatformKindFH::QQ, "  选择要删除的 QQ 好友> ", true)) {
                busy("删除好友");
                if (g.friends.unfriend(*g.me, *target, PlatformKindFH::QQ))
                    noticeOK("已删除 QQ 好友 " + target->getNickname() +
                             "（微信/微博关系不受影响）。");
                else
                    noticeFail("删除失败：对方并非你的 QQ 双向好友。");
            }
            continue;
        case '5':
            if (needPick(PlatformKindFH::WeChat, "  选择要删除的微信好友> ", true)) {
                busy("删除好友");
                if (g.friends.unfriend(*g.me, *target, PlatformKindFH::WeChat))
                    noticeOK("已删除微信好友 " + target->getNickname() + "。");
                else
                    noticeFail("删除失败：对方并非你的微信双向好友。");
            }
            continue;
        case '6':
            if (needPick(PlatformKindFH::Weibo, "  选择要取消关注的人> ", true)) {
                busy("取关处理");
                if (g.friends.unfollow(*g.me, *target))
                    noticeOK("已取消关注 " + target->getNickname() + "。");
                else
                    noticeFail("取消关注失败：你并未关注对方。");
            }
            continue;
        case '7': {  // 修改好友备注（任务书 2.(1) 好友信息“修改”）
            const int pl =
                chooseByLabels("给哪个平台的好友写备注？",
                               {"QQ 好友", "微信好友"});
            if (pl >= 0 &&
                needPick(pl == 0 ? PlatformKindFH::QQ : PlatformKindFH::WeChat,
                         "选择要备注的好友", true)) {
                // 提示由 askText 自带输出，避免复用已整帧输出的 s 导致旧帧重刷
                const auto remark = askText("输入备注名（直接回车取消）：");
                if (remark) {
                    const PlatformKindFH pf = pl == 0 ? PlatformKindFH::QQ
                                                      : PlatformKindFH::WeChat;
                    if (g.friends.setRemark(*g.me, *target, pf, *remark))
                        noticeOK("已将 " + target->getNickname() +
                                 " 备注为「" + *remark + "」。");
                    else
                        noticeFail("备注失败：对方需是你的双向好友"
                                   "（微博关注不支持备注）。");
                } else {
                    noticeFail("已取消。");
                }
            }
            continue;
        }
        case '8': {  // 查询共同好友（任务书 2.(2)）
            const int w =
                chooseByLabels("查询哪类共同关系",
                               {"QQ 共同好友", "微信共同好友", "微博共同关注"});
            PlatformKindFH pf = PlatformKindFH::QQ;
            bool okPick = false;
            if (w == 0)
                okPick = needPick(PlatformKindFH::QQ, "选择要对比的人");
            else if (w == 1) {
                pf = PlatformKindFH::WeChat;
                okPick = needPick(PlatformKindFH::WeChat, "选择要对比的人");
            } else if (w == 2) {
                pf = PlatformKindFH::Weibo;
                okPick = needPick(PlatformKindFH::Weibo, "选择要对比的人");
            } else {
                noticeFail("已取消。");
                continue;
            }
            if (!okPick) {
                noticeFail("已取消。");
                continue;
            }
            busy("共同好友计算");
            std::vector<std::string> ids;
            std::string title;
            if (w <= 1) {
                ids = g.friends.commonFriends(*g.me, *target, pf);
                title = "你与 " + target->getNickname() + " 在" +
                        platCn(pf) + "的共同好友";
            } else {
                ids = g.friends.commonFollowing(*g.me, *target);
                title =
                    "你与 " + target->getNickname() + " 在微博的共同关注";
            }
            fh_ui::Screen r("共同关系查询");
            r.kv("对比对象", target->getNickname());
            r.kv("平台", platCn(pf));
            r.kv("共同数量", std::to_string(ids.size()) + " 人");
            r.blank();
            r.section(title);
            if (ids.empty()) r.item("（空）");
            const int show = std::min<int>(
                static_cast<int>(ids.size()), std::max(1, r.rowsLeft() - 1));
            for (int i = 0; i < show; ++i)
                r.item(nickOf(pf, ids[static_cast<std::size_t>(i)]) + " [" +
                       ids[static_cast<std::size_t>(i)] + "]");
            if (static_cast<int>(ids.size()) > show)
                r.text("    …… 还有 " +
                       std::to_string(ids.size() - static_cast<std::size_t>(show)) +
                       " 人未显示");
            present(r, "按任意键返回：");
            waitKey();
            continue;
        }
        case '9': {  // 跨服务推荐添加好友（任务书 2.(2)/6.(3)）
            const int d = chooseByLabels(
                "跨服务推荐添加",
                {"依据 QQ 好友 → 添加微信好友", "依据微信好友 → 添加 QQ 好友"});
            if (d < 0) {
                noticeFail("已取消。");
                continue;
            }
            const PlatformKindFH from =
                d == 0 ? PlatformKindFH::QQ : PlatformKindFH::WeChat;
            const PlatformKindFH to =
                d == 0 ? PlatformKindFH::WeChat : PlatformKindFH::QQ;
            // 入口校验（任务书 6.(3)）：跨服务推荐要求本人已开通「目标」服务，
            // 且具备目标平台账号。不满足时直接拦截并指明去处，避免进入
            // 「暂无可推荐」诊断屏后看到“候选 N 人”产生自相矛盾的误解。
            if (!g.me->hasPlatformAccount(to)) {
                noticeFail(std::string("你还没有") + toZhName(to) +
                           "账号，请先到【账号中心】[3] 绑定后再使用本方向推荐。");
                continue;
            }
            if (!g.me->isActivated(to)) {
                noticeFail(std::string("请先在【账号中心】[1] 开通「") +
                           toZhName(to) + "」服务，再使用跨服务推荐。");
                continue;
            }
            busy("推荐计算");
            const auto rec = g.friends.recommendFriendsFrom(
                *g.me, g.registry, from, to);
            if (rec.empty()) {
                // 无候选时逐条回显前置条件与当前状态，避免用户以为功能损坏
                const std::size_t fromFriendCount =
                    g.friends.friendIds(*g.me, from).size();
                // ③④ 一律以「你的来源平台好友」为基数（交集语义）：
                // 只有来源好友里已绑定目标平台的人，才可能被推荐。
                std::size_t withTarget = 0;    // 其中已绑定目标平台账号的人
                std::size_t notYetTarget = 0;  // 其中尚不是目标平台好友的人
                for (const std::string& fid :
                     g.friends.friendIds(*g.me, from)) {
                    const UserProfileFH* p = nullptr;
                    for (const auto& q : g.people)
                        if (q->platformAccountId(from) == fid) {
                            p = q.get();
                            break;
                        }
                    if (!p || !p->hasPlatformAccount(to)) continue;
                    ++withTarget;
                    if (!g.friends.isFriend(*g.me, *p, to)) ++notYetTarget;
                }
                auto mark = [](bool ok) { return ok ? "✔" : "✘"; };
                const std::string fromCn = toZhName(from);
                const std::string toCn = toZhName(to);
                fh_ui::Screen r("跨服务推荐添加好友 · 暂无可推荐");
                r.kv("方向", fromCn + " 好友 → 添加" + toCn +
                                "好友（任务书 2.(2)、6.(3)）");
                r.text("  以下四个条件须同时满足，逐条核对当前状态：");
                r.item("① 本人已开通来源与目标服务：" + fromCn + " " +
                       mark(g.me->isActivated(from)) + "   " + toCn + " " +
                       mark(g.me->isActivated(to)) + "（可在【账号中心】[1] 开通）");
                r.item("② 对方已是你的" + fromCn + "好友：当前 " +
                       std::to_string(fromFriendCount) + " 人" +
                       (fromFriendCount ? std::string()
                                        : std::string("（【通讯录】[1] 先加好友）")));
                r.item("③ 你的" + fromCn + "好友中已绑定" + toCn + "账号：" +
                       std::to_string(withTarget) + " 人");
                r.item("④ 其中尚不是你的" + toCn + "好友：" +
                       std::to_string(notYetTarget) + " 人");
                r.blank();
                r.text("  推荐名单 = 上述四条件的交集；任一为 0，可推荐即 0 人。");
                // 入口已拦截“目标服务未开通”，这里唯一可能 ✘ 的是来源服务
                r.text("  建议顺序：账号中心开通" + fromCn +
                       "（来源服务） → 通讯录加 " + fromCn +
                       "好友 → 回到 [9] 选择本方向。");
                present(r, "按任意键返回：");
                waitKey();
                continue;
            }
            std::vector<std::string> labels;
            for (const auto* q : rec)
                labels.push_back(std::string(q->getNickname()) + "（" +
                                 toZhName(from) + "好友 → " + toZhName(to) +
                                 " " + q->platformAccountId(to) + "）");
            const int idx =
                chooseByLabels("选择要添加为好友的人", labels);
            if (idx < 0) {
                noticeFail("已取消。");
                continue;
            }
            if (g.friends.addFriendFromRecommendation(*g.me, *rec[idx], from,
                                                      to))
                noticeOK(std::string("已依据") + toZhName(from) +
                         "好友关系，将 " + rec[idx]->getNickname() + " 添加为" +
                         toZhName(to) + "好友。");
            else
                noticeFail("添加失败。");
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
