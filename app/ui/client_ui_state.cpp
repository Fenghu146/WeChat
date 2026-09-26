#include "client_ui_internal.hpp"

namespace fh_client {

App g;

void noticeOK(const std::string& msg) { g.notice = "[成功] " + msg; }
void noticeFail(const std::string& msg) { g.notice = "[失败] " + msg; }
void noticeInfo(const std::string& msg) { g.notice = "[提示] " + msg; }

// 统一帧尾：把最近一次操作结果作为状态条挂在菜单上方，写入提示行后整帧重绘。
// 所有界面都走这里，格式与「按任意键返回」类提示保持一致。
void present(fh_ui::Screen& s, const std::string& prompt) {
    if (!g.notice.empty()) s.notice(g.notice);
    g.notice.clear();
    s.prompt(prompt);
    s.flush();
}

// 列表型子提示：单独成一帧（标题栏 + 编号列表 + 提示行）。
// 与主界面共用同一套排版，同样受终端高度约束，不会把内容顶出屏幕。
// 返回 0 起下标；取消 / 无效输入返回 -1。
int chooseByLabels(const std::string& title,
                   const std::vector<std::string>& labels,
                   const std::string& hint) {
    if (labels.empty()) return -1;

    fh_ui::Screen s(title);
    if (!hint.empty()) {
        s.text(hint);
        s.blank();
    }

    if (labels.size() <= 9) {
        std::vector<fh_ui::MenuItem> items;
        items.reserve(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i)
            items.push_back(fh_ui::MenuItem{static_cast<char>('1' + i), labels[i]});
        s.menu(items, 1);  // 候选项较长，保持一行一个
        s.prompt("请按键选择（[0] 取消）：");
        s.flush();
        const char k = waitKey();
        if (k >= '1' && k <= static_cast<char>('0' + labels.size()))
            return static_cast<int>(k - '1');
        if (k != 0 && k != '0') noticeInfo("无效选择，请重新进入后重试。");
        return -1;
    }

    for (std::size_t i = 0; i < labels.size(); ++i)
        s.text("    [" + std::to_string(i + 1) + "] " + labels[i]);
    s.prompt("输入序号（直接回车取消）：");
    s.flush();
    const long r = askNum("> ", 1, static_cast<long>(labels.size()));
    return r < 0 ? -1 : static_cast<int>(r - 1);
}

std::string platCn(PlatformKindFH p) { return toZhName(p); }

const char* zhRole(GroupRoleFH role) { return toZhName(role); }

// 平台账号 -> 用户实体（QQ/微博同号共用同一 UserFH，微信单独）
UserPtr actorFor(const ProfilePtr& p, PlatformKindFH platform) {
    if (!p) return nullptr;
    const std::string id = p->platformAccountId(platform);
    if (id.empty()) return nullptr;
    const std::string key = std::to_string(static_cast<int>(platform)) + "|" + id;
    auto it = g.actorPool.find(key);
    if (it != g.actorPool.end()) return it->second;
    auto u = std::make_shared<UserFH>(id, p->getNickname());
    g.actorPool.emplace(key, u);
    return u;
}

// —— 正式群“操作身份”解析（配合管理模式切换）——
// 任务书 6.(4)：切换管理模式只换绑群策略，成员数据（以入群时的平台账号记录）
// 原样保留。因此解析当前账号的操作身份时，优先取当前管理模式的平台账号；
// 若它不是群成员，而本人的另一平台账号恰在群内（切换前入群），则回退到
// 该成员身份 —— 保证切换后群主/管理员仍可正常操作，而不是被判成“非成员”。
UserPtr localActorFor(const ProfilePtr& p, const LocalSlot& slot) {
    const GroupFH& grp = *slot.group;
    const UserPtr primary = actorFor(p, slot.platform);
    if (primary && grp.contains(primary)) return primary;
    for (const auto pl : {PlatformKindFH::QQ, PlatformKindFH::WeChat}) {
        if (pl == slot.platform) continue;
        const UserPtr u = actorFor(p, pl);
        if (u && grp.contains(u)) return u;
    }
    return primary;  // 无成员身份时保持原口径（无该平台账号 / 非成员）
}

// 由成员实体（任一平台账号）反查自然人档案：切换管理模式后，
// 群内成员可能以“另一平台”的账号入群，按 QQ / 微信两个平台逐一匹配。
ProfilePtr profileOfMember(const UserPtr& u) {
    if (!u) return nullptr;
    for (const auto& q : g.people)
        for (const auto pl : {PlatformKindFH::QQ, PlatformKindFH::WeChat})
            if (q->platformAccountId(pl) == u->getId()) return q;
    return nullptr;
}

// 由“平台账号号码”反查昵称（用于官方群/讨论组中成员展示）
std::string nickOf(PlatformKindFH platform, const std::string& accountId) {
    for (const auto& p : g.people) {
        if (p->platformAccountId(platform) == accountId)
            return p->getNickname() + "(" + accountId + ")";
    }
    return accountId + "(?)";
}

// 预置演示自然人与服务状态
void seedWorld() {
    auto xm = g.registry.registerUser("10001", "小明", "2000-06-01",
                                      "广东·深圳", 2018);
    auto xh = g.registry.registerUser("10002", "小红", "1999-11-11",
                                      "湖南·长沙", 2016);
    auto lb = g.registry.registerUser("10003", "路人乙", "2001-03-03",
                                      "四川·成都", 2020);
    auto lc = g.registry.registerUser("10004", "路人丙", "2002-07-07",
                                      "湖北·武汉", 2021);
    g.registry.bindWeChat(xm, "wx-88-0001");
    g.registry.bindWeChat(xh, "wx-88-0002");
    // 路人乙也绑定微信：作为预置微信群「可被推荐入群」的对象（路人丙保持
    // 未绑定，用于演示「未绑定微信」状态）。
    g.registry.bindWeChat(lb, "wx-88-0003");
    // 说明：演示环境默认已为每个自然人生效 QQ/微博服务（可随时在账号中心取消），
    // 微信需先绑定再手动开通 —— 开通/登录联动规则在“账号中心”验证。
    for (const auto& p : {xm, xh, lb, lc}) {
        g.activation.activate(*p, PlatformKindFH::QQ);
        g.activation.activate(*p, PlatformKindFH::Weibo);
        g.people.push_back(p);
    }
}

// 好友关系标注（选择列表用）：QQ/微信为双向好友，微博为单向关注
std::string friendMark(const ProfilePtr& p, PlatformKindFH pl) {
    if (pl == PlatformKindFH::Weibo)
        return g.friends.isFollowing(*g.me, *p) ? "  ［已关注］" : "  ［未关注］";
    return g.friends.isFriend(*g.me, *p, pl)
               ? ("  ［已是" + platCn(pl) + "好友］")
               : ("  ［非" + platCn(pl) + "好友］");
}

// annotate 非空时，在每项后追加状态标注（如「已是QQ好友」「已在群内」）。
// 同一选择器同时服务「加好友」和「删好友」等相反意图，因此标注状态而非过滤名单：
// 既避免误操作，也保留「重复添加被拒绝」这类规则的可演示性。
// title 作为子界面标题栏（如「选择要邀请的人」）。
ProfilePtr pickProfile(bool excludeSelf, std::optional<PlatformKindFH> needAcct,
                       const std::string& title,
                       const std::function<std::string(const ProfilePtr&)>& annotate) {
    std::vector<std::string> labels;
    std::vector<ProfilePtr> hits;
    for (const auto& p : g.people) {
        if (excludeSelf && p == g.me) continue;
        if (needAcct && !p->hasPlatformAccount(*needAcct)) continue;
        std::string wx = p->hasWeChatAccount()
                             ? (" 微信:" + p->getWeChatId())
                             : " 微信:未绑定";
        labels.push_back(p->getNickname() + "  QQ/微博:" + p->getQQId() + wx +
                         (annotate ? annotate(p) : std::string()));
        hits.push_back(p);
    }
    if (hits.empty()) {
        noticeFail("没有可选的成员（可能缺少该平台的账号）。");
        return nullptr;
    }
    const int idx = chooseByLabels(title, labels);
    if (idx < 0) return nullptr;
    return hits[static_cast<std::size_t>(idx)];
}

void drawAccountCard(fh_ui::Screen& s) {
    const auto& p = g.me;
    s.kv("当前账号", p->getNickname() + "（QQ/微博 " + p->getQQId() + " · " +
                        p->getLocation() + " · T龄 " +
                        std::to_string(p->tAge(g.year)) + " 年）");
    s.kv("微信账号", p->hasWeChatAccount() ? p->getWeChatId() : std::string("未绑定"));

    // 三层维度互相独立：是否有账号(身份) → 是否开通(用户自选) → 是否登录(在线)
    std::string states;
    const char* sep = "";
    for (const auto pl :
         {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo}) {
        std::string state;
        if (!p->hasPlatformAccount(pl)) {
            state = "无账号";
        } else if (!p->isActivated(pl)) {
            state = "有账号·未开通";
        } else {
            state = "已开通";
            if (p->isOnline(pl)) state += "·在线";
        }
        states += sep + platCn(pl) + "[" + state + "]";
        sep = "   ";
    }
    s.kv("服务状态", states);
}
}  // namespace fh_client
