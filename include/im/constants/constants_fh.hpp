#pragma once
// ============================================================
// ConstantsFH —— 项目常量定义（统一管理硬编码数值）
// ============================================================
// 官方群号范围
namespace Constants {
    namespace GroupNumbers {
        constexpr unsigned MIN_OFFICIAL_QQ = 1001;
        constexpr unsigned MAX_OFFICIAL_QQ = 1002;
        constexpr unsigned MIN_OFFICIAL_WECHAT = 1003;
        constexpr unsigned MAX_OFFICIAL_WECHAT = 1004;
        constexpr unsigned MIN_OFFICIAL_WEIBO = 1005;
        constexpr unsigned MAX_OFFICIAL_WEIBO = 1006;
        constexpr unsigned MIN_CUSTOM = 1007;  // 自建群从 1007 起分配
    }

    // 群配置默认值
    namespace GroupConfig {
        constexpr std::size_t DEFAULT_MAX_MEMBERS = 50;
        constexpr std::size_t DEFAULT_RECALL_TIME_SECONDS = 120;
    }

    // 消息长度限制
    namespace MessageLimits {
        constexpr std::size_t QQ_TEXT_LIMIT = 8000;
        constexpr std::size_t WECHAT_TEXT_LIMIT = 5000;
        constexpr std::size_t WEIBO_TEXT_LIMIT = 1000;
    }

    // 聊天记录上限
    namespace ChatRecords {
        constexpr std::size_t MAX_RECORDS = 50;
    }

    // QQ 临时讨论组配置
    namespace DiscussionGroup {
        constexpr std::size_t MAX_MEMBERS = 20;
    }
}
