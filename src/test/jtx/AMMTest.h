#ifndef XRPL_TEST_JTX_AMMTEST_H_INCLUDED
#define XRPL_TEST_JTX_AMMTEST_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace test {
namespace jtx {

class AMM;

enum class Fund { All, Acct, Gw, IOUOnly };

struct TestAMMArg
{
    std::optional<std::pair<STAmount, STAmount>> pool = std::nullopt;
    std::uint16_t tfee = 0;
    std::optional<jtx::ter> ter = std::nullopt;
    std::vector<FeatureBitset> features = {testable_amendments()};
    bool noLog = false;
};

void
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    std::vector<STAmount> const& amts,
    Fund how);

void
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts = {},
    Fund how = Fund::All);

void
fund(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts = {},
    Fund how = Fund::All);

class AMMTestBase : public beast::unit_test::suite
{
protected:
    jtx::Account const gw;
    jtx::Account const carol;
    jtx::Account const alice;
    jtx::Account const bob;
    jtx::IOU const USD;
    jtx::IOU const EUR;
    jtx::IOU const GBP;
    jtx::IOU const BTC;
    jtx::IOU const BAD;

public:
    AMMTestBase();

protected:
    /** testAMM() funds 30,000XRP and 30,000IOU
     * for each non-XRP asset to Alice and Carol
     */
    void
    testAMM(
        std::function<void(jtx::AMM&, jtx::Env&)>&& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = std::nullopt,
        std::uint16_t tfee = 0,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::vector<FeatureBitset> const& features = {testable_amendments()});

    void
    testAMM(
        std::function<void(jtx::AMM&, jtx::Env&)>&& cb,
        TestAMMArg const& arg);
};

class AMMTest : public jtx::AMMTestBase
{
protected:
    XRPAmount
    reserve(jtx::Env& env, std::uint32_t count) const;

    XRPAmount
    ammCrtFee(jtx::Env& env) const;

    /* Path_test */
    /************************************************/
    class gate
    {
    private:
        std::condition_variable cv_;
        std::mutex mutex_;
        bool signaled_ = false;

    public:
        // Thread safe, blocks until signaled or period expires.
        // Returns `true` if signaled.
        template <class Rep, class Period>
        bool
        wait_for(std::chrono::duration<Rep, Period> const& rel_time)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, rel_time, [this] { return signaled_; });
            signaled_ = false;
            return b;
        }

        void
        signal()
        {
            std::lock_guard lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    jtx::Env
    pathTestEnv();

    Json::Value
    find_paths_request(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt);

    std::tuple<STPathSet, STAmount, STAmount>
    find_paths(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        STAmount const& saDstAmount,
        std::optional<STAmount> const& saSendMax = std::nullopt,
        std::optional<Currency> const& saSrcCurrency = std::nullopt);
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // XRPL_TEST_JTX_AMMTEST_H_INCLUDED
