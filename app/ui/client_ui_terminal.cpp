#include "client_ui_internal.hpp"

namespace fh_client {

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

std::string fmtClock(std::chrono::system_clock::time_point tp, bool withDate) {
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
}  // namespace fh_client
