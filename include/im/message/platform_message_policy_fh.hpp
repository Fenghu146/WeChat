#pragma once
// ============================================================
// PlatformMessagePolicyFH —— 微X 平台“群消息”能力差异（作者代号：FH）
// ------------------------------------------------------------
// 消息平台差异（阶段 D）：把“各微X 平台上群聊消息的能力差异”
// 集中收口到一个策略类，供群聊发送时统一校验，避免在调用方
// 散布 if (platform == XX) 分支。
// 规则均为课程简化口径，明确声明如下：
//   - QQ   群：支持全部消息类型；文本上限 8000；支持引用回复；
//   - 微信 群：禁止发送【文件】（简化），支持文本/图片/语音/表情；
//              文本上限 5000；支持引用回复；
//   - 微博 群：仅支持【文本/表情】（简化），不支持文件/图片/语音；
//              文本上限 1000；不支持引用回复。
// 其中 render() 只做“同一条内容在不同产品视图下的形态示意”，
// 供演示说明呈现层差异，不代表任何真实平台的排版实现。
// ============================================================
#include <cstddef>
#include <string>

#include "im/message/message_kind_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"

class PlatformMessagePolicyFH {
public:
    static constexpr std::size_t kQQTextLimit = 8000;
    static constexpr std::size_t kWeChatTextLimit = 5000;
    static constexpr std::size_t kWeiboTextLimit = 1000;

    // 各平台允许发送的消息类型
    static bool supportsKind(PlatformKindFH platform, MessageKindFH kind) {
        if (!isValidPlatformFH(platform)) return false;
        switch (platform) {
            case PlatformKindFH::QQ:
                return true;  // QQ：全部类型
            case PlatformKindFH::WeChat:
                return kind != MessageKindFH::DOCUMENT;  // 微信禁文件（简化）
            case PlatformKindFH::Weibo:
                return kind == MessageKindFH::TEXT ||
                       kind == MessageKindFH::EMOJI;  // 微博仅文本/表情（简化）
            default:
                return false;
        }
    }

    // 各平台文本长度上限
    static std::size_t maxTextLength(PlatformKindFH platform) {
        switch (platform) {
            case PlatformKindFH::QQ:    return kQQTextLimit;
            case PlatformKindFH::WeChat: return kWeChatTextLimit;
            case PlatformKindFH::Weibo:  return kWeiboTextLimit;
            default:                     return 0;
        }
    }

    // 是否支持“引用回复”（消息扩展，阶段 D）
    static bool supportsReply(PlatformKindFH platform) {
        return platform == PlatformKindFH::QQ ||
               platform == PlatformKindFH::WeChat;
    }

    // 同一内容在不同产品视图下的展示形态（仅演示示意）
    static std::string render(PlatformKindFH platform,
                              const std::string& nickname,
                              const std::string& content,
                              MessageKindFH kind,
                              const std::string& timeText) {
        const std::string tag =
            kind == MessageKindFH::TEXT ? std::string{}
                                        : std::string("[") + kindToZhName(kind) + "]";
        switch (platform) {
            case PlatformKindFH::QQ:
                return "[QQ] " + nickname + " " + timeText + " " + tag + " " +
                       content;
            case PlatformKindFH::WeChat:
                return nickname + "（微信）" + timeText + " " + tag + " " +
                       content;
            case PlatformKindFH::Weibo:
                return "#微X# " + nickname + " " + timeText + " " + tag + " " +
                       content;
            default:
                return content;
        }
    }
};
