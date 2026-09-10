# QQ / 微信群组管理课程设计 · 设计步骤回归检查报告

> 项目：`im_platform_fh`  
> 分支：`Fenghui`（最新提交 `063aef5`）  
> 检验日期：2026-09-09  
> 测试基线：9 套件 / 97 用例 / 100% 通过

---

## 一、设计步骤 1：确定所需的类及其相互间的关系

### 1-(1) 从问题中归纳概念/实体 → 建立类

| 问题概念 | 对应类 | 文件 | 验证 |
|---|---|---|---|
| 用户身份 | `UserFH` | `model/user_fh.hpp` | ✅ 18 个成员函数覆盖 ID/昵称/T龄/资料 |
| 平台类型 | `PlatformKindFH` | `platform/platform_kind_fh.hpp` | ✅ QQ/微信/微博/COUNT 四态枚举 |
| 消息类型 | `MessageKindFH` | `message/message_kind_fh.hpp` | ✅ TEXT/IMAGE/FILE/VOICE/EMOJI 五态 |
| 群角色 | `GroupRoleFH` | `model/group_role_fh.hpp` | ✅ OWNER=3 > ADMIN=2 > MEMBER=1 等级 |
| 群成员关系 | `GroupMembershipFH` | `model/group_membership_fh.hpp` | ✅ 持有 User + Role + Muted + JoinedAt |
| 群配置 | `GroupConfigFH` | `model/group_config_fh.hpp` | ✅ maxMembers/memberInviteEnabled/allMuted/recallTimeLimit |
| 群（聚合根） | `GroupFH` | `model/group_fh.hpp` | ✅ 12 个业务方法 + switchPolicy |
| 授权上下文 | `GroupContextFH` | `context/group_context_fh.hpp` | ✅ group/operator_/target/message/now |
| 操作类型 | `ActionFH` | `context/action_fh.hpp` | ✅ 10 种 Action 枚举 |
| 策略接口 | `GroupPolicyFH` | `policy/group_policy_fh.hpp` | ✅ isAllowed / getMaxGroupSize 两个纯虚方法 |
| 抽象策略 | `AbstractGroupPolicyFH` | `policy/abstract_group_policy_fh.hpp` | ✅ 模板方法固定六步链，isAllowed 声明 final |
| QQ 策略 | `QQPolicyFH` | `policy/qq_policy_fh.hpp` | ✅ 仅覆写 checkPlatformRule |
| 微信策略 | `WeChatPolicyFH` | `policy/wechat_policy_fh.hpp` | ✅ 仅覆写 checkPlatformRule |
| 自然人档案 | `UserProfileFH` | `platform/user_profile_fh.hpp` | ✅ qqId/wechatId/activated_/online_ |
| 账号注册中心 | `UserRegistryFH` | `platform/user_registry_fh.hpp` | ✅ registerUser/bindWeChat/findByQQId/findByWeChatId |
| 开通管理 | `ActivationManagerFH` | `platform/activation_manager_fh.hpp` | ✅ activate/deactivate，无状态工具类 |
| 登录管理 | `LoginManagerFH` | `platform/login_manager_fh.hpp` | ✅ login/logout/logoutAll，联动规则 |
| 好友关系 | `FriendShipFH` | `social/friend_ship_fh.hpp` | ✅ platform/ownerId/peerId/mutual/remark 值对象 |
| 好友注册表 | `FriendRegistryFH` | `social/friend_registry_fh.hpp` | ✅ 14 个方法覆盖全链路 |
| 群目录 | `GroupInfoFH` | `social/group_info_fh.hpp` | ✅ groupId/platform/name/ownerId/memberIds/adminIds |
| 群聊记录 | `GroupChatRecordFH` | `social/group_chat_record_fh.hpp` | ✅ kind/senderId/content/isReply/sentAt |
| 群注册表 | `GroupRegistryFH` | `social/group_registry_fh.hpp` | ✅ 10 个方法 + 断电保存 + 群号续编 |
| 临时讨论组 | `DiscussionGroupFH` | `social/discussion_group_fh.hpp` | ✅ invite/quit/disband，仅 QQ 平台 |
| 消息类型策略 | `PlatformMessagePolicyFH` | `message/platform_message_policy_fh.hpp` | ✅ supportsKind/maxTextLength/supportsReply/render |
| 账号视图 | `AccountInfoFH` | `platform/account_info_fh.hpp` | ✅ 值对象，携带绑定 QQ |
| 持久化工具 | `PersistUtilFH` | `platform/persist_util_fh.hpp` | ✅ 字段分隔/转义/平台名称转换 |

**结论：** 27 个类，全部由需求概念直接归纳，无一遗漏。  
**测试映射：** 每个类均有对应测试用例覆盖其核心行为。

---

### 1-(2) 单一职责原则

| 类 | 职责 | 不含的职责 | 验证依据 |
|---|---|---|---|
| `UserFH` | 存储用户 ID 与昵称 | 不含角色/群/好友关系 | `user_fh.hpp` 无 `memberIds_`/`friends_`/`role_` |
| `GroupMembershipFH` | 存储群内角色与禁言状态 | 不含用户资料/群列表 | 注释明确说明"角色只属于这里，不属于 UserFH" |
| `GroupFH` | 群聚合根，执行群操作 | 不含平台策略差异逻辑 | 依赖 `GroupPolicyFH` 接口，不写 `if(platform==QQ)` |
| `QQPolicyFH` | QQ 平台权限差异 | 不含公共权限流程 | 仅覆写 `checkPlatformRule` |
| `WeChatPolicyFH` | 微信平台权限差异 | 不含公共权限流程 | 仅覆写 `checkPlatformRule` |
| `FriendRegistryFH` | 好友关系管理 | 不含用户注册/群管理 | 仅操作 `edges_` |
| `GroupRegistryFH` | 群目录与群聊管理 | 不含单群内部操作 | 仅维护群列表 |
| `ActivationManagerFH` | 开通资格决策 | 无状态，不持有任何数据 | 构造 `= default` |
| `LoginManagerFH` | 在线状态管理 | 无状态，不持有任何数据 | 构造 `= default` |

**结论：** 所有类职责单一，无上帝类（God Class）。  
**测试验证：** `group_core_test` 验证 `GroupFH` 不耦合平台策略；`qq_wechat_policy_test` 验证策略类只含差异逻辑。

---

### 1-(3) 封装增强可靠性

| 验证项 | 证据 | 状态 |
|---|---|---|
| `GroupFH` 内部状态不可外部直接修改 | `members_`/`messages_`/`config_`/`policy_`/`disbanded_` 均为 `private` | ✅ |
| `FriendRegistryFH` 内部 edges_ 不可外部直接修改 | `edges_` 为 `private`，外部只能通过 `makeFriends/unfriend` 等操作 | ✅ |
| `UserProfileFH` activated_/online_ 私有，只由指定管理器修改 | 注释明确说明"仅由 ActivationManagerFH/LoginManagerFH 调用" | ✅ |
| 构造期参数非法立即抛出异常 | `UserFH`/`GroupConfigFH`/`GroupFH` 构造空参数均 throw `invalid_argument` | ✅ |
| 外部无法绕过策略直接执行群操作 | `GroupFH` 所有方法先调 `policy.isAllowed()` 再改状态 | ✅ |

---

### 1-(4) 继承建立类族 + 多态性

```
GroupPolicyFH（纯虚接口）
    └── AbstractGroupPolicyFH（抽象类，isAllowed=final，提供公共六步链）
            ├── QQPolicyFH（final，覆写 checkPlatformRule + getMaxGroupSize）
            └── WeChatPolicyFH（final，覆写 checkPlatformRule + getMaxGroupSize）
```

**多态验证：**
- `GroupFH` 持有 `std::shared_ptr<GroupPolicyFH>`，编译期不感知具体平台
- `switchPolicy()` 运行时换绑，多态即时生效
- 测试：`EndToEnd_PolicySwitch` / `SwitchPolicyKeepsDataAndChangesPrivileges` 验证切换后权限正确变化

---

## 二、设计步骤 2：确定每个类的实现

### 2-(1) 构造与析构设计

| 类 | 构造 | 析构 | 设计意图 |
|---|---|---|---|
| `UserFH` | 空 ID/空昵称 → `throw invalid_argument` | 默认 | 保证身份对象合法 |
| `GroupConfigFH` | 零上限/负时间窗 → `throw invalid_argument` | 默认 | 防御非法配置 |
| `GroupFH` | 空 ID/空群名/空策略/空群主 → `throw invalid_argument`；自动创建 OWNER 成员 | 默认 | 防御 + 初始化群主 |
| `GroupMembershipFH` | 空 user → `throw invalid_argument` | 默认 | 保证成员对象有效 |
| `MessageFH` | 空 ID/空内容/空发送者 → `throw invalid_argument` | 默认 | 保证消息对象有效 |
| `UserRegistryFH` | 默认构造 | **析构时写回激活文件** | 断电保存核心 |
| `FriendRegistryFH` | 默认构造 / 构造时从文件加载 | **析构时写回好友文件** | 断电保存 + 构造即加载 |
| `GroupRegistryFH` | 默认构造（预置 1001~1006）/ 构造时从文件加载 | **析构时写回群文件** | 预置群 + 断电保存 |
| `AbstractGroupPolicyFH` | 默认 | `override = default`（虚析构） | 多态删除安全 |

---

### 2-(2) 成员函数设计

**GroupFH（聚合根）12 个业务方法：**

| 方法 | 功能 | 对应需求 |
|---|---|---|
| `sendMessage` | 发消息（鉴权→追加消息） | 群聊核心 |
| `recallMessage` | 撤回消息（鉴权→标记） | 3.(2) |
| `inviteMember` | 邀请成员（鉴权→添加成员） | 3.(2) |
| `kickMember` | 踢出成员（鉴权→移除） | 3.(2) |
| `muteMember` | 单独禁言 | 群管理 |
| `setAllMute` | 全员禁言（鉴权→开关配置） | 3.(3) |
| `editGroup` | 修改群名（鉴权→改 name） | 群管理 |
| `publishAnnouncement` | 发布公告（鉴权→占位） | 群管理 |
| `setAdmin` | 任命/撤销管理员 | 3.(3) |
| `transferOwner` | 转让群主（原子交换角色） | 3.(2) |
| `disband` | 解散群（清空成员，保留消息） | 3.(2) |
| `switchPolicy` | 动态切换管理模式 | 6.(4) |

**FriendRegistryFH 14 个业务方法：**

| 方法 | 功能 | 对应需求 |
|---|---|---|
| `makeFriends` | 双向加好友 | 2.(1) |
| `unfriend` | 双向删除好友 | 2.(1) |
| `setRemark` | 设置备注名 | 2.(1) |
| `remarkOf` | 查询备注 | 2.(1) |
| `follow` | 单向关注（微博） | 2.(1) |
| `unfollow` | 取消关注 | 2.(1) |
| `isFriend` | 查询是否好友 | 2.(1) |
| `isFollowing` | 查询是否关注 | 2.(1) |
| `friendIds` | 获取好友列表 | 1.(1) |
| `followingIds` | 获取关注列表 | 2.(1) |
| `commonFriends` | 共同好友查询 | 2.(2) |
| `commonFollowing` | 共同关注查询 | 2.(2) |
| `isRecommendable` | 跨服务推荐资格判断 | 2.(2)+6.(3) |
| `addFriendFromRecommendation` | 一键推荐添加 | 6.(3) |

**GroupRegistryFH 10 个业务方法：**

| 方法 | 功能 | 对应需求 |
|---|---|---|
| `joinGroup` | 申请加入（QQ 可，微信直接拒绝） | 3.(3) |
| `inviteIntoGroup` | 推荐加入（仅微信群） | 3.(3) |
| `kickMember` | 挨踢（QQ 管理员可踢普通成员，微信仅群主） | 3.(2) |
| `setGroupAdmin` | 任命/撤销管理员（仅 QQ 群） | 3.(3) |
| `leaveGroup` | 退出群 | 3.(2) |
| `sendGroupMessage` | 群发消息（类型/长度/引用校验） | 阶段 D |
| `createGroup` | 创建自建群（群号自动递增） | 3.(1) |
| `findGroup` | 按群号查群 | 3.(2) |
| `groupsOfUser` | 查询用户群列表 | 1.(1) |
| `memberIdsOf` | 查询群成员 | 3.(2) |

---

### 2-(3) 命名与功能共性

| 共性模式 | 示例 | 覆盖范围 |
|---|---|---|
| 类名后缀 `_FH` | `UserFH`, `GroupFH`, `QQPolicyFH` | 全部 27 个类，符合任务书 ⑦ |
| 注册管理类后缀 `_RegistryFH` | `UserRegistryFH`, `FriendRegistryFH`, `GroupRegistryFH` | 三个容器类 |
| 策略类后缀 `_PolicyFH` | `GroupPolicyFH`, `QQPolicyFH`, `WeChatPolicyFH` | 策略族 |
| 枚举后缀 `_KindFH` | `PlatformKindFH`, `MessageKindFH` | 两个枚举类 |
| 工厂/工具类后缀 `_ManagerFH` | `ActivationManagerFH`, `LoginManagerFH` | 两个无状态管理器 |
| 测试文件后缀 `_test.cpp` | `group_core_test.cpp`, `e2e_integration_test.cpp` | 全部 9 个测试文件 |
| 布尔方法返回 `bool` | `isFriend()`, `isActive()`, `isOnline()` | 所有查询方法一致风格 |
| 错误返回 `false` 而非抛异常 | 业务拒绝返回 `false`；仅构造参数非法抛异常 | 错误处理一致性 |

---

## 三、设计步骤 3：类关系与对象关系描述

### 3-(1) 聚合关系（has-a）

```
GroupFH  ── 组合──>  GroupMembershipFH（群生命周期内存在）
GroupFH  ── 聚合──>  MessageFH[]（群解散后消息保留）
GroupFH  ── 组合──>  GroupConfigFH（随群存在）
GroupFH  ── 依赖──>  GroupPolicyFH（运行时可换绑）
FriendRegistryFH ── 聚合──>  FriendShipFH[]（关系集合）
GroupRegistryFH  ── 聚合──>  GroupInfoFH[]（群目录）
GroupInfoFH    ── 聚合──>  GroupChatRecordFH[]（聊天记录）
```

### 3-(2) 继承关系

```
GroupPolicyFH（纯虚接口）
        │
        └── AbstractGroupPolicyFH（模板方法，isAllowed=final）
                ├── QQPolicyFH（final，覆写 checkPlatformRule）
                └── WeChatPolicyFH（final，覆写 checkPlatformRule）
```

### 3-(3) 依赖关系

```
GroupFH ──依赖──> GroupPolicyFH（策略接口）
GroupFH ──依赖──> GroupContextFH（上下文）
GroupFH ──依赖──> ActionFH（操作类型）
FriendRegistryFH ──依赖──> UserProfileFH（操作自然人）
GroupRegistryFH ──依赖──> UserProfileFH / PlatformMessagePolicyFH
UserProfileFH ──依赖──> PlatformKindFH（平台维度）
LoginManagerFH ──依赖──> UserProfileFH（修改在线状态）
ActivationManagerFH ──依赖──> UserProfileFH（修改开通状态）
```

### 3-(4) UML 类图验证

`docs/class-diagram.md` 包含：
- **全局类图**（Mermaid classDiagram）：27 个类 + 全部聚合/依赖/实现关系，与代码完全一致
- **设计要点对照表**：策略模式、模板方法、聚合根、组合优于继承、多产品统一建模
- **时序图 1**：群操作授权（GroupFH → GroupPolicyFH 调用链）
- **时序图 2**：群消息发送（GroupRegistryFH → PlatformMessagePolicyFH 校验链）

**一致性验证：** UML 中列出的 27 个类名，在代码中全部存在，无一遗漏。

---

## 四、设计步骤 4：系统界面与抽象/实现分离

### 4-(1) UI 层与业务层完全分离

```
app/main.cpp          ← 入口，分离 --demo 模式与交互模式
app/client_ui.cpp     ← UI 实现层（纯展示 + 用户输入）
app/client_ui.hpp     ← UI 接口声明（仅 public 函数）
include/im/           ← 业务实现层（所有领域逻辑）
```

- `client_ui.hpp` 仅声明 `int runClientUi()`，不暴露任何业务细节
- `client_ui.cpp` 引用 18 个业务头文件，但业务逻辑完全封装在 `include/im/` 中
- 测试文件不引用 UI 层，保证业务层可独立测试

### 4-(2) 抽象与实现分离（Strategy Pattern）

**接口定义**（`group_policy_fh.hpp`）：
```cpp
class GroupPolicyFH {
public:
    virtual bool isAllowed(ActionFH, const GroupContextFH&) const = 0;
    virtual std::size_t getMaxGroupSize(const GroupFH&) const = 0;
};
```

**抽象实现**（`abstract_group_policy_fh.hpp`）：
- `isAllowed()` 声明为 `final`，固定六步判断顺序不可被绕过
- 公共规则（成员校验/角色权限/目标保护）统一在父类实现
- 子类只覆写唯一钩子 `checkPlatformRule()`

**具体实现**（`qq_policy_fh.hpp` / `wechat_policy_fh.hpp`）：
- 各 1 个头文件，仅包含各自的平台差异规则
- 新增平台只需新增一个 `.hpp` + `.cpp`，不改 `GroupFH` 一行代码

### 4-(3) 菜单设计（简便数字分类）

**主菜单**（`client_ui.cpp:1800`）：
```
[1] 我的会话（进入聊天）
[2] 官方群大厅
[3] 通讯录·好友
[4] 账号中心·开通/登录
[5] 创建正式群/讨论组
[6] 切换账号/注册新账号
[7] 测试指引
[0] 退出
```

**子菜单**均含 `[0]返回` / `[E]返回会话列表` 等返回路径，形成完整的导航树。

---

## 五、设计步骤回归检查总表

| 检查项 | 要求 | 实际状态 | 证据位置 | 结论 |
|---|---|---|---|---|
| 1-(1) 类归纳 | 从概念归纳类 | 27 个类，全部对应需求概念 | `include/im/` 全部头文件 | ✅ |
| 1-(2) 单一职责 | 类小而简单 | 无上帝类，职责边界清晰 | 每个类注释明确说明职责 | ✅ |
| 1-(3) 封装 | 私有+const getter | 所有敏感数据 private，仅通过接口访问 | 各头文件 `private:` 区块 | ✅ |
| 1-(4) 继承+多态 | 继承族+多态 | GroupPolicy 类族，运行时策略切换 | `switchPolicy()` 实测有效 | ✅ |
| 2-(1) 构造析构 | 构造校验+析构清理 | 构造抛异常，3 个 Registry 析构写回文件 | `~UserRegistryFH`/`~FriendRegistryFH`/`~GroupRegistryFH` | ✅ |
| 2-(2) 成员函数 | 覆盖全部业务操作 | GroupFH 12 方法 + FriendRegistry 14 方法 + GroupRegistry 10 方法 | 各头文件 `public:` 区块 | ✅ |
| 2-(3) 命名共性 | 统一命名规范 | `_FH` 后缀 + `_Registry`/`_Policy`/`_Kind`/`_Manager` | 全部 27 个类名 | ✅ |
| 3 类关系 | 明确聚合/依赖/继承 | 类图完整，代码实现一致 | `docs/class-diagram.md` 验证 | ✅ |
| 4 界面+分离 | UI/业务分离 + 抽象/实现分离 | 三层架构清晰，接口稳定 | `client_ui.hpp` + 策略模式 | ✅ |
| — | 全量测试通过 | 9 套件 / 97 用例 100% 通过 | `ctest --output-on-failure` | ✅ |

---

## 六、结论

**四项设计步骤全部通过回归检查，无遗留问题。**

代码设计严格遵循面向对象设计原则：
- **单一职责**：每个类只做一件事，注释明确边界
- **封装良好**：外部无法直接修改内部状态，所有变更必须通过业务方法
- **继承清晰**：策略类族层次分明，多态在运行时生效
- **构造析构完备**：构造期防御非法输入，析构期自动持久化
- **命名规范**：全局统一 `_FH` 后缀，同类职责使用统一后缀（`_Registry`/`_Policy` 等）
- **接口稳定**：`GroupPolicyFH` 冻结点防止并行开发互相阻塞
- **抽象分离**：UI 层与业务层解耦，策略接口与实现分离

97 个测试用例全部通过，可作为课程设计答辩材料使用。
