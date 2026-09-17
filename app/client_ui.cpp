// ============================================================
// 手动测试工作台 · 交互实现（作者代号：FH）
// ------------------------------------------------------------
// 设计目标：按“真实 IM 客户端”的操作逻辑完成 A→D 全流程手动
// 测试：
//   账号层(B)  ：预置自然人（QQ/微博同号、微信独立可绑定）、
//               开通 → 登录联动 → 退出，全部可在界面直接验证；
//   会话层(A)  ：本地正式群（QQ/微信群策略）的完整群管理：发消息、
//               撤回、邀请、踢人、禁言、全员禁言、任命/撤销管理员、
//               改群名、公告、转让群主、切换管理模式、解散群；
//   群目录(C/D)：官方群大厅（1001~1006）入群/退群/发不同类型消息
//               （类型、长度、引用按平台校验），微信禁文件、微博
//               仅文本/表情等差异可直接体验；
//   讨论组(C)  ：QQ 临时讨论组（任何成员可邀请、成员自由退、仅发
//               起人可解散）；
//   通讯录(C)  ：QQ/微信双向好友 + 微博单向关注，按平台隔离。
// 交互约定：
//   - 所有“按钮”均为单键热键：直接按键即触发，无需回车；
//   - 需要输入文本/数字时按界面提示输入，直接回车可取消；
//   - 每次操作后自动刷新数据展示区，并在提示条给出成功/失败原因；
//   - 无效输入不会崩溃，均给出“操作失败/无效按键”类提示。
// ============================================================
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "im/message/message_kind_fh.hpp"
#include "im/message/platform_message_policy_fh.hpp"
#include "im/model/group_config_fh.hpp"
#include "im/model/group_fh.hpp"
#include "im/model/group_membership_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/platform/activation_manager_fh.hpp"
#include "im/platform/login_manager_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"
#include "im/social/discussion_group_fh.hpp"
#include "im/social/friend_registry_fh.hpp"
#include "im/social/group_registry_fh.hpp"

#include "client_ui.hpp"
#include "ui_screen.hpp"

namespace {

using ProfilePtr = std::shared_ptr<UserProfileFH>;
using UserPtr = std::shared_ptr<UserFH>;

// ============================================================
// 基础 UI 组件：整帧重绘 / 单键输入 / 状态提示条
//
// 重绘统一交给 fh_ui::Screen：它按终端可视高度裁剪、逐行覆盖写，
// 因此任何界面、任何窗口尺寸下，按任意键都不会再把标题挤出屏幕。
// ============================================================

void initUiConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

std::string trimCopy(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

#ifndef _WIN32
// POSIX：把终端临时切到「非规范模式 + 关闭回显」，实现真正的单键读取。
// 默认规范模式下终端要等到回车才把字节交给程序（并回显），这正是
// “按了数字没反应、要再按回车”的原因。仅清除 ICANON/ECHO，保留 ISIG，
// 因此 Ctrl+C 仍然有效；非终端（管道/重定向）不做任何改动；析构时恢复。
class RawTerminalGuard {
public:
    RawTerminalGuard() {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        struct termios raw = saved_;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) active_ = true;
    }
    ~RawTerminalGuard() {
        if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    }
    RawTerminalGuard(const RawTerminalGuard&) = delete;
    RawTerminalGuard& operator=(const RawTerminalGuard&) = delete;

private:
    struct termios saved_{};
    bool active_ = false;
};
#endif

// 单键输入：直接返回用户按下的按键（小写）；方向键/回车/Esc 返回 0（忽略）
char waitKey() {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (hIn == INVALID_HANDLE_VALUE || !GetConsoleMode(hIn, &mode)) {
        // 非交互（管道/重定向/自动化冒烟）时退回标准输入逐字节读取
        char c = 0;
        if (std::fread(&c, 1, 1, stdin) != 1) std::exit(0);  // 输入已结束：直接退出
        if (c == '\r' || c == '\n' || c == 27) return 0;
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    int c = _getch();
    if (c == 0 || c == 224) {  // 功能键前缀：丢弃第二字节
        _getch();
        return 0;
    }
    if (c == '\r' || c == '\n' || c == 27) return 0;
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#else
    RawTerminalGuard guard;  // 终端下无需回车；管道/重定向下不做改动
    char c = 0;
    if (std::fread(&c, 1, 1, stdin) != 1) std::exit(0);  // 输入已结束：直接退出
    if (c == '\r' || c == '\n' || c == 27) return 0;
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
}

// 文本表单输入：直接回车 = 取消（返回 nullopt）
std::optional<std::string> askText(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    if (!std::cin) {
        std::cin.clear();
        return std::nullopt;
    }
    line = trimCopy(line);
    if (line.empty()) return std::nullopt;
    return line;
}

// 数值输入：直接回车 = 取消（返回 -1）
long askNum(const std::string& prompt, long lo, long hi) {
    for (;;) {
        std::cout << prompt;
        std::string line;
        std::getline(std::cin, line);
        if (!std::cin) {
            std::cin.clear();
            return -1;
        }
        line = trimCopy(line);
        if (line.empty()) return -1;
        try {
            long v = std::stol(line);
            if (v >= lo && v <= hi) return v;
        } catch (...) {
        }
        std::cout << "  [提示] 请输入 " << lo << "~" << hi
                  << " 的整数（直接回车取消）。\n";
    }
}

// 长帮助页：按终端高度分页，逐页显示，避免一屏放不下时静默丢内容。
struct HelpEntry {
    bool isSection;
    std::string text;
};

void showPagedHelp(const std::string& title, const std::vector<HelpEntry>& entries) {
    const fh_ui::TermSize ts = fh_ui::termSize();
    const int perPage = std::max(3, ts.rows - 4);  // 预留标题栏、状态条与提示行

    // 估算每条占几行（item 会在过宽时自动折行），据此切页
    std::vector<int> rows;
    rows.reserve(entries.size());
    int totalRows = 0;
    for (const auto& e : entries) {
        const std::string line =
            e.isSection ? fh_ui::section(e.text) : ("    · " + e.text);
        const int width = fh_ui::displayWidth(line);
        const int span = std::max(1, ts.cols - (e.isSection ? 2 : 6));
        const int n = width <= ts.cols ? 1 : 1 + (width - ts.cols + span - 1) / span;
        rows.push_back(n);
        totalRows += n;
    }
    const int pages = std::max(1, (totalRows + perPage - 1) / perPage);

    std::size_t i = 0;
    for (int page = 1; page <= pages && i < entries.size(); ++page) {
        fh_ui::Screen s(title);
        int used = 0;
        for (; i < entries.size(); ++i) {
            if (used > 0 && used + rows[i] > perPage) break;
            if (entries[i].isSection)
                s.section(entries[i].text);
            else
                s.item(entries[i].text);
            used += rows[i];
        }
        s.prompt(pages > 1 ? ("第 " + std::to_string(page) + "/" +
                              std::to_string(pages) + " 页 —— 按任意键" +
                              (page < pages ? "继续：" : "返回："))
                           : std::string("按任意键返回："));
        s.flush();
        waitKey();
    }
}

// 操作过程反馈。
// 说明：本项目为单机内存模型，操作同步瞬时完成。此处原有一段“处理中”
// 动画，但每次操作固定 sleep 5×45ms ≈ 225ms，会让每一次操作都明显变慢。
// 现默认瞬时返回；如需观察提交过程，可用环境变量 FH_UI_ANIM=1 开启动画。
void busy(const std::string& what) {
    static const bool animate = [] {
        const char* v = std::getenv("FH_UI_ANIM");
        return v != nullptr && *v != '\0' && !(v[0] == '0' && v[1] == '\0');
    }();
    if (!animate) return;

    static const char spin[] = {'|', '/', '-', '\\'};
    std::cout << "  " << what << " 处理中 ";
    std::cout.flush();
    for (int i = 0; i < 5; ++i) {
        std::cout << spin[i % 4];
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(45));
        std::cout << '\b';
    }
    std::cout << "完成\n";
}

std::string fmtClock(std::chrono::system_clock::time_point tp,
                     bool withDate = false) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), withDate ? "%m-%d %H:%M:%S" : "%H:%M:%S",
                  &local);
    return buf;
}

// ============================================================
// 会话数据世界：一次启动内数据持久，可反复测试
// ============================================================

struct LocalSlot {
    PlatformKindFH platform = PlatformKindFH::QQ;  // 当前管理模式（策略）
    std::shared_ptr<GroupFH> group;
};

struct App {
    UserRegistryFH registry;
    std::vector<ProfilePtr> people;          // 全部自然人（含新注册）
    ActivationManagerFH activation;
    LoginManagerFH login;
    FriendRegistryFH friends;
    GroupRegistryFH official;                // 官方群目录 + 自建官方群
    std::vector<LocalSlot> locals;           // 本地正式群（聚合根，阶段 A）
    std::vector<std::shared_ptr<DiscussionGroupFH>> discs;  // QQ 临时讨论组
    std::unordered_map<std::string, UserPtr> actorPool;     // 平台账号 -> UserFH

    ProfilePtr me;            // 当前操作的自然人
    std::string notice;       // 底部提示条（最近一次操作结果）
    unsigned msgSeq = 0;      // 本地正式群消息自增号
    int localSeq = 0;         // 本地正式群自增号
    int year = 2026;          // 演示用当前年份（计算 T 龄）
};
App g;

void noticeOK(const std::string& msg) { g.notice = "[成功] " + msg; }
void noticeFail(const std::string& msg) { g.notice = "[失败] " + msg; }
void noticeInfo(const std::string& msg) { g.notice = "[提示] " + msg; }

// 统一帧尾：把最近一次操作结果作为状态条挂在菜单上方，写入提示行后整帧重绘。
// 所有界面都走这里，格式与「按任意键返回」类提示保持一致。
void present(fh_ui::Screen& s, const std::string& prompt = "请按键选择：") {
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
                   const std::string& hint = std::string()) {
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
                       const std::function<std::string(const ProfilePtr&)>& annotate = {}) {
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

// —— 正式群“更多操作”二级菜单（全部数字键：0 返回会话，8 返回会话列表）——
// 返回 false 表示退出本会话（回到会话列表）
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
                {'6', "以其他成员身份操作"},
                {'7', "本页操作帮助"},
                {'8', "返回会话列表"},
                {'0', "返回会话"}});
        present(s);
        const char k = waitKey();

        if (k == '0') return true;
        if (k == '8') return false;
        if (k == '7') {
            fh_ui::Screen h("更多操作 · 说明（阶段 A / 平台差异）");
            h.section("各操作的含义");
            h.kv("切换管理模式",
                 "群成员与消息数据原样保留，仅换绑群策略（任务书 6.(4)：动态变换"
                 "管理特色，数据不受伤害）");
            h.kv("转让群主", "仅群主可操作，原群主降为普通成员（角色属于群成员关系）");
            h.kv("解散群", "仅群主可操作；解散后成员清空、所有操作被拒绝");
            h.kv("退出本群", "普通成员/管理员主动退群；群主须先转让或解散");
            h.kv("群设置", "邀请开关（QQ 概念）与撤回时间窗，需管理员及以上身份");
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
        case '6':
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

        // —— 操作按钮区：数字键为功能键，0 进入“更多操作” ——
        s.blank();
        s.section("可用操作");
        if (!meUser) {
            s.text("  提示：当前管理模式为「" + platCn(live.platform) +
                   "」，但你没有该平台账号，仅可浏览本群。");
            s.menu({{'0', "更多操作（含返回会话列表）"}});
        } else {
            s.menu({{'1', "发送消息"}, {'2', "撤回消息"}, {'3', "邀请成员"},
                    {'4', "踢出成员"}, {'5', "禁言/解禁"}, {'6', "全员禁言"},
                    {'7', "任命管理员"}, {'8', "群公告"}, {'9', "改群名"},
                    {'0', "更多操作"}},
                   5);  // 每行 5 项，与原版一致
        }
        present(s);
        const char k = waitKey();
        if (!meUser) {  // 无账号时只保留“更多操作”（其中含返回会话列表）
            if (k == '0') {
                if (!runLocalMore(live)) return;
            } else if (k != 0) {
                noticeFail("无效按键：" + std::string(1, k));
            }
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
        case '8': {  // 发布 / 更新公告
            auto text = askText("  新群公告内容（直接回车取消）> ");
            if (!text) {
                noticeInfo("已取消发布公告");
                continue;
            }
            busy("公告发布");
            if (g.publishAnnouncement(meUser, *text))
                noticeOK("群公告已更新。");
            else
                noticeFail("发布公告需要管理员及以上身份（或群已解散）。");
            continue;
        }
        case '9': {  // 修改群名
            auto name = askText("  新群名（直接回车取消）> ");
            if (!name) {
                noticeInfo("已取消改名");
                continue;
            }
            busy("群名修改");
            if (g.editGroup(meUser, *name))
                noticeOK("群名已改为「" + *name + "」。");
            else
                noticeFail("修改群名需要管理员及以上身份（或群已解散）。");
            continue;
        }
        case '0': {  // 更多操作：切换管理模式 / 转让 / 解散 / 退群 / 群设置 / 切换身份
            if (!runLocalMore(live)) return;
            continue;
        }
        default:
            if (k != 0) noticeFail("无效按键：" + std::string(1, k));
            continue;
        }
    }
}

// ============================================================
// 阶段 C/D：官方群 / 自建群（群注册表）会话窗口
// ============================================================

const GroupInfoFH* findOfficial(const std::string& groupId) {
    return g.official.findGroup(groupId);
}

// 官方群发送失败原因：与 PlatformMessagePolicyFH 规则一致
std::string officialSendReason(const GroupInfoFH* info,
                               const ProfilePtr& op, MessageKindFH kind,
                               const std::string& content, bool asReply) {
    const auto pl = info->platform;
    const auto senderId = op->platformAccountId(pl);
    if (senderId.empty())
        return "你没有该平台的账号，无法在" + platCn(pl) + "群发言";
    bool isMember = false;
    for (const auto& id : info->memberIds)
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

void runOfficialChat(const std::string& groupId) {
    for (;;) {
        const GroupInfoFH* info = findOfficial(groupId);
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
        s.menu({{'1', "发送消息"}, {'2', "加入本群"}, {'3', "退出本群"},
                {'0', "返回"}},
               4);  // 保持一行，与原版一致
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
            s.prompt("输入" + kindName + "消息" + what + "内容（直接回车取消）：");
            s.flush();
            auto text = askText("");
            if (!text) {
                noticeInfo("已取消发送");
                continue;
            }
            bool wantReply = false;
            if (kind == MessageKindFH::TEXT) {
                s.prompt("作为引用回复发送？[y]是 / [n]否：");
                s.flush();
                wantReply = waitKey() == 'y';
            }
            busy("消息发送");
            const std::string reason =
                officialSendReason(info, g.me, kind, *text, wantReply);
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
            if (g.official.leaveGroup(*g.me, groupId))
                noticeOK("已退出「" + info->name + "」。");
            else
                noticeFail("退群失败：可能你并不在该群。");
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
    for (const GroupInfoFH* gi : g.official.groupsOfUser(*g.me)) {
        items.push_back(Item{ConvKind::Official, gi->groupId,
                             "官方群 · " + platCn(gi->platform) + " · " +
                                 gi->name + "（群号 " + gi->groupId + "）"});
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
        const std::string nid = list.back()->groupId;
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
        std::vector<const GroupInfoFH*> hits;
        for (const auto pl :
             {PlatformKindFH::QQ, PlatformKindFH::WeChat, PlatformKindFH::Weibo}) {
            for (const GroupInfoFH* gi : groups.groupsOfPlatform(pl)) {
                const std::string myId = g.me->platformAccountId(pl);
                bool joined = false;
                if (!myId.empty())
                    for (const auto& id : gi->memberIds)
                        if (id == myId) joined = true;
                const std::string label =
                    "[" + platCn(pl) + " 群号 " + gi->groupId + "] " + gi->name +
                    (gi->predefined ? "（官方）" : "（自建）") + " 成员 " +
                    std::to_string(gi->memberIds.size()) + "/" +
                    std::to_string(gi->maxMembers) +
                    (joined ? "  ［我已加入］" : "");
                labels.push_back(label);
                groupItems.push_back(fh_ui::MenuItem{
                    static_cast<char>('1' + static_cast<int>(labels.size()) - 1), label});
                hits.push_back(gi);
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
                runOfficialChat(hits[static_cast<std::size_t>(k - '1')]->groupId);
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
        runOfficialChat(hits[static_cast<std::size_t>(sel - 1)]->groupId);
    }
}

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
            s.prompt("输入要绑定的微信号（直接回车取消）：");
            s.flush();
            auto wid = askText("");
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
                s.prompt("输入备注名（直接回车取消）：");
                s.flush();
                const auto remark = askText("");
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
            busy("推荐计算");
            const auto rec = g.friends.recommendFriendsFrom(
                *g.me, g.registry, from, to);
            if (rec.empty()) {
                // 无候选时逐条回显前置条件与当前状态，避免用户以为功能损坏
                const std::size_t fromFriendCount =
                    g.friends.friendIds(*g.me, from).size();
                std::size_t withTarget = 0;  // 候选：已绑定目标平台账号的人
                std::size_t notYetTarget = 0;  // 其中尚非目标平台好友的人
                for (const auto& p : g.people) {
                    if (p == g.me || !p->hasPlatformAccount(to)) continue;
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
                r.item("③ 对方已绑定" + toCn + "账号：候选 " +
                       std::to_string(withTarget) + " 人");
                r.item("④ 对方尚不是你的" + toCn + "好友：其中 " +
                       std::to_string(notYetTarget) + " 人满足");
                r.blank();
                r.text("  建议顺序：账号中心开通" + toCn + " → 通讯录加 " + fromCn +
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
                {false, "全部操作项均为数字键（0 = 返回上级 / 更多操作）；"
                        "界面底部状态条会显示每一步成功或失败的原因，"
                        "每次操作后界面自动重绘"},
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
                  "会话内按 [0] 进入“更多操作”：切换管理模式 / 转让群主 / 解散群 / "
                  "退出本群（群主不能直接退群，须先转让或解散）/ 群设置（改邀请开关、"
                  "把撤回窗口调小以复现“超时不可撤回”）/ 以其他成员身份操作"},
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
        HelpEntry{false, "加入官方群 / 自建官方群；未绑微信者不能入微信群"},
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
        for (const GroupInfoFH* gi : g.official.groupsOfUser(*g.me))
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

}  // namespace

namespace fh_client {

int runClientUi() {
    initUiConsole();
    seedWorld();
    loadWorldFromDisk();  // 任务书 6.(1)：启动时从文件加载到内存
    for (;;) {
        if (!runAccountGate()) return 0;
        if (!runWorkspace()) return 0;
    }
}

}  // namespace fh_client
