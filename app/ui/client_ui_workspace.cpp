#include "client_ui_internal.hpp"

namespace fh_client {

// ============================================================
// 测试指引
// ============================================================

void runHelp() {
    const std::vector<HelpEntry> entries = {
        HelpEntry{true, "体系说明"},
        HelpEntry{false,
                  "群分两套：正式群（聚合根 GroupFH，群号 9000+，走【创建】）"
                  "与官方/自建群（群注册表，群号 1001~1006 / 1007+，走【官方群大厅】）。"},
        HelpEntry{true, "A · 正式群管理（本地正式群）"},
        HelpEntry{false, "创建 QQ 正式群与微信群各一个（成员邀请开关选一次开 / 一次关）"},
        HelpEntry{false, "切换到其他账号把对方邀请进群、任命管理员"},
        HelpEntry{false,
                  "分别用不同身份测试：普通成员邀请（QQ 开关 vs 微信禁止）；"
                  "管理员设全员禁言（QQ 可、微信仅群主）；禁言后普通成员发言失败；"
                  "撤回时间窗；转让群主后原群主仅剩成员权限；切模式成员不丢；解散后一切被拒"},
        HelpEntry{false,
                  "会话内 [0] 直接返回会话列表，[8] 进入“更多操作”：切换管理模式 / "
                  "转让群主 / 解散群 / 退出本群（群主不能直接退群，须先转让或解散）/ "
                  "群设置（改邀请开关、把撤回窗口调小以复现“超时不可撤回”）/ 群公告 / "
                  "改群名 / 以其他成员身份操作"},
        HelpEntry{true, "B · 多产品体系（账号中心）"},
        HelpEntry{false,
                  "先分清三层：有账号（身份）→ 已开通（你自选启用，任务书第 4 点）"
                  "→ 在线（已登录）；三者互相独立"},
        HelpEntry{false,
                  "“绑定了微信号”只说明有账号，不等于已开通微信服务；任务书里的“服务”"
                  "指的是 QQ/微信/微博 这类微X 产品本身，不是第三方应用"},
        HelpEntry{false, "用新注册账号体验：未开通服务登录失败 → 开通 → 登录联动（全部上线）"},
        HelpEntry{false, "在线时取消开通被拒 → 退出后取消成功"},
        HelpEntry{false, "微信：先绑定 → 开通 → 登录；重复开通 / 绑定失败均有提示"},
        HelpEntry{true, "C · 社交（通讯录 / 大厅 / 讨论组）"},
        HelpEntry{false, "QQ/微信双向好友、微博单向关注；删除 QQ 好友不影响微信（平台隔离）"},
        HelpEntry{false,
                  "加入官方群 / 自建官方群；微信群只能推荐加入：会话内 [4] 推荐好友入群"
                  "（推荐者须已在群内，被推荐者须已绑定微信）"},
        HelpEntry{false,
                  "自建群群主可 [6] 转让群主、[5] 解散本群；群主不能直接退群，"
                  "须先转让或解散"},
        HelpEntry{false, "QQ 临时讨论组：成员可互邀、自由退、仅发起人可解散"},
        HelpEntry{true, "D · 消息平台差异（官方群聊天）"},
        HelpEntry{false,
                  "在 QQ/微信/微博各官方群试发：文件、图片、超长文本、引用回复，"
                  "观察允许与否的差异提示与“产品视图”渲染的差异"},
    };
    showPagedHelp("手动测试指引（建议按顺序走查）", entries);
}

// ============================================================
// 主工作台
// ============================================================

// 返回 false 表示退出整个程序
bool runWorkspace() {
    for (;;) {
        int nLocal = 0, nOfficial = 0, nDisc = 0;
        for (const auto& slot : g.locals) {
            const auto u = actorFor(g.me, slot.platform);
            if (u && !slot.group->isDisbanded() && slot.group->contains(u)) ++nLocal;
        }
        for (const GroupInfoFH& gi : g.official.groupsOfUser(*g.me))
            (void)gi, ++nOfficial;
        for (const auto& d : g.discs)
            if (!d->isDisbanded() && d->contains(g.me->getQQId())) ++nDisc;

        fh_ui::Screen s("微X 平台 · 终端客户端（手动测试工作台）");
        drawAccountCard(s);

        s.blank();
        s.section("我的会话概览");
        s.kv("正式群", std::to_string(nLocal) + " 个");
        s.kv("官方/自建群", std::to_string(nOfficial) + " 个");
        s.kv("QQ 讨论组", std::to_string(nDisc) + " 个");

        s.blank();
        s.section("主菜单");
        s.menu({{'1', "我的会话"}, {'2', "官方群大厅"}, {'3', "通讯录·好友"},
                {'4', "账号中心"}, {'5', "创建群 / 讨论组"}, {'6', "切换账号"},
                {'7', "测试指引"}, {'0', "退出"}});
        present(s);
        const char k = waitKey();
        switch (k) {
        case '1': runConversationList(); break;
        case '2': runHall(); break;
        case '3': runContacts(); break;
        case '4': runServiceCenter(); break;
        case '5': runCreateScreen(); break;
        case '6': return true;  // 回账号选择界面
        case '7': runHelp(); break;
        case '0': return false;
        default:
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            break;
        }
    }
}

// ============================================================
// 断电保存（任务书 6.(1)/优化(2)）：系统启动时把开通服务情况、
// 群成员信息和好友信息从文件加载到内存；三类信息各一个存档文件，
// 容器析构（程序退出）时自动写回。
// ============================================================
void loadWorldFromDisk() {
    const bool okAct = g.registry.setActivationPath("save_activation_fh.dat");
    const bool okFri = g.friends.setPersistencePath("save_friends_fh.dat");
    const bool okGrp = g.official.setPersistencePath("save_groups_fh.dat");
    if (okAct || okFri || okGrp)
        noticeInfo("已从存档文件恢复开通/好友/群数据（退出时自动写回）。");
    else
        noticeInfo("未发现存档：本次运行数据将在退出时写入存档文件。");
}

// 预置微信群初始成员注入（演示环境）。
// 库层预置群默认无成员，而微信群「只能推荐加入」且推荐者须已在群内，
// 于是无人可推荐 → 群永久空置（死群）。此处为「空成员」的预置微信群补齐
// 演示成员；仅对空群生效，因此用户手动退群后重启不会被强行加回。
void backfillPredefinedGroups() {
    auto wxIdOf = [](const std::string& nick) -> std::string {
        for (const auto& p : g.people)
            if (p->getNickname() == nick && p->hasWeChatAccount())
                return p->getWeChatId();
        return std::string();
    };
    const std::string xm = wxIdOf("小明");
    const std::string xh = wxIdOf("小红");
    const std::string lb = wxIdOf("路人乙");
    g.official.ensurePredefinedMembers("1003", {xm, xh});  // 家庭群
    g.official.ensurePredefinedMembers("1004", {xh, lb});  // 同事群
}

int runClientUi() {
    initUiConsole();
    seedWorld();
    loadWorldFromDisk();  // 任务书 6.(1)：启动时从文件加载到内存
    // 加载之后再注入预置微信群初始成员：存档载入会整体替换群目录，
    // 故必须在加载后回填，空成员的预置微信群才能真正"活"起来。
    backfillPredefinedGroups();
    for (;;) {
        if (!runAccountGate()) return 0;
        if (!runWorkspace()) return 0;
    }
}
}  // namespace fh_client
