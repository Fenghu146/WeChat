#pragma once
// ============================================================
// FH 迷你单元测试框架（阶段 E · 自包含 · 零第三方依赖）
// ------------------------------------------------------------
// 为真实项目迁移准备的自动化回归层：不下载 GoogleTest 也能在
// 离线课程环境一键运行（cmake --build build && ctest）。
// 约定：每个测试翻译单元 = 一个可执行程序；用例经 FH_TEST 宏
// 注册，断言失败时打印 文件:行 与表达式并累计，最终由 main()
// 返回失败个数相关的退出码（0 = 全部通过）。
// 后续接入 GoogleTest 时，仅需把断言宏替换为 EXPECT_* 语义即可。
// ============================================================
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace fhtest {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int& failCounter() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

// —— 断言失败信息的“值转字符串” ——
namespace detail {
template <typename T, typename = void>
struct IsStreamable : std::false_type {};
template <typename T>
struct IsStreamable<
    T, std::void_t<decltype(std::declval<std::ostringstream&>()
                            << std::declval<const T&>())>> : std::true_type {};
}  // namespace detail

// 可打印类型输出到 ostringstream；不可打印类型给出占位说明
// （枚举等可比较但无 operator<< 的类型走这里，避免编译失败）
template <typename T>
inline std::string toStr(const T& v) {
    if constexpr (detail::IsStreamable<T>::value) {
        std::ostringstream os;
        os << v;
        return os.str();
    } else {
        (void)v;
        return "(不可打印类型)";
    }
}

inline int runAll(const char* suite) {
    int passed = 0;
    int failed = 0;
    std::printf("==== 测试套件 [%s]：共 %zu 个用例 ====\n", suite,
                registry().size());
    for (const auto& t : registry()) {
        failCounter() = 0;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++failCounter();
            std::printf("    未捕获异常: %s\n", e.what());
        }
        if (failCounter() > 0) {
            ++failed;
            std::printf("[FAIL] %s（%d 处断言失败）\n", t.name, failCounter());
        } else {
            ++passed;
            std::printf("[PASS] %s\n", t.name);
        }
    }
    std::printf("---- [%s] 结果：通过 %d / 失败 %d ----\n", suite, passed,
                failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace fhtest

// 注册一个测试用例
#define FH_TEST(name)                                                        \
    static void fh_test_body_##name();                                       \
    static ::fhtest::Registrar fh_test_reg_##name(#name, &fh_test_body_##name); \
    static void fh_test_body_##name()

// 布尔断言：失败打印文件/行/表达式
#define FH_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++::fhtest::failCounter();                                       \
            std::printf("    断言失败 %s:%d: %s\n", __FILE__, __LINE__,      \
                        #cond);                                              \
        }                                                                    \
    } while (0)

// 相等断言：失败时额外打印两侧实际值
#define FH_CHECK_EQ(a, b)                                                    \
    do {                                                                     \
        const auto fh_a = (a);                                               \
        const auto fh_b = (b);                                               \
        if (!(fh_a == fh_b)) {                                               \
            ++::fhtest::failCounter();                                       \
            std::printf("    断言失败 %s:%d: %s == %s （实际 %s vs %s）\n",  \
                        __FILE__, __LINE__, #a, #b,                          \
                        ::fhtest::toStr(fh_a).c_str(),                       \
                        ::fhtest::toStr(fh_b).c_str());                      \
        }                                                                    \
    } while (0)
