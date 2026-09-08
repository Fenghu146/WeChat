# 类图（FH 分支全量）

> 版本：阶段 A ~ D · 绘制于 2026-09
> 用于课程设计答辩与文档审查。Mermaid 需在支持 Mermaid 的 Markdown
> 阅读器（如 Typora / VS Code 插件 / GitHub）中渲染。
> 配套说明见根目录 `README.md`（各阶段功能与平台差异规则表）。

## 0. 阅读指引

类按职责分五组：

| 分组 | 目录 | 职责 |
|---|---|---|
| 领域实体 | `model/` | 用户、角色、成员、群配置、消息、群（聚合根） |
| 平台策略 | `context/` `policy/` | 授权上下文 + 群策略接口 / 抽象链 / 平台实现 |
| 多产品身份 | `platform/` | 平台枚举、自然人档案、账号、注册 / 开通 / 登录 |
| 社交关系 | `social/` | 好友与关注、群注册表与群聊、QQ 临时讨论组 |
| 消息扩展 | `message/` | 消息类型枚举、平台消息能力规则 |

## 1. 全局类图

```mermaid
classDiagram
    direction LR

    %% ---------- 枚举 ----------
    class PlatformKindFH { <<enumeration>> QQ WeChat Weibo COUNT }
    class MessageKindFH { <<enumeration>> TEXT IMAGE FILE VOICE EMOJI }
    class ActionFH { <<enumeration>> SEND_MESSAGE RECALL_MESSAGE INVITE_MEMBER KICK_MEMBER MUTE_MEMBER EDIT_GROUP PUBLISH_ANNOUNCEMENT SET_ALL_MUTE ASSIGN_ADMIN TRANSFER_OWNER DISBAND_GROUP }
    class GroupRoleFH { <<enumeration>> MEMBER=1 ADMIN=2 OWNER=3 }

    %% ---------- model ----------
    class UserFH {
        +id
        +nickname
    }
    class GroupMembershipFH {
        +role
        +muted
        +joinedAt
    }
    class GroupConfigFH {
        +maxMembers
        +memberInviteEnabled
        +allMuted
        +recallTimeLimit
    }
    class MessageFH {
        +kind_
        +recall()
        +isRecalled()
    }
    class GroupFH {
        +sendMessage / recallMessage
        +inviteMember / kickMember
        +muteMember / setAllMute
        +editGroup / publishAnnouncement
        +setAdmin / transferOwner / disband
        +switchPolicy
    }

    %% ---------- context / policy ----------
    class GroupContextFH {
        +operatorUser
        +target
        +message
        +now
    }
    class GroupPolicyFH {
        <<interface>>
        +isAllowed(action, context)
        +getMaxGroupSize(group)
    }
    class AbstractGroupPolicyFH {
        <<abstract>>
        +isAllowed(...) final
        #checkPlatformRule(...) virtual
    }
    class QQPolicyFH
    class WeChatPolicyFH

    %% ---------- platform ----------
    class UserProfileFH {
        +qqId / weiboId
        +wechatId(可绑定)
        +platformAccountId(p)
    }
    class AccountInfoFH
    class UserRegistryFH {
        +registerUser()
        +bindWeChat()
    }
    class ActivationManagerFH {
        +开通 / 取消
    }
    class LoginManagerFH {
        +登录 / 退出
    }

    %% ---------- social ----------
    class FriendShipFH {
        +mutual 双向好友 / 单向关注
    }
    class FriendRegistryFH {
        +makeFriends / unfriend / follow
        +isFriend / isFollowing
    }
    class GroupRegistryFH {
        +joinGroup / leaveGroup / createGroup
        +sendGroupMessage / chatOf
        +groupsOfPlatform / groupsOfUser
    }
    class GroupInfoFH {
        +groupId / platform / name
        +ownerId / memberIds
    }
    class GroupChatRecordFH {
        +kind / senderNick / content
        +isReply / sentAt
    }
    class DiscussionGroupFH {
        +invite / quit / disband
    }

    %% ---------- message ----------
    class PlatformMessagePolicyFH {
        +supportsKind / maxTextLength / supportsReply
        +render(视图示意)
    }

    %% ---------- 关系：多产品身份 ----------
    UserRegistryFH --> UserProfileFH
    ActivationManagerFH ..> UserProfileFH
    LoginManagerFH ..> UserProfileFH
    UserProfileFH o-- AccountInfoFH
    UserProfileFH ..> PlatformKindFH

    %% ---------- 关系：群聚合与策略 ----------
    GroupFH o-- GroupMembershipFH
    GroupMembershipFH --> UserFH
    GroupMembershipFH --> GroupRoleFH
    GroupFH o-- MessageFH
    GroupFH o-- GroupConfigFH
    GroupFH --> GroupPolicyFH
    GroupContextFH --> GroupFH
    GroupContextFH --> ActionFH

    %% ---------- 关系：策略继承 ----------
    GroupPolicyFH <|.. AbstractGroupPolicyFH
    AbstractGroupPolicyFH <|-- QQPolicyFH
    AbstractGroupPolicyFH <|-- WeChatPolicyFH

    %% ---------- 关系：社交 ----------
    FriendRegistryFH o-- FriendShipFH
    FriendRegistryFH ..> UserProfileFH
    FriendShipFH ..> PlatformKindFH
    GroupRegistryFH o-- GroupInfoFH
    GroupRegistryFH ..> UserProfileFH
    GroupRegistryFH ..> PlatformMessagePolicyFH
    GroupInfoFH o-- GroupChatRecordFH
    DiscussionGroupFH ..> UserProfileFH

    %% ---------- 关系：消息扩展 ----------
    MessageFH --> MessageKindFH
    GroupChatRecordFH --> MessageKindFH
    PlatformMessagePolicyFH ..> PlatformKindFH
    PlatformMessagePolicyFH ..> MessageKindFH
```

## 2. 设计要点对照

| 面向对象手段 | 落点 |
|---|---|
| 接口抽象（Strategy） | `GroupFH` 依赖 `GroupPolicyFH`，不依赖具体平台 |
| 模板方法（Template Method） | `AbstractGroupPolicyFH::isAllowed` final，固定六步授权链，平台只覆写 `checkPlatformRule` |
| 聚合根 | `GroupFH` 对外只暴露业务方法，内部先授权后变更状态 |
| 组合优于继承 | 角色挂在 `GroupMembershipFH`（成员实例）上而非用户上 |
| 多产品统一建模 | `UserProfileFH`（自然人） + `PlatformKindFH` 维度贯穿社交与消息 |

## 3. 典型时序

### 3.1 群操作授权（阶段 A）

```mermaid
sequenceDiagram
    participant C as 调用方
    participant G as GroupFH
    participant P as GroupPolicyFH
    C->>G: setAdmin(操作者, 目标, true)
    G->>G: 组装 GroupContextFH(now=注入时刻)
    G->>P: isAllowed(ASSIGN_ADMIN, ctx)
    P-->>G: true / false（含平台差异 checkPlatformRule）
    G-->>C: 返回操作结果（授权失败不改变状态）
```

### 3.2 群消息发送（阶段 D）

```mermaid
sequenceDiagram
    participant C as 调用方
    participant R as GroupRegistryFH
    participant M as PlatformMessagePolicyFH
    C->>R: sendGroupMessage(用户, 平台, 群号, 类型, 内容, 引用?)
    R->>R: 群存在且平台匹配 + 发送者是群成员
    R->>M: supportsKind(平台, 类型)
    R->>M: maxTextLength(平台) < 内容长度?
    R->>M: supportsReply(平台)（引用时）
    R-->>C: 全部通过 → 追加 GroupChatRecordFH（超限淘汰最旧）
```
