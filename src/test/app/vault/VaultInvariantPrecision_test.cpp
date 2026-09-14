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

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tuple>

namespace xrpl::test {

// With fixCleanup3_4_0 disabled the six delta invariants and the
// lossUnrealized > (assetsTotal - assetsAvailable) gap invariant spuriously
// fire on legitimate flows; with the amendment enabled the one-unit
// tolerance absorbs the sub-ULP drift and every one of these transactions
// must succeed.  Exactness (assetsTotal delta == assetsAvailable delta
// exactly) is covered by VaultTransactorPrecision_test.
class VaultInvariantPrecision_test : public VaultPrecisionFixture
{
    // Deposit small integer amounts into an A-1 vault.  Pre-amendment,
    // deposits of 1, 7, and 10'000'000 land on assetsTotal/assetsAvailable
    // grids that disagree by one ULP and the invariant fires.  Post-
    // amendment the tolerance-widened check accepts the same states.
    void
    testDepositBoundaryInvariant(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 deposit boundary invariant") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 3> const kAmounts{1, 7, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!f.asset || !f.broker)
            {
                BEAST_EXPECT(f.asset && f.broker);
                continue;
            }
            auto const& asset = *f.asset;

            auto const before = read(env, f);

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();

            if (fixEnabled)
            {
                BEAST_EXPECTS(
                    actual == tesSUCCESS,
                    "amount=" + std::to_string(amount) + " expected tesSUCCESS, got " +
                        transToken(actual));

                auto const after = read(env, f);
                Number const tDelta = after.assetsTotal - before.assetsTotal;
                Number const aDelta = after.assetsAvailable - before.assetsAvailable;
                Number const requested = asset(amount).number();

                BEAST_EXPECT(tDelta <= requested);

                Number const gap = tDelta > aDelta ? tDelta - aDelta : aDelta - tDelta;
                BEAST_EXPECT(gap <= oneUnit(asset, after.assetsTotal));
            }
            else
            {
                BEAST_EXPECTS(
                    actual == tecINVARIANT_FAILED,
                    "amount=" + std::to_string(amount) + " expected tecINVARIANT_FAILED, got " +
                        transToken(actual));
            }
        }
    }

    // Withdraw long-mantissa share counts from an A-1 vault.  Pre-fix
    // some counts trip the withdraw delta invariants; post-fix none does.
    void
    testWithdrawBoundaryInvariant(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 withdraw boundary invariant") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<std::uint64_t, 6> const kShareCounts{
            99'999u, 100'001u, 333'333u, 1'234'567u, 142'857'142u, 333'333'333u};

        // Fill the vault with enough shares that every count below is
        // available to the depositor.
        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!f.asset || !f.broker)
        {
            BEAST_EXPECT(f.asset && f.broker);
            return;
        }
        auto const& asset = *f.asset;

        Vault const v{env};
        // Deposit a large amount so we can afford every withdrawal below.
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(1'000'000).value()}),
            Ter(std::ignore));
        env.close();

        for (auto const count : kShareCounts)
        {
            auto const before = read(env, f);
            if (before.sharesTotal < count)
                continue;

            STAmount const shareAmount{MPTIssue{f.share}, Number{static_cast<std::int64_t>(count)}};
            env(v.withdraw(
                    {.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = shareAmount}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();

            if (fixEnabled)
            {
                BEAST_EXPECTS(
                    actual != tecINVARIANT_FAILED,
                    "shares=" + std::to_string(count) + " unexpected invariant failure");

                if (actual == tesSUCCESS)
                {
                    auto const after = read(env, f);
                    Number const tDelta = before.assetsTotal - after.assetsTotal;
                    Number const pDelta = before.pseudo - after.pseudo;
                    Number const gap = tDelta > pDelta ? tDelta - pDelta : pDelta - tDelta;
                    // VaultTransactorPrecision_test tightens this to strict
                    // equality.
                    BEAST_EXPECT(gap <= oneUnit(asset, before.assetsTotal));
                }
            }
            // Pre-fix behaviour is fixture-dependent: some share counts may
            // succeed even without the amendment.  The important property is
            // that post-fix no legitimate withdrawal is rejected by the
            // widened invariant.
        }
    }

    // Clawback of small IOU amounts against a live-loan vault.  Pre-fix
    // some amounts trip the clawback delta invariants; post-fix none does.
    // Also assert the owner force-burn path returns tecNO_PERMISSION
    // under both amendment states (it never enters assetsToClawback).
    void
    testClawbackBoundaryInvariant(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 clawback boundary invariant") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 6> const kAmounts{1, 7, 99, 333, 993, 2000};

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false, /*allowClawback=*/true);
        if (!f.asset || !f.broker)
        {
            BEAST_EXPECT(f.asset && f.broker);
            return;
        }
        auto const& asset = *f.asset;

        Vault const v{env};

        // Give the depositor a stake so that the issuer has something to
        // claw back.
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(2'000).value()}),
            Ter(std::ignore));
        env.close();

        for (auto const amount : kAmounts)
        {
            auto const before = read(env, f);
            if (before.sharesTotal == 0)
                continue;

            env(v.clawback(
                    {.issuer = f.issuer,
                     .id = f.vaultKeylet.key,
                     .holder = f.depositor,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();

            if (fixEnabled)
            {
                BEAST_EXPECTS(
                    actual != tecINVARIANT_FAILED,
                    "amount=" + std::to_string(amount) + " unexpected invariant failure");
            }
            // Pre-fix behaviour is fixture-dependent: some clawback amounts
            // may succeed even without the amendment.  The important
            // property is that post-fix no legitimate clawback is rejected
            // by the widened invariant.
        }

        // Owner force-burn only succeeds against an EMPTY vault (see
        // VaultClawback::preclaim).  Our fixture keeps a live loan, so
        // this must return tecNO_PERMISSION regardless of the amendment.
        env(v.clawback({.issuer = f.lender, .id = f.vaultKeylet.key, .holder = f.depositor}),
            Ter(tecNO_PERMISSION));
        env.close();
    }

    // Deposit into an A-3 vault where the impaired-loan gap plus the
    // interest earned from the sibling repayment lands L > (T - A) by
    // sub-ULP.  Pre-fix the loss invariant fires; post-fix it does not.
    void
    testLossInvariantA3(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-3 loss invariant sweep") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 3> const kAmounts{1, 7, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/true);
            if (!f.asset || !f.broker)
            {
                BEAST_EXPECT(f.asset && f.broker);
                continue;
            }
            auto const& asset = *f.asset;

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();

            if (fixEnabled)
            {
                BEAST_EXPECTS(
                    actual == tesSUCCESS,
                    "amount=" + std::to_string(amount) + " expected tesSUCCESS, got " +
                        transToken(actual));

                auto const after = read(env, f);
                BEAST_EXPECT(
                    after.lossUnrealized <= (after.assetsTotal - after.assetsAvailable) +
                        oneUnit(asset, after.assetsTotal));
            }
            else
            {
                BEAST_EXPECTS(
                    actual == tecINVARIANT_FAILED,
                    "amount=" + std::to_string(amount) + " expected tecINVARIANT_FAILED, got " +
                        transToken(actual));
            }
        }
    }

    // Full 17-magnitude A-1 deposit sweep.  Pre-fix {1, 7, 10'000'000}
    // are the boundary amounts that fail; post-fix every amount succeeds.
    void
    testA1DepositMagnitudes(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 deposit magnitude sweep") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 17> const kAmounts{
            1,
            2,
            5,
            7,
            10,
            50,
            100,
            500,
            1'000,
            5'000,
            10'000,
            50'000,
            100'000,
            500'000,
            1'000'000,
            5'000'000,
            10'000'000};
        std::array<int, 3> const kPreFixFailures{1, 7, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!f.asset || !f.broker)
            {
                BEAST_EXPECT(f.asset && f.broker);
                continue;
            }
            auto const& asset = *f.asset;

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();

            if (fixEnabled)
            {
                BEAST_EXPECTS(
                    actual == tesSUCCESS,
                    "amount=" + std::to_string(amount) + " expected tesSUCCESS, got " +
                        transToken(actual));
            }
            else
            {
                bool const shouldFail =
                    std::ranges::find(kPreFixFailures, amount) != kPreFixFailures.end();
                if (shouldFail)
                {
                    BEAST_EXPECTS(
                        actual == tecINVARIANT_FAILED,
                        "pre-fix amount=" + std::to_string(amount) +
                            " expected tecINVARIANT_FAILED, got " + transToken(actual));
                }
                // For other amounts pre-fix, we accept any outcome; the
                // interesting property is only asserted for the known-failing
                // ones.
            }
        }
    }

    // A-3 deposit sweep.  Pre-fix {1, 7, 10'000, 10'000'000} fail; post-fix
    // every amount succeeds.  99'999 (delta tolerance) and 10'000'000
    // (loss tolerance) are the two boundary cases that motivate this PR.
    void
    testA3DepositMagnitudes(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-3 deposit magnitude sweep") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 9> const kAmounts{
            1, 7, 100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000, 99'999};

        std::array<int, 4> const kPreFixFailures{1, 7, 10'000, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/true);
            if (!f.asset || !f.broker)
            {
                BEAST_EXPECT(f.asset && f.broker);
                continue;
            }
            auto const& asset = *f.asset;

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();

            if (fixEnabled)
            {
                BEAST_EXPECTS(
                    actual == tesSUCCESS,
                    "amount=" + std::to_string(amount) + " expected tesSUCCESS, got " +
                        transToken(actual));
            }
            else
            {
                bool const shouldFail =
                    std::ranges::find(kPreFixFailures, amount) != kPreFixFailures.end();
                if (shouldFail)
                {
                    BEAST_EXPECTS(
                        actual == tecINVARIANT_FAILED,
                        "pre-fix amount=" + std::to_string(amount) +
                            " expected tecINVARIANT_FAILED, got " + transToken(actual));
                }
            }
        }
    }

public:
    void
    run() override
    {
        for (auto const& features : {all_ - fixCleanup3_4_0, all_})
        {
            testDepositBoundaryInvariant(features);
            testWithdrawBoundaryInvariant(features);
            testClawbackBoundaryInvariant(features);
            testLossInvariantA3(features);
            testA1DepositMagnitudes(features);
            testA3DepositMagnitudes(features);
        }
    }
};

BEAST_DEFINE_TESTSUITE(VaultInvariantPrecision, app, xrpl);

}  // namespace xrpl::test
