#include <test/app/vault/VaultPrecisionFixture.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace xrpl::test {

// With fixCleanup3_4_0, deposit/withdraw/clawback apply one amount on the
// sfAssetsTotal grid. These tests require T, A, and the pseudo-account to
// change by the same Number; the invariant suite still allows a one-unit gap.
class VaultTransactorPrecision_test : public VaultPrecisionFixture
{
    jtx::Env
    makeEnv()
    {
        return jtx::Env{*this, jtx::envconfig(), all_, nullptr, beast::Severity::Disabled};
    }

    bool
    ready(Fixture const& f)
    {
        return BEAST_EXPECT(f.asset && f.broker) && f.asset;
    }

    void
    assertEqualDeltas(Numbers const& before, Numbers const& after, std::string const& tag)
    {
        Number const tDelta = before.assetsTotal - after.assetsTotal;
        Number const aDelta = before.assetsAvailable - after.assetsAvailable;
        Number const pDelta = before.pseudo - after.pseudo;
        BEAST_EXPECTS(tDelta == aDelta, tag + " tDelta != aDelta");
        BEAST_EXPECTS(tDelta == pDelta, tag + " tDelta != pDelta");
    }

    void
    testDeposit()
    {
        using namespace jtx;

        testcase("deposit clamp does not over-credit");

        std::array<int, 4> const kAmounts{1, 7, 1'000, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env = makeEnv();
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!ready(f))
                continue;
            // ready() above guarantees f.asset is engaged; the guard is opaque to clang-tidy.
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            jtx::PrettyAsset const& asset = f.asset.value();

            auto const before = read(env, f);

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            if (env.ter() != tesSUCCESS)
                continue;

            auto const after = read(env, f);
            Number const tDelta = after.assetsTotal - before.assetsTotal;
            Number const requested = asset(amount).number();
            BEAST_EXPECTS(
                tDelta <= requested,
                "amount=" + std::to_string(amount) + " tDelta exceeds requested");

            Number const sharesMinted = after.sharesTotal - before.sharesTotal;
            if (before.sharesTotal == Number{0})
                continue;
            Number const shareValue = (before.assetsTotal * sharesMinted) / before.sharesTotal;
            BEAST_EXPECTS(
                shareValue <= tDelta,
                "amount=" + std::to_string(amount) + " shareValue > assetsTaken");
        }

        {
            Env env = makeEnv();
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!ready(f))
                return;
            // ready() above guarantees f.asset is engaged; the guard is opaque to clang-tidy.
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            jtx::PrettyAsset const& asset = f.asset.value();

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(99'000'000).value()}),
                Ter(std::ignore));
            env.close();

            auto const before = read(env, f);
            Number const kLowerBound{1, 6};
            BEAST_EXPECT(before.assetsTotal > kLowerBound);

            auto const tinyAmount = asset(Number{1, -10}).value();
            env(v.deposit(
                    {.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = tinyAmount}),
                Ter(std::ignore));
            env.close();

            BEAST_EXPECTS(
                env.ter() == tecPRECISION_LOSS,
                std::string{"expected tecPRECISION_LOSS, got "} + transToken(env.ter()));

            auto const after = read(env, f);
            BEAST_EXPECT(after.assetsTotal == before.assetsTotal);
            BEAST_EXPECT(after.assetsAvailable == before.assetsAvailable);
            BEAST_EXPECT(after.sharesTotal == before.sharesTotal);
        }
    }

    void
    testWithdraw()
    {
        using namespace jtx;

        testcase("withdraw deltas are equal");

        Env env = makeEnv();
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!ready(f))
            return;
        // ready() above guarantees f.asset is engaged; the guard is opaque to clang-tidy.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        jtx::PrettyAsset const& asset = f.asset.value();

        Vault const v{env};
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(1'000'000).value()}),
            Ter(std::ignore));
        env.close();

        auto checkSuccess = [&](STAmount const& amount, std::string const& tag) {
            auto const before = read(env, f);
            env(v.withdraw({.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = amount}),
                Ter(std::ignore));
            env.close();
            if (env.ter() != tesSUCCESS)
                return;

            auto const after = read(env, f);
            assertEqualDeltas(before, after, tag);

            Number const sharesBurned = before.sharesTotal - after.sharesTotal;
            if (before.sharesTotal == Number{0})
                return;
            Number const shareValue = (before.assetsTotal * sharesBurned) / before.sharesTotal;
            Number const tDelta = before.assetsTotal - after.assetsTotal;
            BEAST_EXPECTS(tDelta <= shareValue, tag + " payout > shareValue");
        };

        std::array<std::uint64_t, 3> const kShareCounts{99'999u, 333'333u, 1'234'567u};
        for (auto const count : kShareCounts)
        {
            auto const before = read(env, f);
            if (before.sharesTotal < count)
                continue;
            STAmount const shareAmount{MPTIssue{f.share}, Number{static_cast<std::int64_t>(count)}};
            checkSuccess(shareAmount, "shares=" + std::to_string(count));
        }

        std::array<int, 3> const kAssetAmounts{1, 7, 99};
        for (auto const amount : kAssetAmounts)
            checkSuccess(asset(amount).value(), "assets=" + std::to_string(amount));
    }

    // Withdraw more than sfAssetsAvailable must return tecINSUFFICIENT_FUNDS,
    // not tecPRECISION_LOSS.
    void
    testWithdrawInsufficientFundsPrecedence()
    {
        using namespace jtx;

        testcase("withdraw over available returns insufficient funds, not precision loss");

        Env env = makeEnv();
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!ready(f))
            return;
        // ready() above guarantees f.asset is engaged; the guard is opaque to clang-tidy.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        jtx::PrettyAsset const& asset = f.asset.value();

        Vault const v{env};
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(1'000'000).value()}),
            Ter(std::ignore));
        env.close();

        auto const before = read(env, f);
        if (!BEAST_EXPECT(before.assetsAvailable > Number{0}))
            return;

        STAmount const request = asset(before.assetsAvailable + Number{1}).value();
        env(v.withdraw({.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = request}),
            Ter(std::ignore));
        env.close();

        BEAST_EXPECTS(
            env.ter() == tecINSUFFICIENT_FUNDS,
            std::string{"expected tecINSUFFICIENT_FUNDS, got "} + transToken(env.ter()));
    }

    void
    testClawback()
    {
        using namespace jtx;

        testcase("clawback deltas are equal");

        Env env = makeEnv();
        auto f = setupSingleLoanVault(
            env,
            /*impairAndPaySibling=*/false,
            /*allowClawback=*/true);
        if (!ready(f))
            return;
        // ready() above guarantees f.asset is engaged; the guard is opaque to clang-tidy.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        jtx::PrettyAsset const& asset = f.asset.value();

        Vault const v{env};
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(2'000).value()}),
            Ter(std::ignore));
        env.close();

        auto checkSuccess = [&](std::optional<STAmount> const& amount, std::string const& tag) {
            auto const before = read(env, f);
            if (before.sharesTotal == Number{0})
                return;

            env(v.clawback(
                    {.issuer = f.issuer,
                     .id = f.vaultKeylet.key,
                     .holder = f.depositor,
                     .amount = amount}),
                Ter(std::ignore));
            env.close();
            if (env.ter() != tesSUCCESS)
                return;

            assertEqualDeltas(before, read(env, f), tag);
        };

        std::array<int, 3> const kAmounts{1, 7, 99};
        for (auto const amount : kAmounts)
            checkSuccess(asset(amount).value(), "amount=" + std::to_string(amount));

        checkSuccess(std::nullopt, "sfAmount absent");
    }

    void
    testImpairedVault()
    {
        using namespace jtx;

        testcase("impaired vault loss stays within assetsTotal - assetsAvailable");

        Env env = makeEnv();
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/true);
        if (!ready(f))
            return;
        // ready() above guarantees f.asset is engaged; the guard is opaque to clang-tidy.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        jtx::PrettyAsset const& asset = f.asset.value();

        Vault const v{env};
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(5'000).value()}),
            Ter(std::ignore));
        env.close();

        auto checkInvariant = [&](std::string const& tag) {
            TER const actual = env.ter();
            BEAST_EXPECTS(actual != tecINVARIANT_FAILED, tag + " unexpected invariant failure");
            if (actual != tesSUCCESS)
                return;
            auto const after = read(env, f);
            BEAST_EXPECTS(
                after.lossUnrealized <= after.assetsTotal - after.assetsAvailable,
                tag + " lossUnrealized exceeds assetsTotal - assetsAvailable");
        };

        std::array<int, 4> const kAmounts{1, 7, 51, 137};
        for (std::size_t i = 0; i + 1 < kAmounts.size(); i += 2)
        {
            int const depositAmount = kAmounts[i];
            int const withdrawAmount = kAmounts[i + 1];

            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(depositAmount).value()}),
                Ter(std::ignore));
            env.close();
            checkInvariant("deposit=" + std::to_string(depositAmount));

            env(v.withdraw(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(withdrawAmount).value()}),
                Ter(std::ignore));
            env.close();
            checkInvariant("withdraw=" + std::to_string(withdrawAmount));
        }
    }

public:
    void
    run() override
    {
        testDeposit();
        testWithdraw();
        testWithdrawInsufficientFundsPrecedence();
        testClawback();
        testImpairedVault();
    }
};

BEAST_DEFINE_TESTSUITE(VaultTransactorPrecision, app, xrpl);

}  // namespace xrpl::test
