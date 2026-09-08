#pragma once
// ============================================================
// persist_util_fh.hpp —— 演示级文本持久化小工具（作者代号：FH）
// ------------------------------------------------------------
// 任务书“功能展示要求(1)”与“优化提高(2)”：开通服务情况、群成
// 员信息和好友信息可保存在文件中，容器实例化（配置）时读入、析
// 构时写回，以实现断电保存。本头文件提供各容器共用的行式文本工具：
//   - kFieldSepFH：字段分隔符（ASCII 单元分隔符 0x1F，避免与
//     昵称/群名/消息内容中的常规字符冲突）；
//   - splitFieldsFH：按分隔符把一行切分为字段；
//   - escapeTextFH / unescapeTextFH：自由文本（昵称/群名/消息内
//     容/备注）的最小转义（反斜杠、换行、分隔符）；
//   - 平台枚举 ↔ 文本互转。
// ============================================================
#include <string>
#include <vector>

#include "im/platform/platform_kind_fh.hpp"

namespace persist_util_fh {

inline constexpr char kFieldSepFH = '\x1F';

// 按字段分隔符切分一行（保留空字段）
inline std::vector<std::string> splitFieldsFH(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char ch : line) {
        if (ch == kFieldSepFH) {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    fields.push_back(cur);
    return fields;
}

// 自由文本转义：\ → \\，换行 → \n，分隔符 → \s（\r 归一化丢弃）
inline std::string escapeTextFH(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            default:
                if (ch == kFieldSepFH) out += "\\s";
                else out.push_back(ch);
                break;
        }
    }
    return out;
}

// escapeTextFH 的逆操作
inline std::string unescapeTextFH(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char nxt = s[++i];
            switch (nxt) {
                case '\\': out.push_back('\\'); break;
                case 'n': out.push_back('\n'); break;
                case 's': out.push_back(kFieldSepFH); break;
                default: out.push_back(nxt); break;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// 平台枚举 ↔ 稳定文本名（持久化格式用英文，避免编码问题）
inline std::string platformName(PlatformKindFH p) {
    switch (p) {
        case PlatformKindFH::QQ:     return "QQ";
        case PlatformKindFH::WeChat: return "WeChat";
        case PlatformKindFH::Weibo:  return "Weibo";
        default:                     return "?";
    }
}

inline bool platformFromName(const std::string& s, PlatformKindFH& out) {
    if (s == "QQ")     { out = PlatformKindFH::QQ;     return true; }
    if (s == "WeChat") { out = PlatformKindFH::WeChat; return true; }
    if (s == "Weibo")  { out = PlatformKindFH::Weibo;  return true; }
    return false;
}

}  // namespace persist_util_fh
