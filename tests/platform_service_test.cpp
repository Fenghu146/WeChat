// ============================================================
// platform_service_test.cpp —— 微X 多产品体系回归测试（阶段 B）
// 覆盖：QQ/微博同号、微信号独立绑定、注册唯一性、账号资料视图、
// 自选开通资格、在线不可取消、登录联动（一次登录全家在线）。
// ============================================================
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "fh_mini_test.hpp"
#include "im/platform/account_info_fh.hpp"
#include "im/platform/activation_manager_fh.hpp"
#include "im/platform/login_manager_fh.hpp"
#include "im/platform/platform_kind_fh.hpp"
#include "im/platform/user_profile_fh.hpp"
#include "im/platform/user_registry_fh.hpp"

namespace {

using std::shared_ptr;

struct People {
    UserRegistryFH reg;
    shared_ptr<UserProfileFH> xm;
    shared_ptr<UserProfileFH> hong;

    People() {
        xm = reg.registerUser("10001", "小明", "2006-01-01", "杭州", 2021);
        hong = reg.registerUser("10002", "小红", "2007-02-02", "北京", 2021);
        reg.bindWeChat(xm, "wx-88-0001");
    }
};

// QQ 与微博共享同一号码；微信独立可绑定
FH_TEST(QQAndWeiboShareIdWhileWeChatIndependent) {
    People p;
    FH_CHECK_EQ(p.xm->getQQId(), std::string("10001"));
    FH_CHECK_EQ(p.xm->getWeiboId(), std::string("10001"));  // 共享主号
    FH_CHECK_EQ(p.xm->platformAccountId(PlatformKindFH::QQ), std::string("10001"));
    FH_CHECK_EQ(p.xm->platformAccountId(PlatformKindFH::Weibo), std::string("10001"));
    FH_CHECK_EQ(p.xm->platformAccountId(PlatformKindFH::WeChat),
                std::string("wx-88-0001"));  // 微信号独立
    FH_CHECK(p.xm->hasPlatformAccount(PlatformKindFH::QQ));
    FH_CHECK(p.xm->hasPlatformAccount(PlatformKindFH::WeChat));
    FH_CHECK(!p.hong->hasPlatformAccount(PlatformKindFH::WeChat));  // 未绑定
}

// 注册唯一性 / 微信号全局唯一 / 查询
FH_TEST(RegistrationUniquenessAndLookup) {
    UserRegistryFH reg;
    reg.registerUser("10001", "一号", "2000-01-01", "上海", 2020);  // 先占用主号
    bool threw = false;
    try {
        reg.registerUser("10001", "重名者", "2000-01-01", "上海", 2020);
    } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);  // QQ 主号重复注册被拒

    auto a = reg.registerUser("10009", "阿九", "2000-01-01", "上海", 2020);
    FH_CHECK(reg.bindWeChat(a, "wx-88-0009"));
    FH_CHECK(!reg.bindWeChat(a, "wx-88-000A"));       // 每人至多绑定一个微信号
    auto b = reg.registerUser("10010", "阿十", "2000-02-02", "广州", 2021);
    FH_CHECK(!reg.bindWeChat(b, "wx-88-0009"));       // 微信号全局唯一
    FH_CHECK_EQ(reg.findByQQId("10009"), a);
    FH_CHECK_EQ(reg.findByWeChatId("wx-88-0009"), a);
    FH_CHECK(!reg.findByQQId("no-such"));
}

// 账号资料视图：微信账号自带“绑定 QQ”归属
FH_TEST(MakeAccountViewsCarryPlatformBinding) {
    People p;
    auto qqAcc = p.reg.makeAccount(*p.xm, PlatformKindFH::QQ);
    FH_CHECK_EQ(qqAcc.getAccountId(), std::string("10001"));
    FH_CHECK(!qqAcc.hasBindQQ());  // 非微信平台不可绑定 QQ
    auto wxAcc = p.reg.makeAccount(*p.xm, PlatformKindFH::WeChat);
    FH_CHECK_EQ(wxAcc.getAccountId(), std::string("wx-88-0001"));
    FH_CHECK(wxAcc.hasBindQQ());
    FH_CHECK_EQ(wxAcc.getBindQqId(), std::string("10001"));
    // 未绑定微信的自然人生成微信账号视图应抛异常
    bool threw = false;
    try {
        p.reg.makeAccount(*p.hong, PlatformKindFH::WeChat);
    } catch (const std::invalid_argument&) { threw = true; }
    FH_CHECK(threw);
}

// 自选开通：QQ/微博注册即有资格；微信须先绑定；重复开通幂等拒绝
FH_TEST(ActivationEligibilityAndIdempotency) {
    People p;
    ActivationManagerFH act;
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::QQ));      // 注册即有主号
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::Weibo));   // 与 QQ 同号
    FH_CHECK(!act.activate(*p.xm, PlatformKindFH::QQ));     // 重复开通拒绝
    FH_CHECK(!act.activate(*p.hong, PlatformKindFH::WeChat));  // 微信未绑定：无资格
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::WeChat));     // 已绑定 wx：可开通
    FH_CHECK_EQ(act.activatedCount(*p.xm), 3);
    FH_CHECK_EQ(act.activatedCount(*p.hong), 0);
}

// 取消开通：在线服务必须先退出登录
FH_TEST(DeactivateRequiresLogoutFirst) {
    People p;
    ActivationManagerFH act;
    LoginManagerFH login;
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(login.login(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(!act.deactivate(*p.xm, PlatformKindFH::QQ));  // 在线不可取消
    FH_CHECK(login.logout(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(act.deactivate(*p.xm, PlatformKindFH::QQ));   // 退出后可取消
    FH_CHECK(!act.deactivate(*p.xm, PlatformKindFH::QQ));  // 未开通不可再取消
}

// 登录联动：一次登录，全部已开通服务上线；退出为单服务操作
FH_TEST(LoginLinksAllActivatedServices) {
    People p;
    ActivationManagerFH act;
    LoginManagerFH login;
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::Weibo));
    FH_CHECK(act.activate(*p.xm, PlatformKindFH::WeChat));
    // 未开通的服务不能登录
    FH_CHECK(!login.login(*p.hong, PlatformKindFH::QQ));
    FH_CHECK(login.login(*p.xm, PlatformKindFH::QQ));  // 登录 QQ
    FH_CHECK(login.isOnline(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*p.xm, PlatformKindFH::Weibo));   // 联动上线
    FH_CHECK(login.isOnline(*p.xm, PlatformKindFH::WeChat));  // 联动上线
    FH_CHECK_EQ(login.onlinePlatforms(*p.xm).size(), std::size_t(3));
    // 退出只影响单个服务
    FH_CHECK(login.logout(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(!login.isOnline(*p.xm, PlatformKindFH::QQ));
    FH_CHECK(login.isOnline(*p.xm, PlatformKindFH::Weibo));   // 其余保持在线
    login.logoutAll(*p.xm);  // 退出全部服务
    FH_CHECK_EQ(login.onlinePlatforms(*p.xm).size(), std::size_t(0));
}

}  // namespace

int main() { return ::fhtest::runAll("platform-service"); }
