#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 阶段 B：账号中心 —— 开通 / 登录联动 / 退出 / 绑定微信
// ============================================================

std::optional<PlatformKindFH> askPlatformService() {
    {
        fh_ui::Screen s("选择服务");
        s.menu({{'1', "QQ"}, {'2', "微信"}, {'3', "微博"}, {'0', "取消"}}, 4);
        s.prompt("请按键选择：");
        s.flush();
    }
    const char k = waitKey();
    switch (k) {
        case '1': return PlatformKindFH::QQ;
        case '2': return PlatformKindFH::WeChat;
        case '3': return PlatformKindFH::Weibo;
        default:  return std::nullopt;
    }
}

// 按当前状态给出「下一步该按哪个键」，把开通 → 登录 → 退出串成一条明线。
std::string accountNextStep() {
    const auto& p = g.me;
    for (const auto pl :
         {PlatformKindFH::WeChat, PlatformKindFH::QQ, PlatformKindFH::Weibo})
        if (p->hasPlatformAccount(pl) && !p->isActivated(pl))
            return "按 [1] 开通" + platCn(pl) + "服务";
    for (const auto pl :
         {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo})
        if (p->isActivated(pl) && !p->isOnline(pl))
            return "按 [4] 登录" + platCn(pl) + "（其余已开通服务会自动登录）";
    for (const auto pl :
         {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo})
        if (p->isOnline(pl))
            return "按 [5] 退出单服务 / [6] 退出全部";
    return "按 [1] 开通服务";
}

void runServiceCenter() {
    for (;;) {
        fh_ui::Screen s("账号中心（阶段 B · 多产品体系）");
        drawAccountCard(s);

        s.blank();
        s.section("平台规则");
        s.item("QQ/微博共享号码；微信独立号码可绑定 QQ；开通后才能登录");
        s.item("任一服务登录后，其余已开通服务自动登录");
        s.item("状态口径：有账号 → 已开通（自选启用）→ 在线；三者互相独立，"
               "绑定微信号 ≠ 已开通微信");
        s.kv("建议下一步", accountNextStep());

        s.blank();
        s.section("可用操作");
        s.menu({{'1', "开通服务"}, {'2', "取消开通"}, {'3', "绑定微信号"},
                {'4', "登录服务"}, {'5', "退出单服务"}, {'6', "退出全部"},
                {'0', "返回"}});
        present(s);
        const char k = waitKey();
        switch (k) {
        case '1': {
            auto pl = askPlatformService();
            if (!pl) { noticeInfo("已取消"); continue; }
            busy("开通处理");
            if (g.activation.activate(*g.me, *pl)) {
                noticeOK("已开通" + platCn(*pl) + "服务。");
            } else {
                if (*pl == PlatformKindFH::WeChat &&
                    !g.me->hasWeChatAccount())
                    noticeFail("开通微信前必须先绑定微信号（按 [3]）。");
                else if (g.me->isActivated(*pl))
                    noticeFail("该服务已经开通（重复开通无效，幂等规则）。");
                else
                    noticeFail("开通失败。");
            }
            continue;
        }
        case '2': {
            auto pl = askPlatformService();
            if (!pl) { noticeInfo("已取消"); continue; }
            busy("取消开通");
            if (g.activation.deactivate(*g.me, *pl))
                noticeOK("已取消开通" + platCn(*pl) + "服务。");
            else if (g.me->isOnline(*pl))
                noticeFail("该服务仍在线，须先退出登录才能取消开通。");
            else
                noticeFail("该服务本就未开通。");
            continue;
        }
        case '3': {
            if (g.me->hasWeChatAccount()) {
                noticeFail("你已绑定微信号 " + g.me->getWeChatId() +
                           "，每人只能绑定一个。");
                continue;
            }
            // 提示由 askText 自带输出，避免复用已整帧输出的 s 导致旧帧重刷
            auto wid = askText("输入要绑定的微信号（直接回车取消）：");
            if (!wid) { noticeInfo("已取消绑定"); continue; }
            busy("绑定处理");
            if (g.registry.bindWeChat(g.me, *wid))
                noticeOK("绑定成功：微信号 " + *wid +
                         "（可继续按 [1] 开通微信服务）。");
            else
                noticeFail("绑定失败：微信号不能为空、重复或已绑定。");
            continue;
        }
        case '4': {
            auto pl = askPlatformService();
            if (!pl) { noticeInfo("已取消"); continue; }
            busy("登录处理");
            if (g.login.login(*g.me, *pl)) {
                std::string on;
                for (const auto p : g.me->onlinePlatforms()) on += platCn(p) + " ";
                noticeOK("已登录" + platCn(*pl) + "；按联动规则，其余已开通服务"
                         "自动登录，当前在线：" + on);
            } else {
                if (!g.me->isActivated(*pl))
                    noticeFail("未开通" + platCn(*pl) +
                               "服务，请先按 [1] 开通后再登录。");
                else
                    noticeFail("登录失败。");
            }
            continue;
        }
        case '5': {
            auto pl = askPlatformService();
            if (!pl) { noticeInfo("已取消"); continue; }
            busy("退出处理");
            if (g.login.logout(*g.me, *pl))
                noticeOK("已退出" + platCn(*pl) + "登录（其它服务不受影响）。");
            else
                noticeFail("该服务并未在线。");
            continue;
        }
        case '6':
            busy("全部退出");
            g.login.logoutAll(*g.me);
            noticeOK("已退出全部服务。");
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
// 账号选择 / 注册（启动与切换共用）
// ============================================================

// 返回 true 表示已选定 g.me；返回 false 表示请求退出程序
bool runAccountGate() {
    for (;;) {
        fh_ui::Screen s("微X 平台 · 终端客户端（手动测试工作台）");
        s.text("  请选择当前操作的自然人账号（演示数据）：");
        const std::size_t n = g.people.size();
        // 注册 / 说明采用动态编号（账号数 +1 / +2），避免与账号序号冲突
        const std::size_t regKey = n + 1;
        const std::size_t helpKey = n + 2;

        std::vector<ProfilePtr> hits;
        std::vector<fh_ui::MenuItem> accounts;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& p = g.people[i];
            const std::string label =
                p->getNickname() + "（QQ/微博 " + p->getQQId() +
                (p->hasWeChatAccount() ? "，微信 " + p->getWeChatId()
                                       : "，未绑定微信") +
                "）";
            if (n <= 7)
                accounts.push_back(
                    fh_ui::MenuItem{static_cast<char>('1' + i), label});
            else
                s.text("    [" + std::to_string(i + 1) + "] " + label);
            hits.push_back(p);
        }

        s.blank();
        s.section("演示账号（" + std::to_string(n) + " 个）");
        if (n <= 7) {
            s.menu(accounts, 1);  // 账号标签较长，保持一行一个
        } else {
            s.text("    （账号较多，请按序号输入）");
        }

        s.blank();
        s.section("其他操作");
        if (n <= 7) {
            s.menu({{static_cast<char>('0' + regKey), "注册新账号"},
                    {static_cast<char>('0' + helpKey), "操作说明"},
                    {'0', "退出"}});
        } else {
            s.text("    [" + std::to_string(regKey) + "] 注册新账号");
            s.text("    [" + std::to_string(helpKey) + "] 操作说明");
            s.text("    [0] 退出");
        }
        present(s);
        std::size_t num = 0;
        if (n <= 7) {
            const char k = waitKey();
            if (k == '0') return false;
            num = (k >= '0' && k <= '9') ? static_cast<std::size_t>(k - '0') : 0;
            if (num == 0) {
                if (k != 0) noticeFail("无效按键：" + std::string(1, k));
                continue;
            }
        } else {
            const long r = askNum("> ", 0, static_cast<long>(helpKey));
            if (r <= 0) return false;
            num = static_cast<std::size_t>(r);
        }
        if (num == regKey) {
            fh_ui::Screen r("注册新账号");
            r.prompt("新 QQ 号（唯一，直接回车取消）：");
            r.flush();
            auto qq = askText("");
            if (!qq) { noticeInfo("已取消注册"); continue; }
            r.kv("QQ 号", *qq);

            r.prompt("昵称（直接回车取消）：");
            r.flush();
            auto nick = askText("");
            if (!nick) { noticeInfo("已取消注册"); continue; }
            r.kv("昵称", *nick);

            r.prompt("所在地（如：广东·深圳，直接回车跳过）：");
            r.flush();
            auto loc = askText("");
            std::string locv = loc ? *loc : "未知";
            r.kv("所在地", locv);

            r.prompt("注册年份（1~2026，直接回车取消）：");
            r.flush();
            auto year = askNum("", 1, 2026);
            if (year < 0) { noticeInfo("已取消注册"); continue; }
            busy("注册处理");
            try {
                auto p = g.registry.registerUser(
                    *qq, *nick, "2000-01-01", locv,
                    static_cast<int>(year));
                // 演示口径：注册即生效 QQ/微博基础服务，微信需绑定后另行开通
                g.activation.activate(*p, PlatformKindFH::QQ);
                g.activation.activate(*p, PlatformKindFH::Weibo);
                g.people.push_back(p);
                g.me = p;
                noticeOK(std::string("注册成功并切换为 ") + *nick + "。");
                return true;
            } catch (const std::exception& e) {
                noticeFail(std::string("注册失败：") + e.what());
                continue;
            }
        }
        if (num == helpKey) {
            showPagedHelp("操作说明", {
                {false, "本工作台按真实 IM 客户端的逻辑组织操作路径："},
                {true, "① 账号层（阶段 B）"},
                {false, "在【账号中心】完成“开通 / 登录”体验：QQ 与微博默认已开通，"
                        "微信需先绑定微信号再手动开通"},
                {true, "② 群目录（阶段 C / D）"},
                {false, "【官方群大厅】浏览 QQ/微信/微博官方群（1001~1006）并加入；"
                        "聊天支持文本/图片/文件/语音/表情，平台差异（微信禁文件、"
                        "微博仅文本表情、文本上限、引用回复）会即时提示"},
                {true, "③ 群管理（阶段 A）"},
                {false, "【创建】可建“本地正式群”（QQ 或微信群策略）体验完整群管理："
                        "发消息、撤回（时间窗 120 秒）、邀请、踢人、禁言、全员禁言、"
                        "任命管理员、公告、改群名、转让群主、切换管理模式、解散群"},
                {false, "也可创建 QQ 临时讨论组（阶段 C）"},
                {true, "④ 会话与社交（阶段 C）"},
                {false, "【我的会话】进入任一已加入会话开始操作；"
                        "【通讯录】管理 QQ/微信双向好友与微博单向关注"},
                {false, "全部操作项均为数字键（0 = 返回上级；正式群会话里"
                        "[8] 为“更多操作”）；界面底部状态条会显示每一步成功或"
                        "失败的原因，每次操作后界面自动重绘"},
            });
            continue;
        }
        if (num >= 1 && num <= hits.size()) {
            g.me = hits[num - 1];
            g.notice = "[提示] 当前账号：" + g.me->getNickname() +
                       "。建议先到【账号中心】确认服务状态。";
            return true;
        }
        noticeFail("无效选择。");
    }
}
}  // namespace fh_client
