# QQ / 微信群组管理课程设计：四人 C++ 分工

> 技术栈：C++17、CMake、GoogleTest（或 `<cassert>`）

---

## 架构总览

```
Group（聚合根）
  └── GroupPolicy（策略接口）
        └── AbstractGroupPolicy（公共流程）
              ├── QQPolicy
              └── WeChatPolicy

Action + GroupContext → 统一权限判断
```

---

## 落地状态（阶段 A~D 已实现，2026-09）

> 本文下方各“负责文件”清单为**早期规划命名**（无 FH 后缀、计划拆分
> `src/model/*.cpp`）。集成分支 FH 的最终形态已统一为 **FH 命名 + header-only
> 领域模型**，仅策略实现保留在 `src/policy/`，自动化测试已接入 ctest。规则本身
> （含下文的平台差异表与验收清单）均与实现一致，以下为映射关系：

| 早期规划 | 落地（分支 FH） |
|---|---|
| `include/group/model/{user,group_role,group_membership,group_config,message,group}.hpp` | `include/im/model/*_fh.hpp`（全部 header-only，无 `src/model/*.cpp`） |
| `include/group/context/{action,group_context}.hpp` | `include/im/context/*_fh.hpp` |
| `include/group/policy/{group_policy,abstract_group_policy,qq_policy,wechat_policy}.hpp` + `src/policy/*.cpp` | `include/im/policy/*_fh.hpp` + `src/policy/*_fh.cpp` |
| 阶段 B~D 新增 | `include/im/platform/`、`include/im/social/`、`include/im/message/` |
| `tests/{integration_tests,policy_tests}.cpp` | `tests/{group_core,abstract_policy,qq_wechat_policy,platform_service,social_message}_test.cpp`（自包含断言，经 ctest 一键运行） |

---

## 分工总览

| 角色 | 职责 | 对应原成员 |
|---|---|---|
| **组长兼架构师** | 架构决策、公共接口定义、集成验收、答辩统筹 | D |
| **用户** | 领域模型：User、GroupRole、GroupMembership、GroupConfig、Message | A |
| **群** | Group 聚合根：所有群操作的业务方法 | A+B |
| **服务** | 策略框架：Action、GroupContext、AbstractGroupPolicy、QQ/WeChatPolicy | B+C |

---

## 一、组长兼架构师（D）

### 负责内容

1. **项目骨架**：创建 CMake 工程，配置 C++17、测试目标、编译选项。
2. **公共接口冻结**：定义 `Action`、`GroupContext`、`GroupPolicy` 三个接口，冻结后不随意变更。
3. **集成测试**：编写跨模块集成测试，覆盖 QQ/微信邀请差异、全员禁言、群主转让、解散后操作等场景。
4. **Demo 程序**：编写控制台 Demo，直观展示两个平台的策略差异。
5. **文档与验收**：维护 README（编译/测试/运行说明）、类图、答辩材料；合并前全量测试通过。

### 具体文件

```text
CMakeLists.txt
app/main.cpp
tests/integration_tests.cpp
README.md
reports/类图(FH分支全量).md
```

### 交付标准

- 新人拉取项目后一条命令运行测试：`cmake --build build && ctest --output-on-failure`
- Demo 可演示 QQ 普通成员邀请成功、微信普通成员邀请失败等差异。
- README 包含构建方法、测试方法、类图说明、四人分工。

---

## 二、用户（A）

### 负责内容

实现所有领域实体，角色属于 `GroupMembership` 而非 `User`。

### 具体文件与任务

```text
include/group/model/user.hpp                  → User：userId、nickname，构造参数非法抛 std::invalid_argument
include/group/model/group_role.hpp            → GroupRole 枚举：OWNER / ADMIN / MEMBER，提供等级比较 operator
include/group/model/group_membership.hpp      → GroupMembership：持有 User、GroupRole、isMuted 字段
include/group/model/group_config.hpp          → GroupConfig：maxMembers、memberInviteEnabled、allMuted、recallTimeLimit
include/group/model/message.hpp               → Message：messageId、sender、content、sentTime、isRecalled
src/model/user.cpp
src/model/group_membership.cpp
src/model/group_config.cpp
src/model/message.cpp
```

### 单元测试

为每个模型类编写基础测试：构造校验、属性读写、枚举比较。

### 交付标准

- 所有类无虚函数，不依赖任何策略接口。
- `GroupMembership` 是唯一持有角色的地方。
- `User` 不暴露内部容器，只提供 const 引用访问。

---

## 三、群（A+B）

### 负责内容

`Group` 聚合根，封装所有群业务操作。B 负责通过 `GroupPolicy` 的授权逻辑，A 负责状态变更。

### 具体文件

```text
include/group/model/group.hpp
src/model/group.cpp
```

### 任务

**A 负责的状态变更：**

| 方法 | 功能 |
|---|---|
| `sendMessage(operator, content)` | 发消息，创建 Message 并追加到消息列表 |
| `recallMessage(messageId)` | 撤回消息，标记 isRecalled |
| `inviteMember(user)` | 添加成员，检查人数上限和唯一性 |
| `kickMember(target)` | 踢出成员 |
| `muteMember(target, duration)` | 禁言成员 |
| `editGroup(newName)` | 修改群名 |
| `setAllMute(enabled)` | 全员禁言开关 |
| `transferOwner(newOwnerId)` | 转让群主，原群主降级为 ADMIN |
| `disband()` | 解散群，标记解散状态 |

**B 负责的授权逻辑（通过注入的 `GroupPolicy&`）：**

- 每个方法先调用 `policy.isAllowed(action, ctx)`，返回 `false` 则方法直接返回 `false`，不修改状态。
- 构造时接收 `std::shared_ptr<GroupPolicy>`，不感知平台差异。

### 约束

- 不在 `Group` 中写任何 QQ/微信分支。
- 不暴露成员容器的可写引用。
- 群解散后所有方法返回 `false`。

### 交付标准

- `new Group(policy)` 后，用 `QQPolicy` 和 `WeChatPolicy` 都能正常创建群。
- 成员重复添加返回 `false`，人数超上限返回 `false`。
- 解散后调用任意方法均返回 `false`。

---

## 四、服务（B+C）

### 负责内容

策略框架：公共权限流程 + 平台差异实现。

### B 负责文件

```text
include/group/context/action.hpp            → Action 枚举：SEND_MESSAGE / RECALL_MESSAGE / INVITE_MEMBER / KICK_MEMBER / MUTE_MEMBER / EDIT_GROUP / PUBLISH_ANNOUNCEMENT / SET_ALL_MUTE / TRANSFER_OWNER / DISBAND_GROUP
include/group/context/group_context.hpp     → GroupContext 结构体：group、operator_、target、message（后三者可为 nullptr）
include/group/policy/group_policy.hpp       → GroupPolicy 纯虚接口：isAllowed、getMaxGroupSize
include/group/policy/abstract_group_policy.hpp
src/policy/abstract_group_policy.cpp
```

**`AbstractGroupPolicy` 固定流程（`isAllowed` 声明为 `final`）：**

```
validateContext        → group 和 operator_ 非空
      ↓
checkMembership        → operator_ 必须在群中
      ↓
checkPermission        → 根据 Action 和角色判断是否有权限
      ↓
checkTargetPermission  → 管理员不能操作同级或更高角色
      ↓
checkPlatformRule      → 交由子类实现平台差异
```

### C 负责文件

```text
include/group/policy/qq_policy.hpp
include/group/policy/wechat_policy.hpp
src/policy/qq_policy.cpp
src/policy/wechat_policy.cpp
tests/policy_tests.cpp
```

**只重写两个方法：**

| 方法 | QQ | 微信 |
|---|---|---|
| `checkPlatformRule` | 全员禁言时 admin+owner 可发消息；`memberInviteEnabled=true` 时普通成员可邀请；admin+owner 可设全员禁言 | 全员禁言时 admin+owner 可发消息；仅 ADMIN+ 可邀请（普通成员不可）；只有 owner 可设全员禁言 |
| `getMaxGroupSize` | 读取 `GroupConfig.maxMembers` | 读取 `GroupConfig.maxMembers` |

### 平台差异测试（C 负责）

| 测试场景 | QQ | 微信 |
|---|---|---|
| 普通成员邀请 | 取决于 config | 始终失败 |
| admin 设全员禁言 | 成功 | 失败 |
| 全员禁言时普通成员发消息 | 失败 | 失败 |
| 全员禁言时 admin 发消息 | 成功 | 成功 |

### 交付标准

- `AbstractGroupPolicy::isAllowed` 中无 QQ/微信分支。
- 两个 Policy 不复制父类任何公共逻辑。
- 同一操作在 QQ 和微信下结果不同（邀请、全员禁言），其余结果相同。

---

## 协作顺序

```
第 1 阶段：组长建 CMake 骨架 + 定义公共接口（Action/Context/Policy）
           用户并行实现领域实体

第 2 阶段：组长提交接口冻结
           群（B）实现 AbstractGroupPolicy
           群（A）实现 Group 状态变更
           服务（C）准备测试骨架

第 3 阶段：服务（C）实现 QQPolicy / WeChatPolicy
           群（A）完善 Group 授权集成
           组长编写集成测试和 Demo

第 4 阶段：全员联调，修复边界问题，准备答辩
```

---

## Git 分支

```
main
└── develop
    ├── feature/domain-model        # 用户
    ├── feature/group-aggregate     # 群
    ├── feature/policy-framework    # 服务（B）
    ├── feature/platform-policy     # 服务（C）
    └── feature/integration-docs    # 组长
```

---

## 接口冻结点

**冻结后只修 bug，不改接口：**

```cpp
// 冻结点 1：公共接口（组长负责）
enum class Action { SEND_MESSAGE, RECALL_MESSAGE, INVITE_MEMBER, KICK_MEMBER,
                    MUTE_MEMBER, EDIT_GROUP, PUBLISH_ANNOUNCEMENT, SET_ALL_MUTE,
                    TRANSFER_OWNER, DISBAND_GROUP };

struct GroupContext {
    const Group* group;
    const User* operator_;
    const User* target;       // 可为 nullptr
    const Message* message;   // 可为 nullptr
};

class GroupPolicy {
public:
    virtual ~GroupPolicy() = default;
    virtual bool isAllowed(Action, const GroupContext&) const = 0;
    virtual std::size_t getMaxGroupSize(const Group&) const = 0;
};

// 冻结点 2：领域模型（用户负责）
// GroupMembership 持有角色；Group 通过 policy 授权；GroupConfig 持有配置

// 冻结点 3：平台差异（服务负责）
// QQ 普通成员邀请由 config 控制；微信普通成员不能邀请
// QQ admin 可设全员禁言；微信只有 owner 可设全员禁言
```

---

## 集成验收清单

- [ ] `cmake --build build && ctest --output-on-failure` 全部通过
- [ ] 角色位于 `GroupMembership`，不在 `User` 中
- [ ] 成员不可重复添加，人数上限生效
- [ ] 群解散后所有操作返回 `false`
- [ ] 群外用户被拒绝
- [ ] 管理员不能操作同级或更高角色
- [ ] 只有 owner 可以转让群主和解散群
- [ ] QQ 与微信邀请规则不同（测试覆盖）
- [ ] `getMaxGroupSize` 读取 `GroupConfig`，不硬编码
- [ ] `AbstractGroupPolicy` 中无平台分支
- [ ] Demo 可运行并展示策略差异
- [ ] README 与代码一致，每条命令可执行

---

## 风险处理

| 风险 | 处理方式 |
|---|---|
| 接口频繁变更 | 冻结点后集体确认才能改 |
| GoogleTest 装不上 | 用 `<cassert>` 降级，README 注明 |
| Group 越来越臃肿 | 本期允许集中管理，不提前拆 Service |
| 两个 Policy 太相似 | 只保留真实差异，不加多余抽象 |
| 合并冲突集中爆发 | 小提交、每日合并 develop、公共接口改前通知 |
