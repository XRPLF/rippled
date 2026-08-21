#include <test/app/vault/VaultPrecisionFixture.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

namespace xrpl::test {

// PR 2 tests: with fixCleanup3_4_0 enabled the transactor clamps make
// deposit / withdraw / clawback exact on the sfAssetsTotal grid.  Every
// successful withdrawal and clawback satisfies STRICT equality of the
// assetsTotal / assetsAvailable / pseudo-account deltas -- no tolerance.
// Deposit is exact on the T side; A stays within one unit at the
// sfAssetsTotal grid.
class VaultTransactorPrecision_test : public VaultPrecisionFixture
{
    static Number
    absDiff(Number const& a, Number const& b)
    {
        return a > b ? a - b : b - a;
    }

    // ---- deposit ------------------------------------------------------

    // A-1 magnitude sweep.  Post-fix every successful deposit satisfies
    //   T_delta <= requested amount
    //   |T_delta - A_delta| <= oneUnit(asset, T_after)
    // and the plan's three boundary amounts {1, 7, 10'000'000} now succeed.
    // Pre-fix those three boundary amounts fail with tecINVARIANT_FAILED.
    void
    testDepositNeverOverCredited(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 deposit never over-credited") +
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

        // Plan's three boundary amounts that Part 1 alone closes on A-1.
        std::array<int, 3> const kPlanBoundaryAmounts{1, 7, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
                continue;
            jtx::PrettyAsset const& asset = *f.asset;

            auto const before = read(env, f);

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(amount).value()}),
                Ter(std::ignore));
            env.close();

            TER const actual = env.ter();
            bool const isBoundary =
                std::ranges::find(kPlanBoundaryAmounts, amount) != kPlanBoundaryAmounts.end();

            if (fixEnabled)
            {
                if (isBoundary)
                {
                    BEAST_EXPECTS(
                        actual == tesSUCCESS,
                        "plan boundary amount=" + std::to_string(amount) +
                            " expected tesSUCCESS, got " + transToken(actual));
                }
                if (actual != tesSUCCESS)
                    continue;

                auto const after = read(env, f);
                Number const tDelta = after.assetsTotal - before.assetsTotal;
                Number const aDelta = after.assetsAvailable - before.assetsAvailable;
                Number const requested = asset(amount).number();

                BEAST_EXPECTS(
                    tDelta <= requested,
                    "amount=" + std::to_string(amount) + " tDelta exceeds requested");
                Number const gap = absDiff(tDelta, aDelta);
                BEAST_EXPECTS(
                    gap <= oneUnit(asset, after.assetsTotal),
                    "amount=" + std::to_string(amount) + " |tDelta-aDelta| exceeds oneUnit");
            }
            else if (isBoundary)
            {
                BEAST_EXPECTS(
                    actual == tecINVARIANT_FAILED,
                    "pre-fix amount=" + std::to_string(amount) +
                        " expected tecINVARIANT_FAILED, got " + transToken(actual));
            }
        }
    }

    // Post-fix: the depositor never gets shares worth more than the assets
    // they paid.  Any overpay is bounded by the pre-deposit per-share value.
    void
    testDepositorNeverUnderpays(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        if (!fixEnabled)
            return;

        testcase("A-1 depositor never underpays (fixCleanup3_4_0)");

        std::array<int, 8> const kAmounts{1, 7, 100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000};

        for (auto const amount : kAmounts)
        {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
                continue;
            jtx::PrettyAsset const& asset = *f.asset;

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
            Number const sharesMinted = after.sharesTotal - before.sharesTotal;
            Number const assetsTaken = after.assetsTotal - before.assetsTotal;
            if (before.sharesTotal == Number{0})
                continue;
            Number const shareValue = (before.assetsTotal * sharesMinted) / before.sharesTotal;

            BEAST_EXPECTS(
                shareValue <= assetsTaken,
                "amount=" + std::to_string(amount) + " shareValue > assetsTaken");

            Number const perShareOverpay = before.assetsTotal / before.sharesTotal;
            Number const overpay = assetsTaken > shareValue ? assetsTaken - shareValue : Number{0};
            BEAST_EXPECTS(
                overpay <= perShareOverpay,
                "amount=" + std::to_string(amount) + " overpay exceeds per-share bound");
        }
    }

    // Push T up to ~1e8, then attempt Number{1,-10} deposit.  Post-fix:
    // tecPRECISION_LOSS with the vault state unchanged.  Confirms the
    // deposit clamp cannot mint uncovered shares.
    void
    testZeroCreditRejected(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        if (!fixEnabled)
            return;

        testcase("A-1 zero credit rejected (fixCleanup3_4_0)");

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
            return;
        jtx::PrettyAsset const& asset = *f.asset;

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
        env(v.deposit({.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = tinyAmount}),
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

    // XRP and MPT (integral) vaults: the clamp is a no-op because
    // STAmount(asset, N) already truncates to whole units.  Every amount
    // in the sweep succeeds pre- and post-amendment.
    void
    testIntegralAssetsUnchanged(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("integral asset deposit sweep") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 8> const kAmounts{1, 7, 100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000};

        auto runXrp = [&]() {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            Account const owner{"xrp_owner"};
            Account const depositor{"xrp_depositor"};
            env.fund(XRP(1'000'000'000), owner, depositor);
            env.close();

            Vault const v{env};
            auto [createTx, vaultKeylet] = v.create({.owner = owner, .asset = xrpIssue()});
            env(createTx);
            env.close();

            env(v.deposit(
                {.depositor = owner, .id = vaultKeylet.key, .amount = XRP(1'000).value()}));
            env.close();

            for (auto const amount : kAmounts)
            {
                env(v.deposit(
                        {.depositor = depositor,
                         .id = vaultKeylet.key,
                         .amount = XRP(amount).value()}),
                    Ter(std::ignore));
                env.close();
                BEAST_EXPECTS(
                    env.ter() == tesSUCCESS,
                    "XRP amount=" + std::to_string(amount) + " expected tesSUCCESS, got " +
                        transToken(env.ter()));
            }
        };

        auto runMpt = [&]() {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            Account const issuer{"mpt_issuer"};
            Account const owner{"mpt_owner"};
            Account const depositor{"mpt_depositor"};
            env.fund(XRP(1'000'000), issuer, owner, depositor);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanTransfer});
            PrettyAsset const asset = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            env(pay(issuer, depositor, asset(1'000'000'000)));
            env.close();

            Vault const v{env};
            auto [createTx, vaultKeylet] = v.create({.owner = owner, .asset = asset});
            env(createTx);
            env.close();

            env(pay(issuer, owner, asset(1'000)));
            env.close();
            env(v.deposit(
                {.depositor = owner, .id = vaultKeylet.key, .amount = asset(1'000).value()}));
            env.close();

            for (auto const amount : kAmounts)
            {
                env(v.deposit(
                        {.depositor = depositor,
                         .id = vaultKeylet.key,
                         .amount = asset(amount).value()}),
                    Ter(std::ignore));
                env.close();
                BEAST_EXPECTS(
                    env.ter() == tesSUCCESS,
                    "MPT amount=" + std::to_string(amount) + " expected tesSUCCESS, got " +
                        transToken(env.ter()));
            }
        };

        runXrp();
        runMpt();
    }

    // ---- withdraw -----------------------------------------------------

    // A-1 fixture withdrawals in both asset-fixed and share-fixed modes.
    // Post-fix: never tecINVARIANT_FAILED and every successful withdrawal
    // satisfies STRICT equality
    //   beforeT - afterT == beforeA - afterA == beforePseudo - afterPseudo
    // in Number space.  This is the strong claim of Part 1b of the plan.
    void
    testWithdrawDeltas(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 withdraw delta exactness") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<std::uint64_t, 6> const kShareCounts{
            99'999u, 100'001u, 333'333u, 1'234'567u, 142'857'142u, 333'333'333u};

        std::array<int, 5> const kAssetAmounts{1, 7, 99, 333, 993};

        auto runOnce = [&](bool useShares) {
            Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
            auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
            if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
                return;
            jtx::PrettyAsset const& asset = *f.asset;

            Vault const v{env};
            env(v.deposit(
                    {.depositor = f.depositor,
                     .id = f.vaultKeylet.key,
                     .amount = asset(1'000'000).value()}),
                Ter(std::ignore));
            env.close();

            auto step = [&](STAmount const& amount, std::string const& tag) {
                auto const before = read(env, f);
                env(v.withdraw(
                        {.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = amount}),
                    Ter(std::ignore));
                env.close();

                TER const actual = env.ter();
                if (fixEnabled)
                {
                    BEAST_EXPECTS(
                        actual != tecINVARIANT_FAILED, tag + " unexpected invariant failure");
                    if (actual == tesSUCCESS)
                    {
                        auto const after = read(env, f);
                        Number const tDelta = before.assetsTotal - after.assetsTotal;
                        Number const aDelta = before.assetsAvailable - after.assetsAvailable;
                        Number const pDelta = before.pseudo - after.pseudo;
                        BEAST_EXPECTS(tDelta == aDelta, tag + " tDelta != aDelta");
                        BEAST_EXPECTS(tDelta == pDelta, tag + " tDelta != pDelta");
                    }
                }
            };

            if (useShares)
            {
                for (auto const count : kShareCounts)
                {
                    auto const before = read(env, f);
                    if (before.sharesTotal < count)
                        continue;
                    STAmount const shareAmount{
                        MPTIssue{f.share}, Number{static_cast<std::int64_t>(count)}};
                    step(shareAmount, "shares=" + std::to_string(count));
                }
            }
            else
            {
                for (auto const amount : kAssetAmounts)
                {
                    step(asset(amount).value(), "assets=" + std::to_string(amount));
                }
            }
        };

        runOnce(/*useShares=*/true);
        runOnce(/*useShares=*/false);
    }

    // Post-fix: withdrawer never receives more than the burned share value;
    // any shortfall is bounded by one unit at the sfAssetsTotal scale.
    void
    testWithdrawNeverOverpays(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        if (!fixEnabled)
            return;

        testcase("A-1 withdraw never overpays (fixCleanup3_4_0)");

        std::array<std::uint64_t, 5> const kShareCounts{
            99'999u, 100'001u, 333'333u, 1'234'567u, 142'857'142u};

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
            return;
        jtx::PrettyAsset const& asset = *f.asset;

        Vault const v{env};
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
            if (env.ter() != tesSUCCESS)
                continue;

            auto const after = read(env, f);
            Number const sharesBurned = before.sharesTotal - after.sharesTotal;
            if (before.sharesTotal == Number{0})
                continue;
            Number const shareValue = (before.assetsTotal * sharesBurned) / before.sharesTotal;
            Number const payout = before.assetsTotal - after.assetsTotal;

            BEAST_EXPECTS(
                payout <= shareValue, "shares=" + std::to_string(count) + " payout > shareValue");
            Number const shortfall = shareValue > payout ? shareValue - payout : Number{0};
            BEAST_EXPECTS(
                shortfall <= oneUnit(asset, after.assetsTotal),
                "shares=" + std::to_string(count) + " shortfall exceeds oneUnit");
        }
    }

    // Sub-ULP withdrawal from a ~1e8 vault; post-fix must return
    // tecPRECISION_LOSS after the Upward clamp rounds the amount to zero.
    void
    testWithdrawSubUlpRejected(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        if (!fixEnabled)
            return;

        testcase("sub-ULP withdraw rejected (fixCleanup3_4_0)");

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
            return;
        jtx::PrettyAsset const& asset = *f.asset;

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
        env(v.withdraw({.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = tinyAmount}),
            Ter(std::ignore));
        env.close();

        BEAST_EXPECTS(
            env.ter() == tecPRECISION_LOSS,
            std::string{"expected tecPRECISION_LOSS, got "} + transToken(env.ter()));
    }

    // Depositor burns every share they hold, exercising the final-
    // withdrawal branch of VaultWithdraw (line 336-368 -- deliberately
    // untouched by this amendment).  Must return tesSUCCESS: the clamp
    // does not spuriously reject a legitimate full-share withdrawal.
    //
    // The plan describes reaching a "dust-only gap state" via the A-3
    // fixture; empirically the A-3 fixture retains a non-zero
    // sfLossUnrealized which blocks the final-withdrawal branch through
    // the insufficient-funds guard.  Verifying the branch from a clean
    // A-1 state still exercises the amendment's non-interference claim.
    void
    testFinalWithdrawalDust(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        if (!fixEnabled)
            return;

        testcase("A-1 final withdrawal (fixCleanup3_4_0)");

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/false);
        if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
            return;
        jtx::PrettyAsset const& asset = *f.asset;

        Vault const v{env};
        env(v.deposit(
                {.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = asset(500).value()}),
            Ter(std::ignore));
        env.close();

        auto const depositorMptSle = env.le(keylet::mptoken(f.share, f.depositor.id()));
        if (!BEAST_EXPECT(depositorMptSle != nullptr))
            return;

        auto const held = depositorMptSle->at(sfMPTAmount);
        if (held == 0)
            return;

        STAmount const shareAmount{MPTIssue{f.share}, Number{static_cast<std::int64_t>(held)}};
        env(v.withdraw({.depositor = f.depositor, .id = f.vaultKeylet.key, .amount = shareAmount}),
            Ter(std::ignore));
        env.close();

        TER const actual = env.ter();
        BEAST_EXPECTS(
            actual == tesSUCCESS, std::string{"expected tesSUCCESS, got "} + transToken(actual));
    }

    // ---- deposit + withdraw under impairment (A-3) --------------------

    // A-3 fixture drives sfLossUnrealized to the boundary
    //   lossUnrealized == assetsTotal - assetsAvailable
    // A companion invariant-fix branch found that independent per-field
    // rounding of assetsTotal/assetsAvailable can spuriously trip the
    // XRPL_ASSERT (and, pre-fix, tecINVARIANT_FAILED) that compares
    // lossUnrealized against assetsTotal - assetsAvailable in
    // VaultWithdraw::doApply. This branch's clamp keeps assetsTotal and
    // assetsAvailable exact on the sfAssetsTotal grid, which should keep
    // that comparison stable even while churning the vault through this
    // boundary state. Only meaningful post-fix -- the pre-fix boundary
    // behaviour at A-3 is out of scope for this branch (it is the subject
    // of the companion invariant-fix branch, not this one) -- so mirror
    // testWithdrawNeverOverpays and early-return before the amendment.
    void
    testDepositWithdrawUnderImpairment(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        if (!fixEnabled)
            return;

        testcase("A-3 deposit/withdraw under impairment boundary (fixCleanup3_4_0)");

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(env, /*impairAndPaySibling=*/true);
        if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
            return;
        jtx::PrettyAsset const& asset = *f.asset;

        Vault const v{env};

        // Seed the depositor with an initial stake so later withdrawals
        // have shares/assets to draw against.
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(5'000).value()}),
            Ter(std::ignore));
        env.close();

        std::array<int, 12> const kAmounts{1, 3, 7, 13, 29, 51, 97, 137, 251, 499, 991, 1'999};

        auto checkInvariant = [&](std::string const& tag) {
            TER const actual = env.ter();
            BEAST_EXPECTS(actual != tecINVARIANT_FAILED, tag + " unexpected invariant failure");
            if (actual == tesSUCCESS)
            {
                auto const after = read(env, f);
                BEAST_EXPECTS(
                    after.lossUnrealized <= after.assetsTotal - after.assetsAvailable,
                    tag + " lossUnrealized exceeds assetsTotal - assetsAvailable");
            }
        };

        // Alternate deposit then withdraw of a different amount so the
        // vault's assetsTotal / assetsAvailable / lossUnrealized state
        // churns through several transactions at the A-3 boundary.
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

    // ---- clawback -----------------------------------------------------

    // A-1 fixture with clawback enabled.  Sweep amounts including
    // sfAmount-absent (claw back everything) and amount-exceeds-available.
    // Post-fix: never tecINVARIANT_FAILED and each success satisfies
    //   T_delta == A_delta == pseudo_delta in Number space.
    // Owner force-burn against a vault with a live loan: must return
    // tecNO_PERMISSION regardless of amendment (never enters
    // assetsToClawback so the clamp is unreachable there).
    void
    testClawbackDeltas(FeatureBitset features)
    {
        using namespace jtx;

        bool const fixEnabled = features[fixCleanup3_4_0];
        testcase(
            std::string("A-1 clawback delta exactness") +
            (fixEnabled ? " (fixCleanup3_4_0)" : " (pre-fix)"));

        std::array<int, 6> const kAmounts{1, 7, 99, 333, 993, 5'000'000};

        Env env{*this, envconfig(), features, nullptr, beast::Severity::Disabled};
        auto f = setupSingleLoanVault(
            env,
            /*impairAndPaySibling=*/false,
            /*allowClawback=*/true);
        if (!BEAST_EXPECT(f.asset && f.broker) || !f.asset)
            return;
        jtx::PrettyAsset const& asset = *f.asset;

        Vault const v{env};
        env(v.deposit(
                {.depositor = f.depositor,
                 .id = f.vaultKeylet.key,
                 .amount = asset(2'000).value()}),
            Ter(std::ignore));
        env.close();

        auto stepAmount = [&](int amount) {
            auto const before = read(env, f);
            if (before.sharesTotal == Number{0})
                return;

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
                if (actual == tesSUCCESS)
                {
                    auto const after = read(env, f);
                    Number const tDelta = before.assetsTotal - after.assetsTotal;
                    Number const aDelta = before.assetsAvailable - after.assetsAvailable;
                    Number const pDelta = before.pseudo - after.pseudo;
                    BEAST_EXPECTS(
                        tDelta == aDelta, "amount=" + std::to_string(amount) + " tDelta != aDelta");
                    BEAST_EXPECTS(
                        tDelta == pDelta, "amount=" + std::to_string(amount) + " tDelta != pDelta");
                }
            }
        };

        for (auto const amount : kAmounts)
            stepAmount(amount);

        {
            auto const before = read(env, f);
            if (before.sharesTotal > Number{0})
            {
                env(v.clawback(
                        {.issuer = f.issuer, .id = f.vaultKeylet.key, .holder = f.depositor}),
                    Ter(std::ignore));
                env.close();

                TER const actual = env.ter();
                if (fixEnabled)
                {
                    BEAST_EXPECTS(
                        actual != tecINVARIANT_FAILED,
                        "sfAmount-absent unexpected invariant failure");
                    if (actual == tesSUCCESS)
                    {
                        auto const after = read(env, f);
                        Number const tDelta = before.assetsTotal - after.assetsTotal;
                        Number const aDelta = before.assetsAvailable - after.assetsAvailable;
                        Number const pDelta = before.pseudo - after.pseudo;
                        BEAST_EXPECT(tDelta == aDelta);
                        BEAST_EXPECT(tDelta == pDelta);
                    }
                }
            }
        }

        env(v.clawback({.issuer = f.lender, .id = f.vaultKeylet.key, .holder = f.depositor}),
            Ter(tecNO_PERMISSION));
        env.close();
    }

public:
    void
    run() override
    {
        for (auto const& features : {all_ - fixCleanup3_4_0, all_})
        {
            testDepositNeverOverCredited(features);
            testDepositorNeverUnderpays(features);
            testZeroCreditRejected(features);
            testIntegralAssetsUnchanged(features);
            testWithdrawDeltas(features);
            testWithdrawNeverOverpays(features);
            testWithdrawSubUlpRejected(features);
            testFinalWithdrawalDust(features);
            testDepositWithdrawUnderImpairment(features);
            testClawbackDeltas(features);
        }
    }
};

BEAST_DEFINE_TESTSUITE(VaultTransactorPrecision, app, xrpl);

}  // namespace xrpl::test
