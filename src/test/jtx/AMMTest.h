#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>

namespace xrpl::test::jtx {

class AMM;

enum class Fund { All, Acct, Gw, TokenOnly };

struct TestAMMArg
{
    std::optional<std::pair<STAmount, STAmount>> pool = std::nullopt;
    std::uint16_t tfee = 0;
    std::optional<jtx::ter> ter = std::nullopt;
    std::vector<FeatureBitset> features = {
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas
        jtx::testable_amendments() - featureSingleAssetVault - featureLendingProtocol};

    bool noLog = false;
};

// A hint to testAMM() or fund() to create/fund MPT.
// A distinct MPT is created if both AMM assets
// are MPT. The actual MPT asset can be accessed
// via AMM::operator[](0|1).
inline static auto AMMMPT = MPT("AMM");

[[maybe_unused]] std::vector<STAmount>
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    std::vector<STAmount> const& amts,
    Fund how);

[[maybe_unused]] std::vector<STAmount>
fund(
    jtx::Env& env,
    jtx::Account const& gw,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts = {},
    Fund how = Fund::All);

[[maybe_unused]] std::vector<STAmount>
fund(
    jtx::Env& env,
    std::vector<jtx::Account> const& accounts,
    STAmount const& xrp,
    std::vector<STAmount> const& amts = {},
    Fund how = Fund::All,
    std::optional<Account> const& mptIssuer = std::nullopt);

struct TestAMMArgs
{
    std::optional<std::pair<STAmount, STAmount>> const& pool = std::nullopt;
    std::uint16_t tfee = 0;
    std::optional<jtx::ter> const& ter = std::nullopt;
    std::vector<FeatureBitset> const& features = {testable_amendments()};
};

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

    static FeatureBitset
    testable_amendments()
    {
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas
        return jtx::testable_amendments() - featureSingleAssetVault - featureLendingProtocol;
    }

protected:
    /** testAMM() funds 30,000XRP and 30,000IOU
     * for each non-XRP asset to Alice and Carol
     */
    void
    testAMM(
        std::function<void(jtx::AMM&, jtx::Env&)> const& cb,
        std::optional<std::pair<STAmount, STAmount>> const& pool = std::nullopt,
        std::uint16_t tfee = 0,
        std::optional<jtx::ter> const& ter = std::nullopt,
        std::vector<FeatureBitset> const& features = {testable_amendments()});

    void
    testAMM(std::function<void(jtx::AMM&, jtx::Env&)> const& cb, TestAMMArg const& arg);
};

class AMMTest : public jtx::AMMTestBase
{
protected:
    static XRPAmount
    reserve(jtx::Env& env, std::uint32_t count);

    static XRPAmount
    ammCrtFee(jtx::Env& env);

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
            std::scoped_lock const lk(mutex_);
            signaled_ = true;
            cv_.notify_all();
        }
    };

    jtx::Env
    pathTestEnv();
};

}  // namespace xrpl::test::jtx
