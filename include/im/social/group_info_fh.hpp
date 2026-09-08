#pragma once
// ============================================================
// GroupInfoFH —— 群目录条目（值对象，作者代号：FH）
// ------------------------------------------------------------
// 一个微X 群的目录信息（任务书优化(7)：类独立文件）：
//   - platform/groupId/name：群归属平台、群号、群名；
//   - ownerId：创建者在该平台的账号号码（官方预置群为空）；
//   - adminIds：管理员（仅 QQ 群的管理员制度使用）；
//   - memberIds：成员在该平台的账号号码（微信群成员=微信号）；
//   - chat：群聊消息记录（阶段 D，超上限淘汰最早记录）。
// ============================================================
#include <cstddef>
#include <string>
#include <vector>

#include "im/platform/platform_kind_fh.hpp"
#include "im/social/group_chat_record_fh.hpp"

struct GroupInfoFH {
    PlatformKindFH platform = PlatformKindFH::QQ;
    std::string groupId;       // 群号
    std::string name;          // 群名
    std::string ownerId;       // 创建者在该平台的账号号码（预置群为空=官方群）
    std::size_t maxMembers = 50;
    std::vector<std::string> memberIds;  // 成员在该平台的账号号码
    std::vector<std::string> adminIds;   // 管理员（仅 QQ 群的管理员制度使用）
    bool predefined = false;             // 是否为系统预置群
    std::vector<GroupChatRecordFH> chat; // 群聊消息记录（阶段 D）
};
