#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::test::jtx {

class AMM;

enum class Fund { All, Acct, Gw, TokenOnly };

struct TestAMMArg
{
    std::optional<std::pair<STAmount, STAmount>> pool = std::nullopt;
    std::uint16_t tfee = 0;
    std::optional<jtx::Ter> ter = std::nullopt;
    std::vector<FeatureBitset> features = {
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas
        jtx::testableAmendments() - featureSingleAssetVault - featureLendingProtocol};

    bool noLog = false;
};

// A hint to testAMM() or fund() to create/fund MPT.
// A distinct MPT is created if both AMM assets
// are MPT. The actual MPT asset can be accessed
// via AMM::operator[](0|1).
inline static auto gAmmmpt = MPT("AMM");

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
    std::optional<jtx::Ter> const& ter = std::nullopt;
    std::vector<FeatureBitset> const& features = {testableAmendments()};
};

class AMMTestBase : public beast::unit_test::Suite
{
protected:
    jtx::Account const gw_;
    jtx::Account const carol_;
    jtx::Account const alice_;
    jtx::Account const bob_;
    jtx::IOU const USD;  // NOLINT(readability-identifier-naming)
    jtx::IOU const EUR;  // NOLINT(readability-identifier-naming)
    jtx::IOU const GBP;  // NOLINT(readability-identifier-naming)
    jtx::IOU const BTC;  // NOLINT(readability-identifier-naming)
    jtx::IOU const BAD;  // NOLINT(readability-identifier-naming)

public:
    AMMTestBase();

    static FeatureBitset
    testableAmendments()
    {
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas
        return jtx::testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
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
        std::optional<jtx::Ter> const& ter = std::nullopt,
        std::vector<FeatureBitset> const& features = {testableAmendments()});

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
    class Gate
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
        waitFor(std::chrono::duration<Rep, Period> const& relTime)
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto b = cv_.wait_for(lk, relTime, [this] { return signaled_; });
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
