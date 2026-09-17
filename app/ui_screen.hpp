#pragma once

// ============================================================
// 终端界面工具箱（作者代号：FH）
//
// 目标：
//   1) 让每一帧的输出行数与每行显示宽度都受终端可视区约束，
//      从根本上消除「按一下键界面就多滚一段、标题被顶出屏幕」的问题；
//   2) 把标题栏、键值卡片、分节、菜单列等排版能力集中到一处，
//      工作台（client_ui）与自动演示（demo_runner）共用同一套设计语言。
//
// 两类用法：
//   · Screen —— 整帧缓冲：适合交互工作台，flush() 时统一测量、裁剪、重绘；
//   · rule/section/Rows —— 线性日志：适合 --demo 这类顺序输出，不清屏。
// ============================================================

#include <iosfwd>
#include <string>
#include <vector>

namespace fh_ui {

// ------------------------------------------------------------
// 终端尺寸与显示宽度
// ------------------------------------------------------------

struct TermSize {
    int rows = 24;  // 可视行数（Windows 取窗口而非缓冲区）
    int cols = 80;  // 可视列数
};

// 查询当前终端可视尺寸；非终端（管道/重定向）或查询失败时返回 80x24。
TermSize termSize();

// 是否连接到交互终端（决定要不要发控制序列/颜色码）。
bool interactiveOut();

// 按终端列宽计算的显示宽度：中日韩全角 = 2，ASCII = 1，组合符/零宽 = 0。
int displayWidth(const std::string& utf8);

// 右侧补空格到指定显示宽度（已超出则原样返回）。
std::string padRight(const std::string& text, int width);

// 截断到指定显示宽度，超出部分以省略号收尾。
std::string truncateTo(const std::string& text, int width);

// ------------------------------------------------------------
// 颜色：绿色 = 成功、红色加粗 = 失败、灰色 = 次要说明
// NO_COLOR 置位或输出非终端时自动关闭。
// ------------------------------------------------------------

bool colorEnabled();
const char* colorOk();
const char* colorBad();
const char* colorDim();
const char* colorReset();

std::string paint(const std::string& text, const char* code);

// ------------------------------------------------------------
// 线性日志排版（--demo 用；不涉及清屏与光标控制）
// ------------------------------------------------------------

// "======== 标题 ========"
std::string rule(const std::string& title);

// "  —— 小节 ——"
std::string section(const std::string& name);

// 先收集「标签 - 结果 - 备注」，再按本段最大标签宽度对齐输出。
// 用于把演示里逐行左对齐的「标签：结果」整理成对齐的结果列。
class Rows {
public:
    void add(const std::string& label, const std::string& value,
             const std::string& note = std::string());
    void flush(std::ostream& os, int indent = 4) const;
    bool empty() const { return lines_.empty(); }
    void clear() { lines_.clear(); }

private:
    struct Line {
        std::string label;
        std::string value;
        std::string note;
    };
    std::vector<Line> lines_;
};

// ------------------------------------------------------------
// 整帧界面（工作台用）
//
// 一屏的组装顺序固定为：
//     标题栏 → （空行） → 正文若干行 → 状态条 → 提示行
// flush() 负责：
//   · 测量终端可视尺寸；
//   · 每行按列宽截断（长行折行同样会多占一行，会破坏「不滚动」保证）；
//   · 总行数超屏时只裁剪可裁剪的列表项，并在裁剪处提示省略了多少条；
//   · 逐行重绘：回到首行、每行清到行尾、最后一行的换行符不发，
//     因此光标停在末行，输出永远不会越过可视区底行。
// ------------------------------------------------------------

struct MenuItem {
    char key = 0;
    std::string label;
};

class Screen {
public:
    explicit Screen(const std::string& title);

    void blank();
    void text(const std::string& line);
    void kv(const std::string& label, const std::string& value);
    void section(const std::string& name);

    // 列表项。可裁剪：超屏时优先丢弃它们，其余内容保持不变。
    void item(const std::string& text);

    // 数字键菜单。perRow = 每行最多放几项（1 = 每项独占一行，0 = 默认最多 3 项）；
    // 放不下时自动减少列数，短菜单可传更大值让它保持一行。
    void menu(const std::vector<MenuItem>& items, int perRow = 0);

    // 固定在菜单上方的状态条（如 "[成功] 已开通微信服务"），按前缀着色。
    void notice(const std::string& text);

    // 末行提示（如 "请按键选择："），不输出换行符。
    void prompt(const std::string& text);

    // 本帧还可放下多少行列表项（供调用方裁剪长列表）。
    int rowsLeft() const;

    // 上一次 flush() 实际占用的行数（供帧后的行内子提示计算剩余空间）。
    int rowsUsed() const { return rowsUsed_; }

    void flush();

private:
    enum Kind { kText, kKv, kSection, kItem };

    struct Line {
        Kind kind;
        std::string a;
        std::string b;
    };

    std::vector<Line> lines_;
    std::string title_;
    std::string notice_;
    std::string prompt_;
    int rowsUsed_ = 0;
};

}  // namespace fh_ui
