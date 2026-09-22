#include "ui_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace fh_ui {

namespace {

// ------------------------------------------------------------
// UTF-8 解码与 East Asian Width
// ------------------------------------------------------------

// 解出一个码点并前进下标；遇到非法字节序列时按单字节前进并返回 -1。
int utf8Decode(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    int follow = 0;
    int cp = 0;
    if ((c & 0xE0) == 0xC0) {
        follow = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        follow = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        follow = 3;
        cp = c & 0x07;
    } else {
        ++i;
        return -1;
    }
    if (i + static_cast<std::size_t>(follow) >= s.size()) {
        ++i;
        return -1;
    }
    for (int k = 1; k <= follow; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return -1;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += static_cast<std::size_t>(follow) + 1;
    return cp;
}

bool isZeroWidth(int cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

bool isWide(int cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A ||
           (cp >= 0x2460 && cp <= 0x24FF) || (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x1F300 && cp <= 0x1F64F) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
           (cp >= 0x20000 && cp <= 0x2FFFD) || (cp >= 0x30000 && cp <= 0x3FFFD);
}

// East Asian「歧义宽度」字符：在中文字体环境下通常按全角渲染（2 列），
// 这里一律按 2 列计算 —— 高估只会让折行略早，绝不会让行超出终端而折行溢出。
bool isAmbiguousWide(int cp) {
    return cp == 0x00B7 ||                       // · 间隔号
           (cp >= 0x2010 && cp <= 0x2016) ||      // ‐-‒–—-‖（含中文破折号 ——）
           (cp >= 0x2018 && cp <= 0x2019) ||      // ‘’ 单引号
           (cp >= 0x201C && cp <= 0x201D) ||      // “” 双引号
           (cp >= 0x2020 && cp <= 0x2027) ||      // †‡•…‧ 等（含省略号 …）
           cp == 0x2030 || cp == 0x2032 || cp == 0x2033 || cp == 0x2035 ||
           cp == 0x203B || cp == 0x203E ||
           (cp >= 0x2160 && cp <= 0x2179) ||      // 罗马数字
           (cp >= 0x2190 && cp <= 0x2199) ||      // ←↑→↓ 等箭头
           cp == 0x2260 || cp == 0x2264 || cp == 0x2265 ||  // ≠ ≤ ≥
           (cp >= 0x25A0 && cp <= 0x25FF) ||      // ■□▲△ 等几何图形
           cp == 0x2605 || cp == 0x2606 ||        // ★☆
           cp == 0x2714 || cp == 0x2718;          // ✔ ✘
}

int charWidth(int cp) {
    if (cp < 0) return 1;
    if (isZeroWidth(cp)) return 0;
    if (isWide(cp) || isAmbiguousWide(cp)) return 2;
    return 1;
}

// 按显示宽度折行（中文可任意断行；续行加缩进）。返回至少一行。
std::vector<std::string> wrapLine(const std::string& text, int width,
                                  const std::string& contIndent) {
    std::vector<std::string> out;
    if (width <= 0 || displayWidth(text) <= width) {
        out.push_back(text);
        return out;
    }
    const int indentWidth = displayWidth(contIndent);
    std::string cur;
    int curW = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        const int cp = utf8Decode(text, i);
        const int cw = charWidth(cp);
        if (curW + cw > width && !cur.empty()) {
            out.push_back(cur);
            cur = contIndent;
            curW = indentWidth;
        }
        cur.append(text, start, i - start);
        curW += cw;
    }
    out.push_back(cur);
    return out;
}

// ------------------------------------------------------------
// 输出能力检测
// ------------------------------------------------------------

#ifdef _WIN32
// Windows 10+ 需显式打开 VT 序列处理，ESC 控制序列才会被解释。
bool vtReady() {
    static const bool ready = [] {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return false;
        return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }();
    return ready;
}
#else
bool vtReady() { return true; }
#endif

bool colorForced() {
    const char* v = std::getenv("FH_DEMO_COLOR");
    return v != nullptr && v[0] == '1' && v[1] == '\0';
}

}  // namespace

// ------------------------------------------------------------
// 终端尺寸与显示宽度
// ------------------------------------------------------------

TermSize termSize() {
    TermSize s;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
        s.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        s.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        s.cols = ws.ws_col;
        s.rows = ws.ws_row;
    }
#endif
    if (s.cols < 20) s.cols = 20;
    if (s.rows < 6) s.rows = 6;
    return s;
}

bool interactiveOut() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

int displayWidth(const std::string& utf8) {
    int w = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const int cp = utf8Decode(utf8, i);
        w += charWidth(cp);
    }
    return w;
}

std::string padRight(const std::string& text, int width) {
    const int w = displayWidth(text);
    if (w >= width) return text;
    return text + std::string(static_cast<std::size_t>(width - w), ' ');
}

std::string truncateTo(const std::string& text, int width) {
    if (width <= 0) return {};
    if (displayWidth(text) <= width) return text;
    // 省略号按 2 列预留（部分终端把 "…" 当全角渲染），避免截断后又多占一列折行。
    const int budget = width > 3 ? width - 2 : width;
    std::string out;
    int w = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        const int cp = utf8Decode(text, i);
        const int cw = charWidth(cp);
        if (w + cw > budget) {
            i = start;
            break;
        }
        out.append(text, start, i - start);
        w += cw;
    }
    if (width - w >= 1) out += "…";
    return out;
}

// ------------------------------------------------------------
// 颜色
// ------------------------------------------------------------

bool colorEnabled() {
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return colorForced() || interactiveOut();
}

const char* colorOk() { return "\x1b[32m"; }
const char* colorBad() { return "\x1b[1;31m"; }
const char* colorDim() { return "\x1b[2m"; }
const char* colorReset() { return "\x1b[0m"; }

std::string paint(const std::string& text, const char* code) {
    if (!colorEnabled()) return text;
    return std::string(code) + text + colorReset();
}

// ------------------------------------------------------------
// 线性日志排版
// ------------------------------------------------------------

std::string rule(const std::string& title) {
    return "======== " + title + " ========";
}

std::string section(const std::string& name) {
    return "  —— " + name + " ——";
}

void Rows::add(const std::string& label, const std::string& value,
               const std::string& note) {
    lines_.push_back(Line{label, value, note});
}

void Rows::flush(std::ostream& os, int indent) const {
    int labelWidth = 0;
    for (const auto& l : lines_) labelWidth = std::max(labelWidth, displayWidth(l.label));
    const std::string pad(static_cast<std::size_t>(indent > 0 ? indent : 0), ' ');
    for (const auto& l : lines_) {
        os << pad << padRight(l.label, labelWidth) << "  " << l.value;
        if (!l.note.empty()) os << "  " << l.note;
        os << '\n';
    }
}

// ------------------------------------------------------------
// 整帧界面
// ------------------------------------------------------------

Screen::Screen(const std::string& title) : title_(title) {}

void Screen::blank() { lines_.push_back(Line{kText, std::string(), std::string()}); }

void Screen::text(const std::string& line) { lines_.push_back(Line{kText, line, std::string()}); }

void Screen::kv(const std::string& label, const std::string& value) {
    lines_.push_back(Line{kKv, label, value});
}

void Screen::section(const std::string& name) { lines_.push_back(Line{kSection, name, std::string()}); }

void Screen::item(const std::string& text) { lines_.push_back(Line{kItem, text, std::string()}); }

void Screen::menu(const std::vector<MenuItem>& items, int perRow) {
    if (items.empty()) return;

    std::vector<std::string> cells;
    cells.reserve(items.size());
    for (const auto& it : items)
        cells.push_back("[" + std::string(1, it.key) + "] " + it.label);

    const TermSize ts = termSize();
    const int indent = 4;
    const int gap = 4;

    // 列数：从 perRow（默认 3）逐级回退，取第一个能放下的列数。
    int cols = perRow > 0 ? perRow : 3;
    {
        const std::size_t n = cells.size();
        for (int cand = cols; cand >= 1; --cand) {
            int need = indent;
            for (int c = 0; c < cand && static_cast<std::size_t>(c) < n; ++c) {
                int colWidth = 0;
                for (std::size_t i = static_cast<std::size_t>(c); i < n;
                     i += static_cast<std::size_t>(cand))
                    colWidth = std::max(colWidth, displayWidth(cells[i]));
                need += colWidth + gap;
            }
            if (need - gap <= ts.cols) {
                cols = cand;
                break;
            }
            cols = 1;
        }
    }

    const std::size_t rows =
        (cells.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols);
    for (std::size_t r = 0; r < rows; ++r) {
        std::string line(static_cast<std::size_t>(indent), ' ');
        for (int c = 0; c < cols; ++c) {
            const std::size_t idx = r * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c);
            if (idx >= cells.size()) break;
            int colWidth = 0;
            for (std::size_t i = static_cast<std::size_t>(c); i < cells.size();
                 i += static_cast<std::size_t>(cols))
                colWidth = std::max(colWidth, displayWidth(cells[i]));
            line += padRight(cells[idx], colWidth);
            // 本行后面还有内容才补列间距，行尾不留多余空格
            bool hasNext = false;
            for (int c2 = c + 1; c2 < cols; ++c2) {
                const std::size_t n2 =
                    r * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c2);
                if (n2 < cells.size()) hasNext = true;
            }
            if (hasNext) line += std::string(static_cast<std::size_t>(gap), ' ');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();  // 行尾不留补齐空格
        text(line);
    }
}

void Screen::notice(const std::string& text) { notice_ = text; }

void Screen::prompt(const std::string& text) { prompt_ = text; }

int Screen::rowsLeft() const {
    const TermSize ts = termSize();
    const int used = 2 + static_cast<int>(lines_.size()) + (notice_.empty() ? 0 : 1) +
                     (prompt_.empty() ? 0 : 1);
    return std::max(0, (ts.rows - 1) - used - 1);  // 与 flush() 保持同样的安全余量
}

void Screen::flush() {
    const TermSize ts = termSize();
    // 留出最后一列与最后一行：个别字符宽度或终端换行语义与估算有出入时，
    // 这点余量可以吸收掉误差，保证整帧永远不会越过可视区底行（不滚动）。
    const int cols = std::max(20, ts.cols - 1);
    const int rows = std::max(4, ts.rows - 1);

    struct Out {
        std::string text;
        // 超屏裁剪优先级：0 = 永不裁剪；数值小的先丢（空行 → 列表项 → 正文）。
        int dropRank = 0;
        const char* color = nullptr;
    };

    std::vector<Out> out;
    // 折行而不是截断：帮助类长文本不会丢字，多出来的行由下面的裁剪逻辑统一处理。
    auto emit = [&](const std::string& text, int dropRank, const char* color,
                    const std::string& contIndent) {
        for (const auto& piece : wrapLine(text, cols, contIndent))
            out.push_back(Out{piece, dropRank, color});
    };

    emit(rule(title_), 0, nullptr, "  ");
    emit(std::string(), 1, nullptr, "  ");

    int kvWidth = 0;
    for (const auto& l : lines_)
        if (l.kind == kKv) kvWidth = std::max(kvWidth, displayWidth(l.a));

    for (const auto& l : lines_) {
        switch (l.kind) {
        case kText:
            emit(l.a, l.a.empty() ? 1 : 3, nullptr, "  ");
            break;
        case kKv: {
            const std::string label = "  " + padRight(l.a, kvWidth) + "  ";
            emit(label + l.b, 3, nullptr,
                 std::string(static_cast<std::size_t>(displayWidth(label)), ' '));
            break;
        }
        case kSection:
            emit(fh_ui::section(l.a), 3, nullptr, "  ");
            break;
        case kItem:
            emit("    · " + l.a, 2, nullptr, "      ");
            break;
        }
    }

    if (!notice_.empty()) {
        const char* color = nullptr;
        if (notice_.rfind("[成功]", 0) == 0)
            color = colorOk();
        else if (notice_.rfind("[失败]", 0) == 0)
            color = colorBad();
        else if (notice_.rfind("[提示]", 0) == 0)
            color = colorDim();
        emit("  " + notice_, 0, color, "  ");
    }
    if (!prompt_.empty()) emit("  " + prompt_, 0, nullptr, "  ");

    // 超屏裁剪：按优先级丢行（空行 → 列表项 → 正文），标题栏与尾部菜单/提示永不裁剪。
    int omitted = 0;
    for (int rank = 1; rank <= 3 && static_cast<int>(out.size()) > rows; ++rank) {
        for (int i = static_cast<int>(out.size()) - 1;
             i >= 2 && static_cast<int>(out.size()) > rows; --i) {
            if (out[static_cast<std::size_t>(i)].dropRank == rank) {
                out.erase(out.begin() + i);
                ++omitted;
            }
        }
    }
    // 极端窄窗口：仍有剩余就继续从正文首行丢
    while (static_cast<int>(out.size()) > rows && static_cast<int>(out.size()) > 3) {
        out.erase(out.begin() + 2);
        ++omitted;
    }
    if (omitted > 0 && static_cast<int>(out.size()) < rows) {
        out.insert(out.begin() + 2,
                   Out{"    …… 已省略 " + std::to_string(omitted) + " 行（窗口高度不足）", 2,
                       colorDim()});
    }

    for (auto& o : out) o.text = truncateTo(o.text, cols);
    rowsUsed_ = static_cast<int>(out.size());

    const bool color = colorEnabled();

    // 非终端（管道/重定向）：退化为普通逐行输出，不产生任何控制序列。
    if (!interactiveOut()) {
        for (const auto& o : out) {
            if (color && o.color != nullptr) std::cout << o.color;
            std::cout << o.text;
            if (color && o.color != nullptr) std::cout << colorReset();
            std::cout << '\n';
        }
        std::cout.flush();
        return;
    }

#ifdef _WIN32
    // 未开启 VT 的老终端：用控制台 API 逐行定位覆盖，按显示宽度补空格清除残留。
    if (!vtReady()) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
            const SHORT top = csbi.srWindow.Top;
            const int span = ts.cols > 1 ? ts.cols - 1 : ts.cols;
            DWORD written = 0;
            for (int r = 0; r < ts.rows; ++r) {
                SetConsoleCursorPosition(h, COORD{0, static_cast<SHORT>(top + r)});
                const std::size_t idx = static_cast<std::size_t>(r);
                const std::string text = idx < out.size() ? out[idx].text : std::string();
                const std::string spaces(
                    static_cast<std::size_t>(std::max(0, span - displayWidth(text))), ' ');
                WriteConsoleA(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
                WriteConsoleA(h, spaces.data(), static_cast<DWORD>(spaces.size()), &written,
                              nullptr);
            }
            const int lastRow = std::min(ts.rows, static_cast<int>(out.size())) - 1;
            if (lastRow >= 0) {
                const int col =
                    std::min(span, displayWidth(out[static_cast<std::size_t>(lastRow)].text));
                SetConsoleCursorPosition(
                    h, COORD{static_cast<SHORT>(col), static_cast<SHORT>(top + lastRow)});
            }
            return;
        }
    }
#endif

    // 交互终端：回到首行重绘；每行清到行尾，末行不发换行符，
    // 因此光标始终停在可视区之内，永远不会把内容顶出屏幕。
    std::cout << "\x1b[H";
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (color && out[i].color != nullptr) std::cout << out[i].color;
        std::cout << out[i].text;
        if (color && out[i].color != nullptr) std::cout << colorReset();
        std::cout << "\x1b[K";
        if (i + 1 < out.size()) std::cout << "\r\n";
    }
    std::cout << "\x1b[J";
    std::cout.flush();
}

}  // namespace fh_ui
