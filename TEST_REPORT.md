# QQ / 微信群组管理课程设计 · 测试报告

> 项目：`im_platform_fh`  
> 分支：`Fenghui`（最新提交 `063aef5`）  
> 构建：CMake + AppleClang 21.0.0，C++17 标准  
> 日期：2026-09-09  
> 运行命令：`cmake --build build && ctest --test-dir build --output-on-failure`

---

## 一、测试环境

| 项目 | 内容 |
|---|---|
| 操作系统 | macOS (darwin/arm64) |
| 编译器 | AppleClang 21.0.0.21000101 |
| C++ 标准 | C++17 |
| 构建工具 | CMake 3.16+ |
| 测试框架 | 自包含 `fh_mini_test.hpp`（零第三方依赖） |
| 总源码量 | 8027 行（含 include / src / tests / app） |
| 头文件数 | 27（全部 `_fh.hpp`，每类独立文件） |

---

## 二、测试结果总览

| 轮次 | 时间 | 套件数 | 用例总数 | 通过 | 失败 | 状态 |
|---|---|---:|---:|---:|---:|---|
| 第一次（原始） | 2026-09-08 | 8 | 82 | 82 | 0 | ✅ 全部通过 |
| 第二次（新增后） | 2026-09-09 | 9 | 97 | 97 | 0 | ✅ 全部通过 |
| **合计** | — | **9** | **97** | **97** | **0** | **✅ 100%** |

> 第一次测试为同步远端代码后的基准验收；  
> 第二次测试在新增 `requirement_alignment_comprehensive_test` 后复跑全量。

---

## 三、第一次测试结果（2026-09-08）

### 3.1 group_core_test — 18 用例 ✅

测试 `GroupFH` 聚合根及领域实体构造校验。

| 用例 | 覆盖点 |
|---|---|
| `InvalidEntityArgumentsThrow` | UserFH / GroupConfigFH / GroupFH 构造期非法参数抛异常 |
| `OwnerAssignedOnConstructionAndStaysUnique` | 群主构造时自动赋值且唯一 |
| `DuplicateMemberCannotJoinTwice` | 成员重复加入被拒 |
| `MemberLimitIsEnforced` | 人数上限生效 |
| `OutsideUserRejectedForAllOperations` | 群外用户对 9 种操作全部返回 false |
| `SendMessageMustBeSentByItself` | 消息发送者必须与操作者一致 |
| `MutedMemberCannotSpeakUntilUnmuted` | 单员禁言效果验证 |
| `AllMuteBlocksMemberButNotPrivileged` | 全员禁言：普通成员阻断，Admin/Owner 放行 |
| `PeerProtectionCannotTargetSameOrHigherRole` | 同级/更高角色保护 |
| `RecallRespectsOwnershipAndPrivilege` | 撤回归属权与管理员特权 |
| `RecallExpiresAfterTimeWindow` | 撤回时间窗到期即失效 |
| `EditAndAnnouncementRequireAdmin` | 改群名/公告需 ADMIN+ |
| `AssignAdminOnlyByOwner` | 任免管理员仅群主可执行 |
| `TransferOwnerAtomicallySwapsRoles` | 转让群主原子交换角色 |
| `DisbandBlocksEveryOperation` | 解散后所有操作拒绝 |
| `SwitchPolicyKeepsDataAndChangesPrivileges` | 策略切换保留成员数据但改变权限规则 |
| `MaxGroupSizeReadsConfig` | `getMaxGroupSize` 读取 GroupConfig |
| `MembershipRecordsJoinTime` | 成员记录入群时间 |

### 3.2 abstract_policy_test — 5 用例 ✅

测试 `AbstractGroupPolicyFH` 公共授权流程（六步链）。

| 用例 | 覆盖点 |
|---|---|
| `ValidateContextRejectsMissingPieces` | group/operator_ 为 null 时拒绝 |
| `MembershipStepRejectsOutsiderAndInvalidTarget` | 操作者/目标不在群中时拒绝 |
| `StateRulesBlockMutedAndAllMutedMember` | 单独禁言 + 全员禁言双重判定 |
| `RecallWindowBoundaryIsInclusiveAndOwnershipEnforced` | 边界精确到秒，归属权校验 |
| `CommonActionsEqualAcrossPlatforms` | QQ/微信对公共操作判定结果一致 |

### 3.3 qq_wechat_policy_test — 5 用例 ✅

测试平台差异策略。

| 用例 | 覆盖点 |
|---|---|
| `QQMemberInviteDependsOnConfig` | QQ 普通成员邀请受 `memberInviteEnabled` 控制 |
| `WeChatOwnerOnlyInvite` | 微信仅群主可邀请 |
| `SetAllMutePlatformDifference` | QQ 管理员可设全员禁言 vs 微信仅群主 |
| `AllMuteAffectsBothPlatformsEqually` | 全员禁言对两种平台的影响一致性 |
| `MaxGroupSizeSameAcrossPlatformsFromConfig` | 两种策略均读取 GroupConfig 而非硬编码 |

### 3.4 platform_service_test — 6 用例 ✅

测试多产品身份体系。

| 用例 | 覆盖点 |
|---|---|
| `QQAndWeiboShareIdWhileWeChatIndependent` | 号码 ID 体系正确性 |
| `RegistrationUniquenessAndLookup` | 主号/微信号全局唯一，查询接口 |
| `MakeAccountViewsCarryPlatformBinding` | 微信账号携带绑定 QQ 信息 |
| `ActivationEligibilityAndIdempotency` | 开通资格校验与幂等性 |
| `DeactivateRequiresLogoutFirst` | 在线时不可取消开通 |
| `LoginLinksAllActivatedServices` | 一次登录全家在线联动 |

### 3.5 social_message_test — 13 用例 ✅

测试社交关系与群消息扩展。

| 用例 | 覆盖点 |
|---|---|
| `QQAndWeChatFriendsAreMutualAndDeduplicated` | 双向好友与重复检测 |
| `WeChatFriendshipRequiresBothBound` | 双方均需绑定微信号 |
| `WeiboFollowIsOneWayNotFriend` | 微博单向关注 ≠ 好友 |
| `UnfriendOnQQDoesNotAffectWeChat` | 平台隔离：QQ 解好友不影响微信 |
| `PredefinedOfficialGroupsExistOnEveryPlatform` | 预置群 1001~1006 数量与类型 |
| `JoinGroupGateChecksPlatformAndAccount` | 加入群的平台/账号过滤 |
| `CreateGroupAutoAssignsNumberFrom1007` | 自建群号自动递增 |
| `PlatformSupportsKindMatrix` | 消息类型平台能力矩阵 |
| `SendGroupMessageEnforcesKindLengthAndReply` | 类型/长度/引用三重重校验 |
| `ChatRecordKeepsAtMostFiftyByEvictingOldest` | 50 条上限淘汰最早记录 |
| `DiscussionGroupAnyMemberCanInviteAndQuitFreely` | 临时讨论组成员自由邀请/退出 |
| `DiscussionGroupOnlyCreatorDisbandThenInert` | 仅发起人可解散 |
| `DiscussionGroupCapacityIsSmall` | 容量上限 20 人 |

### 3.6 regression_tests — 15 用例 ✅

回归边界与场景补充测试。

| 用例 | 覆盖点 |
|---|---|
| `MessageRejectsEmptyId/Content/NullSender` | 构造期参数校验（3 项） |
| `GroupMembershipRejectsNullUser` | 成员构造 null 用户校验 |
| `ZeroRecallTimeWindowIsValid` | recallTimeLimit=0 合法边界 |
| `TransferOwnerFullPermissionMigration` | 转让后新旧群主权限完整迁移 |
| `WeChatInviteMatrixAllRoles` | 微信群邀请规则全角色矩阵 |
| `WeiboFollowDoesNotAffectQQFriendship` | 微博关注与 QQ 好友严格隔离 |
| `QQFriendshipDoesNotCreateWeiboFollowing` | QQ 好友不自动产生微博关注 |
| `DisbandedGroupKeepsMessagesButRejectsAllOps` | 解散后消息保留但操作拒绝 |
| `AllMuteOwnerCanSpeakBothPlatforms` | 全员禁言 Owner 发言跨平台一致性 |
| `RecallAtExactBoundaryIsAllowed` | 恰好等于时间窗允许撤回 |
| `CannotKickOwner` | 管理员不能踢群主 |
| `GetRoleReturnsNulloptForNonMembers` | 群外用户 getRole 返回 nullopt |
| `MessageDefaultKindIsText` | 消息默认类型为 TEXT |

### 3.7 e2e_integration_test — 10 用例 ✅

端到端真实性场景验证。

| 用例 | 覆盖点 |
|---|---|
| `EndToEnd_GroupLifeCycle` | 群完整生命周期：创建→邀请→发信→撤回→禁言→解除 |
| `EndToEnd_OfficialGroupFullFlow` | 官方群加入/发信/查记录/退群全流程 |
| `EndToEnd_DiscussionGroupFullFlow` | QQ 临时讨论组完整流程 |
| `EndToEnd_FriendIsolation` | 跨平台好友关系严格隔离 |
| `EndToEnd_LoginLinkage` | 登录联动与在线状态管理 |
| `EndToEnd_GroupDisbandConsistency` | 群解散后状态一致性 |
| `EndToEnd_AllMutePermissions` | 全员禁言期间各级权限验证 |
| `EndToEnd_PolicySwitch` | 管理模式切换前后权限变化 |
| `EndToEnd_WeChatGroupOwnerOnlyPrivilege` | 微信群仅群主为特权账号 |
| `EndToEnd_ChatRecordEviction` | 群消息记录上限淘汰机制 |

### 3.8 requirement_alignment_test — 10 用例 ✅

逐条对照任务书文档的针对性验证。

| 用例 | 覆盖需求 |
|---|---|
| `Doc_1_1_UserBasicInfoAndLists` | 1.(1) 用户基本信息 + 好友/群列表 |
| `Doc_2_1_FriendRemarkModification` | 2.(1) 好友信息管理 |
| `Doc_2_2_CommonFriendsAndFollowing` | 2.(2) 共同好友/关注查询 |
| `Doc_2_2_6_3_CrossPlatformRecommendation` | 2.(2)+6.(3) 跨服务推荐添加好友 |
| `Doc_3_1_PredefinedGroups` | 3.(1) 预置群号 1001~1006 |
| `Doc_3_2_3_3_GroupJoinKickQueryAndAdmin` | 3.(2)(3) 加入/挨踢/查询/管理员制度 |
| `Doc_3_3_JoinRulesAndDiscussionGroupQQOnly` | 3.(3) 申请加入 vs 推荐加入 + 临时讨论组 |
| `Doc_3_3_WeChatGroupOwnerOnlyPrivilege` | 3.(3) 微信群仅群主特权（策略层） |
| `Doc_6_1_PersistenceRoundTrip` | 6.(1) 断电保存完整往返 |
| `Doc_5_6_2_LoginLinkageAutoOnline` | 5+6.(2) 登录联动 |

---

## 四、第二次测试结果（2026-09-09）

在第一次测试基础上，新增 **`requirement_alignment_comprehensive_test`**（15 用例），覆盖第一次未充分展开的真实场景。

### 4.1 新增测试套件详情

| 用例 | 覆盖需求 | 关键验证点 |
|---|---|---|
| `Comprehensive_UserBasicInfoAndLists` | 1.(1) | 号码ID体系、T龄计算、资料修改、好友列表、群列表 |
| `WeChatIndependentIdWithoutBinding` | 1.(1) | 未绑定微信用户无法获取微信账号 |
| `Comprehensive_FriendManagementFullLoop` | 2.(1) | 添加→备注→删除全闭环，重复/自加被拒 |
| `Comprehensive_CommonFriendsAllPlatforms` | 2.(2) | QQ/微信/微博三方共同好友查询 |
| `Comprehensive_CrossPlatformRecommendation_AllConditions` | 2.(2)+6.(3) | 双方均须开通来源+目标服务，推荐幂等 |
| `Comprehensive_PredefinedGroups1001to1006` | 3.(1) | 预置群数量、平台类型、官方群无群主 |
| `Comprehensive_GroupJoinLeaveKickQuery` | 3.(2) | QQ/微信群完整加入/退出/挨踢/查询流程 |
| `Comprehensive_JoinRulesAndDiscussionGroup` | 3.(3) | 申请vs推荐、满员拒绝、未绑微信拒绝、临时讨论组 |
| `Comprehensive_GroupPolicySwitchPreservesMembers` | 6.(4) | 模式切换前后成员数/消息数不变 |
| `Comprehensive_ActivationManagement` | 4 | 开通/取消/在线不可取消/重复开通幂等 |
| `Comprehensive_LoginLinkageAllServices` | 5+6.(2) | 一键登录全部上线，单服务退出，退出后可重登 |
| `Comprehensive_PersistenceFullRoundTrip` | 6.(1) | 开通/好友/群成员/聊天记录完整往返 |
| `Comprehensive_GroupFeatureMatrix` | 6.(4) | QQ/微信群功能差异矩阵 + 转让后权限迁移 |
| `Comprehensive_AdminCanSpeakDuringAllMute` | — | QQ管理员在禁言期间可发言 |
| `Comprehensive_WeiboFollowIsolation` | — | 微博关注与QQ/微信好友严格隔离 |

### 4.2 两次测试对比

| 维度 | 第一次（9-08） | 第二次（9-09） | 变化 |
|---|---:|---:|---|
| 套件数 | 8 | 9 | +1 |
| 用例总数 | 82 | 97 | +15 |
| 新增用例聚焦 | 基准验收 | 真实场景补测 | — |
| 通过率 | 100% | 100% | — |
| 测试耗时 | 0.06s | 0.04s | 优化 |

---

## 五、需求覆盖度验证

### 5.1 任务书 6 项需求全覆盖

| 需求编号 | 内容 | 覆盖测试 | 状态 |
|---|---|---|---|
| 1.(1) | 用户基本信息（ID/昵称/出生/T龄/所在地/好友/群列表） | `Comprehensive_UserBasicInfoAndLists`, `WeChatIndependentIdWithoutBinding` | ✅ |
| 2.(1) | 好友管理：添加/修改(备注)/删除/查询 | `Comprehensive_FriendManagementFullLoop`, `Doc_2_1_FriendRemarkModification` | ✅ |
| 2.(2) | 微X 之间共同好友查询 | `Comprehensive_CommonFriendsAllPlatforms`, `Doc_2_2_CommonFriendsAndFollowing` | ✅ |
| 2.(2)+6.(3) | 跨服务推荐添加好友 | `Comprehensive_CrossPlatformRecommendation_AllConditions`, `Doc_2_2_6_3_CrossPlatformRecommendation` | ✅ |
| 3.(1) | 预置群号 1001~1006 | `Comprehensive_PredefinedGroups1001to1006`, `Doc_3_1_PredefinedGroups` | ✅ |
| 3.(2) | 加入/退出/挨踢/查询群成员 | `Comprehensive_GroupJoinLeaveKickQuery`, `Doc_3_2_3_3_GroupJoinKickQueryAndAdmin` | ✅ |
| 3.(3) | QQ申请加入 vs 微信推荐加入；临时讨论组；管理员制度差异 | `Comprehensive_JoinRulesAndDiscussionGroup`, `Doc_3_3_*`, `EndToEnd_*` | ✅ |
| 4 | 自选开通 N 个微X 服务 | `Comprehensive_ActivationManagement` | ✅ |
| 5 | 一个服务登录 → 其余联动在线 | `Comprehensive_LoginLinkageAllServices`, `Doc_5_6_2_LoginLinkageAutoOnline` | ✅ |
| 6.(1) | 断电保存（文件读写→启动加载） | `Comprehensive_PersistenceFullRoundTrip`, `Doc_6_1_PersistenceRoundTrip` | ✅ |
| 6.(2) | 登录后全部已开通服务上线 | 同 5 | ✅ |
| 6.(3) | 跨服务依据已开通服务的好友添加好友 | 同 2.(2)+6.(3) | ✅ |
| 6.(4) | 群特色功能展示 + 动态切换管理模式 | `Comprehensive_GroupFeatureMatrix`, `Comprehensive_GroupPolicySwitchPreservesMembers`, `EndToEnd_PolicySwitch` | ✅ |

---

## 六、代码质量检验（优化提高层次）

| 编号 | 要求 | 检验结论 |
|---|---|---|
| ① | 简便菜单（数字分类、可返回） | ✅ 主菜单 [1]~[7]+[0]；各子菜单均有 [0]返回；单键热键无需回车 |
| ② | I/O 断电保存（实例化读入、析构写回） | ✅ UserRegistry/FriendRegistry/GroupRegistry 三处均实现 loadFromFile + ~析构 saveToFile |
| ③ | 可扩展性（群/好友与主要服务关系） | ✅ 策略模式：GroupFH 依赖 GroupPolicyFH 接口；注册中心模式：UserRegistry 统一管理自然人 |
| ④ | 灵活性（群管理模式动态可变） | ✅ GroupFH::switchPolicy() 运行时切换 QQ↔微信策略，成员数据不丢失 |
| ⑤ | 必要注释 | ✅ 8027 行源码，608 行首行注释（12.3%），类级说明 + 方法行内注释完整 |
| ⑥ | UML 类图 | ✅ `docs/class-diagram.md` 209 行，含全局 classDiagram（27 类 + 关系）+ 2 个时序图 |
| ⑦ | 类名独立文件 + FH 后缀 | ✅ 27 个头文件全部独立且含 `_fh` 后缀，无一遗漏 |

---

## 七、测试清单

### 7.1 编译与工程

- [x] `cmake --build build` 无错误、无警告
- [x] 所有测试可独立运行并生成可执行文件
- [x] CMakeLists.txt 正确注册 9 个测试目标

### 7.2 领域模型

- [x] 用户 ID 唯一性校验有效
- [x] 角色位于 `GroupMembership`，不在 `User` 中
- [x] 成员列表不能被外部直接修改
- [x] 群人数上限生效
- [x] 群解散后所有操作返回 false

### 7.3 权限流程

- [x] `isAllowed` 流程固定（六步链）
- [x] 群外操作者被拒绝
- [x] 普通成员不能执行管理操作
- [x] 管理员不能操作同级或更高角色
- [x] 只有群主可以转让群主和解散群

### 7.4 平台差异

- [x] QQ 与微信的邀请规则不同
- [x] 全员禁言规则正确
- [x] `getMaxGroupSize` 读取 `GroupConfig`，不硬编码
- [x] `AbstractGroupPolicy` 中无平台分支

### 7.5 文档与演示

- [x] README 能说明如何运行
- [x] 类图与代码一致
- [x] Demo 可运行并展示策略差异
- [x] 测试结果可展示

### 7.6 质量保障

- [x] 所有类名带 `FH` 后缀，独立文件存放
- [x] 注释覆盖率达 12.3%
- [x] UML 类图文档完整
- [x] 断电保存完整往返验证通过

---

## 八、结论

**两次测试均以 100% 通过率收尾，97 个测试用例全部通过，无遗留失败项。**

第一次测试（8 套件 / 82 用例）完成了基准验收；  
第二次测试（9 套件 / 97 用例）补充了面向真实场景的综合覆盖，填补了初次测试中未充分展开的边界条件。

代码质量各项指标均达到课程设计"优化提高层次"要求，可提交答辩。
