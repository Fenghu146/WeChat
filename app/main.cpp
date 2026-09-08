// ============================================================
// 控制台演示程序（作者代号：FH）
// 流程：
//   1) 启动即自动运行一遍“QQ 群 vs 微信群”完整核心流程演示；
//   2) 之后进入交互菜单，可反复体验各项操作。
// 编译：cmake --build build 后运行 demo_fh。
// ============================================================
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "im/model/group_fh.hpp"
#include "im/model/group_role_fh.hpp"
#include "im/model/message_fh.hpp"
#include "im/model/user_fh.hpp"
#include "im/policy/qq_policy_fh.hpp"
#include "im/policy/wechat_policy_fh.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Windows 控制台按 UTF-8 输出，避免中文乱码
void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

const char* ok(bool value) { return value ? "成功" : "失败"; }

unsigned g_messageSeq = 0;

// 生成带自增 ID 的消息
std::shared_ptr<MessageFH> makeMessage(const std::shared_ptr<UserFH>& sender,
                                       const std::string& content) {
    return std::make_shared<MessageFH>("m" + std::to_string(++g_messageSeq),
                                       sender, content);
}

void printMembers(const std::string& title, const GroupFH& group) {
    std::cout << "  [" << title << "] " << group.getName()
              << "（群号 " << group.getGroupNumber() << "），成员数 "
              << group.members().size() << "：\n";
    for (const auto& kv : group.members()) {
        const auto& m = kv.second;
        std::cout << "     - " << m.getUser()->getNickname()
                  << "（ID " << m.getUser()->getId() << "）角色："
                  << toZhName(m.getRole())
                  << (m.isMuted() ? " ［被禁言］" : "") << "\n";
    }
}

void printMessages(const std::string& title, const GroupFH& group) {
    std::cout << "  [" << title << "] 消息记录 " << group.messages().size()
              << " 条：\n";
    for (const auto& msg : group.messages()) {
        std::cout << "     - " << msg->getSender()->getNickname() << "："
                  << msg->getContent()
                  << (msg->isRecalled() ? " ［已撤回］" : "") << "\n";
    }
}

// 自动核心流程演示：用独立的数据，顺序展示全部关键规则
void runAutoScenario() {
    std::cout << "\n=========== 自动演示：QQ 群 vs 微信群（核心流程） ===========\n";

    auto owner    = std::make_shared<UserFH>("10001", "群主本人");
    auto admin    = std::make_shared<UserFH>("10002", "小管理");
    auto member   = std::make_shared<UserFH>("10003", "普通成员");
    auto outsider = std::make_shared<UserFH>("10004", "路人甲");

    // 同样的用户、同样的配置，只是群策略不同
    auto qqPolicy = std::make_shared<QQPolicyFH>();
    GroupFH qqGroup("g-qq-001", 1001, "C++ 学习群",
                    GroupConfigFH(50, true, false, std::chrono::seconds(120)),
                    qqPolicy, owner);
    GroupFH wxGroup("g-wx-002", 1002, "周末爬山群",
                    GroupConfigFH(50, true, false, std::chrono::seconds(120)),
                    std::make_shared<WeChatPolicyFH>(), owner);

    std::cout << "[1] 群主邀请成员入群，并分别任命管理员：\n";
    std::cout << "    QQ 邀请小管理 / 普通成员："
              << ok(qqGroup.inviteMember(owner, admin)) << " / "
              << ok(qqGroup.inviteMember(owner, member)) << "\n";
    std::cout << "    微信邀请小管理 / 普通成员："
              << ok(wxGroup.inviteMember(owner, admin)) << " / "
              << ok(wxGroup.inviteMember(owner, member)) << "\n";
    std::cout << "    QQ 任命小管理为管理员："
              << ok(qqGroup.setAdmin(owner, admin, true)) << "\n";
    std::cout << "    微信任命小管理为管理员："
              << ok(wxGroup.setAdmin(owner, admin, true)) << "\n";

    std::cout << "\n[2] 邀请规则差异：\n";
    std::cout << "    QQ 普通成员邀请路人甲（开关开启）："
              << ok(qqGroup.inviteMember(member, outsider)) << "\n";
    std::cout << "    微信普通成员邀请路人甲：" << ok(wxGroup.inviteMember(member, outsider))
              << "（应失败，微信仅 ADMIN+ 可邀请）\n";
    std::cout << "    微信管理员邀请路人甲：" << ok(wxGroup.inviteMember(admin, outsider))
              << "\n";
    std::cout << "    路人甲再次加入 QQ 群（成员唯一）："
              << ok(qqGroup.inviteMember(owner, outsider)) << "（应失败）\n";

    std::cout << "\n[3] 全员禁言设置差异：\n";
    std::cout << "    QQ 管理员设置全员禁言：" << ok(qqGroup.setAllMute(admin, true)) << "\n";
    std::cout << "    微信管理员设置全员禁言：" << ok(wxGroup.setAllMute(admin, true))
              << "（应失败，仅群主）\n";
    std::cout << "    微信群主设置全员禁言：" << ok(wxGroup.setAllMute(owner, true)) << "\n";

    std::cout << "\n[4] 全员禁言下的发言限制：\n";
    std::cout << "    QQ 普通成员发言：" << ok(qqGroup.sendMessage(
                  member, makeMessage(member, "禁言期间的发言")))
              << "（应失败）\n";
    std::cout << "    QQ 群主发言：" << ok(qqGroup.sendMessage(
                  owner, makeMessage(owner, "群主不受禁言影响")))
              << "\n";
    qqGroup.setAllMute(owner, false);
    wxGroup.setAllMute(owner, false);
    std::cout << "    （两群随后由群主解除全员禁言）\n";

    std::cout << "\n[5] 动态切换群管理模式（官方：成员数据不受伤害）：\n";
    std::cout << "    QQ 群切到“微信模式”后，管理员设全员禁言："
              << ok(qqGroup.switchPolicy(std::make_shared<WeChatPolicyFH>()))
              << "，执行结果：" << ok(qqGroup.setAllMute(admin, true))
              << "（应失败）\n";
    std::cout << "    QQ 群切回“QQ 模式”后，管理员设全员禁言："
              << ok(qqGroup.switchPolicy(qqPolicy)) << "，执行结果："
              << ok(qqGroup.setAllMute(admin, true)) << "（应成功）\n";
    qqGroup.setAllMute(owner, false);
    std::cout << "    切换前后群成员数不变：" << qqGroup.members().size()
              << "（成员数据未受伤害）\n";

    std::cout << "\n[6] 撤回消息（归属 + 时间窗）：\n";
    auto msgByMember = makeMessage(member, "大家好呀");
    auto msgByOwner = makeMessage(owner, "这是群主的消息");
    qqGroup.sendMessage(member, msgByMember);
    qqGroup.sendMessage(owner, msgByOwner);
    std::cout << "    普通成员撤回自己的消息："
              << ok(qqGroup.recallMessage(member, msgByMember->getId())) << "\n";
    std::cout << "    普通成员撤回群主的消息："
              << ok(qqGroup.recallMessage(member, msgByOwner->getId()))
              << "（应失败，只能撤回本人消息）\n";
    std::cout << "    群主撤回自己的消息："
              << ok(qqGroup.recallMessage(owner, msgByOwner->getId())) << "\n";

    std::cout << "\n[7] 任命管理员与转让群主：\n";
    std::cout << "    群主任命普通成员为管理员："
              << ok(qqGroup.setAdmin(owner, member, true)) << "\n";
    std::cout << "    普通成员（已管理员）无权再任命别人："
              << ok(qqGroup.setAdmin(member, outsider, true)) << "（应失败）\n";
    std::cout << "    群主将群转让给原管理员："
              << ok(qqGroup.transferOwner(owner, admin)) << "\n";

    printMembers("QQ 群", qqGroup);
    printMembers("微信群", wxGroup);
    printMessages("QQ 群", qqGroup);
    std::cout << "=========== 自动演示结束 ===========\n";
}

// 交互菜单演示：使用独立于自动演示的“手动演示群”
int runInteractiveMenu() {
    auto mOwner   = std::make_shared<UserFH>("20001", "手动群主");
    auto mAdmin   = std::make_shared<UserFH>("20002", "手动管理员");
    auto mMember  = std::make_shared<UserFH>("20003", "手动成员");
    auto mOutsider = std::make_shared<UserFH>("20004", "手动路人");

    auto qqGroup = std::make_shared<GroupFH>(
        "g-qq-101", 1003, "手动演示群",
        GroupConfigFH(30, true, false, std::chrono::seconds(120)),
        std::make_shared<QQPolicyFH>(), mOwner);
    qqGroup->inviteMember(mOwner, mAdmin);
    qqGroup->inviteMember(mOwner, mMember);
    qqGroup->setAdmin(mOwner, mAdmin, true);  // 任命为管理员

    int choice = -1;
    while (choice != 0) {
        std::cout << "\n================ 交互菜单 ================\n"
                  << " 1. 普通成员发送一条消息（QQ 群）\n"
                  << " 2. 查看两个群的成员与消息\n"
                  << " 3. 普通成员邀请路人（QQ/微信对比演示）\n"
                  << " 4. 管理员设置全员禁言后普通成员发言\n"
                  << " 5. 动态切换管理模式（切到微信模式再切回）\n"
                  << " 6. 群主解散“手动演示群”并验证不可操作\n"
                  << " 0. 退出\n"
                  << " 请选择：";
        std::cin >> choice;
        if (!std::cin) {  // 处理非数字输入
            std::cin.clear();
            std::cin.ignore(1024, '\n');
            choice = -1;
        }

        switch (choice) {
        case 1: {
            const std::string text = "来自普通成员的消息";
            auto msg = makeMessage(mMember, text);
            std::cout << "  发送结果：" << ok(qqGroup->sendMessage(mMember, msg)) << "\n";
            break;
        }
        case 2:
            printMembers("手动 QQ 群", *qqGroup);
            printMessages("手动 QQ 群", *qqGroup);
            break;
        case 3: {
            std::cout << "  QQ 群普通成员邀请路人："
                      << ok(qqGroup->inviteMember(mMember, mOutsider))
                      << "（QQ 开关开启，应成功）\n";
            auto wx = std::make_shared<GroupFH>(
                "g-wx-102", 1004, "手动微信群",
                GroupConfigFH(30, true, false, std::chrono::seconds(120)),
                std::make_shared<WeChatPolicyFH>(), mOwner);
            wx->inviteMember(mOwner, mMember);
            std::cout << "  微信群普通成员邀请路人："
                      << ok(wx->inviteMember(mMember, mOutsider))
                      << "（应失败，仅 ADMIN+）\n";
            break;
        }
        case 4: {
            std::cout << "  管理员设置全员禁言："
                      << ok(qqGroup->setAllMute(mAdmin, true)) << "\n";
            std::cout << "  普通成员发言："
                      << ok(qqGroup->sendMessage(mMember,
                              makeMessage(mMember, "全员禁言中的尝试")))
                      << "（应失败）\n";
            std::cout << "  群主解除全员禁言："
                      << ok(qqGroup->setAllMute(mOwner, false)) << "\n";
            break;
        }
        case 5: {
            std::cout << "  切到微信模式："
                      << ok(qqGroup->switchPolicy(std::make_shared<WeChatPolicyFH>()))
                      << "；管理员设全员禁言："
                      << ok(qqGroup->setAllMute(mAdmin, true)) << "（应失败）\n";
            std::cout << "  切回 QQ 模式："
                      << ok(qqGroup->switchPolicy(std::make_shared<QQPolicyFH>()))
                      << "；管理员设全员禁言："
                      << ok(qqGroup->setAllMute(mAdmin, true)) << "（应成功）\n";
            qqGroup->setAllMute(mOwner, false);
            break;
        }
        case 6: {
            std::cout << "  群主解散："
                      << ok(qqGroup->disband(mOwner)) << "\n";
            std::cout << "  解散后发送消息："
                      << ok(qqGroup->sendMessage(mMember,
                              makeMessage(mMember, "解散后的消息")))
                      << "（应失败）\n";
            std::cout << "  解散后成员数：" << qqGroup->members().size() << "\n";
            break;
        }
        case 0:
            std::cout << "  再见！\n";
            break;
        default:
            std::cout << "  无效选择，请重新输入。\n";
            break;
        }
    }
    return 0;
}

}  // namespace

int main() {
    initConsole();
    std::cout << "==== 模拟即时通信平台：QQ 群 / 微信群管理（作者代号 FH） ====\n";
    runAutoScenario();
    return runInteractiveMenu();
}
