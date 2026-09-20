// ============================================================
// demo_flow_smoke_test.cpp —— 演示程序全流程冒烟测试（阶段 E 增补）
// ------------------------------------------------------------
// 单元测试验证的是库的接口，本用例验证“真正跑起来的那条路径”：
// 直接执行 demo_fh --demo（A→D 全部自动演示 + 自检汇总），并检查
//   1) 进程正常退出（0）；
//   2) 自检汇总报告“全部符合预期”（演示内含 60+ 项校验，其中一部分
//      标注为「应失败」的平台规则/权限校验）；
//   3) stdin 关闭（管道/重定向读完）时手动测试工作台能正常退出，
//      不会出现读空输入死循环刷屏。
// 运行：ctest --test-dir build --output-on-failure
// ============================================================
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "fh_mini_test.hpp"

namespace {

// 演示程序可执行文件（ctest 的工作目录即构建目录）
std::string demoExe() {
#ifdef _WIN32
    return "demo_fh.exe";
#else
    return "./demo_fh";
#endif
}

// 空输入重定向：Windows 用 NUL，类 Unix 用 /dev/null
std::string nullDevice() {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
}

std::string readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

FH_TEST(DemoBinaryRunsAllScenariosAndSelfCheckPasses) {
    const std::string out = "fh_demo_smoke_output.txt";
    std::remove(out.c_str());

    const std::string cmd = demoExe() + " --demo < " + nullDevice() +
                            " > " + out + " 2>&1";
    const int rc = std::system(cmd.c_str());
    FH_CHECK_EQ(rc, 0);  // 演示 + 工作台在输入结束时正常退出

    const std::string log = readAll(out);
    FH_CHECK(!log.empty());

    // 四个阶段的演示都跑到
    FH_CHECK(contains(log, "阶段 A 自动演示结束"));
    FH_CHECK(contains(log, "阶段 B 自动演示结束"));
    FH_CHECK(contains(log, "阶段 C 自动演示结束"));
    FH_CHECK(contains(log, "阶段 D 自动演示结束"));

    // 演示自检：全部校验（含「应失败」项）与预期一致
    FH_CHECK(contains(log, "演示自检汇总"));
    FH_CHECK(contains(log, "自检结果：全部符合预期"));
    FH_CHECK(contains(log, "应失败"));

    // 关键平台差异场景在真实运行路径上确实被判为失败（即校验生效）
    FH_CHECK(contains(log, "微信群只能推荐加入"));
    FH_CHECK(contains(log, "微信群仅群主可推荐加入"));

    // 进入手动测试工作台后，stdin 结束即退出，不得死循环
    FH_CHECK(contains(log, "手动测试工作台"));
    FH_CHECK(log.size() < 200000);  // 正常日志约 12KB；刷屏会轻易超过该量级

    std::remove(out.c_str());
}

}  // namespace

int main() { return ::fhtest::runAll("demo-flow-smoke"); }
