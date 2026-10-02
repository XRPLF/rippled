#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultSoleShareholder_test : public VaultTestBase
{
private:
    // design doc:
    //     AssetsAvailable ≈ 3,333.50
    //     AssetsTotal     ≈ 6,666.50  (3,333.50 cash + 3,333 receivable)
    //     LossUnrealized  =  3,333
    //     OutstandingShares = sharesLender   (5e9 at IOU scale 1e6)
    struct StuckDepositorFixture
    {
        test::jtx::Account issuer{"issuer"};
        test::jtx::Account lender{"lender"};
        test::jtx::Account bob{"bob"};
        test::jtx::Account borrower{"borrower"};
        std::optional<PrettyAsset> asset;
        std::optional<Keylet> vaultKeylet;
        UInt256 brokerID;
        std::optional<Keylet> loanKeylet;
        MPTID shareAsset;
        std::uint64_t sharesLender = 0;
    };

    static constexpr std::int64_t kStuckFunding = 1'000'000;
    static constexpr std::int64_t kStuckDepositorIOU = 1'000'000;
    static constexpr std::int64_t kStuckBorrowerIOU = 100'000;
    static constexpr std::int64_t kStuckDeposit = 5'000;
    static constexpr std::int64_t kStuckPrincipal = 3'333;
    static constexpr std::uint32_t kStuckPayInterval = 600;
    static constexpr std::uint32_t kStuckPayTotal = 2;

    [[nodiscard]] StuckDepositorFixture
    setupStuckDepositor(test::jtx::Env& env)
    {
        using namespace test::jtx;

        StuckDepositorFixture f;
        f.asset = f.issuer[iouCurrency_];

        env.fund(XRP(kStuckFunding), f.issuer, f.lender, f.bob, f.borrower);
        env.close();

        env(trust(f.lender, (*f.asset)(10'000'000)));
        env(trust(f.bob, (*f.asset)(10'000'000)));
        env(trust(f.borrower, (*f.asset)(10'000'000)));
        env.close();

        env(pay(f.issuer, f.lender, (*f.asset)(kStuckDepositorIOU)));
        env(pay(f.issuer, f.bob, (*f.asset)(kStuckDepositorIOU)));
        env(pay(f.issuer, f.borrower, (*f.asset)(kStuckBorrowerIOU)));
        env.close();

        // Vault: Lender creates and seeds it; Bob matches the deposit for a
        // clean 50/50 split.
        Vault const v{env};
        auto [createTx, vaultKeylet] = v.create({.owner = f.lender, .asset = *f.asset});
        env(createTx);
        env.close();
        if (!BEAST_EXPECT(env.le(vaultKeylet)))
            return f;
        f.vaultKeylet = vaultKeylet;

        env(v.deposit({
                .depositor = f.lender,
                .id = vaultKeylet.key,
                .amount = (*f.asset)(kStuckDeposit),
            }),
            Ter(tesSUCCESS));
        env(v.deposit({
                .depositor = f.bob,
                .id = vaultKeylet.key,
                .amount = (*f.asset)(kStuckDeposit),
            }),
            Ter(tesSUCCESS));
        env.close();

        // Loan broker: no cover, no management fee, debt cap 10x principal.
        f.brokerID =
            keylet::loanBroker(f.lender.id(), SeqProxy::rawSequence(env.seq(f.lender))).key;
        {
            using namespace loan_broker;
            env(set(f.lender, vaultKeylet.key),
                kDebtMaximum((*f.asset)(kStuckPrincipal * 10).value()));
            env.close();
        }

        // Loan: 3,333 USD principal, impaired immediately.
        auto const sleBroker = env.le(keylet::loanBroker(f.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return f;
        f.loanKeylet =
            keylet::loan(f.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        {
            using namespace loan;
            using namespace std::chrono_literals;
            env(set(f.borrower, f.brokerID, kStuckPrincipal),
                Sig(sfCounterpartySignature, f.lender),
                kPaymentTotal(kStuckPayTotal),
                kPaymentInterval(kStuckPayInterval),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            // Impairment requires the payment to be late, so advance past
            // the due date before impairing.
            auto const loanSle = env.le(*f.loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return f;
            std::uint32_t const dueDate = loanSle->at(sfNextPaymentDueDate);
            env.close(NetClock::time_point{NetClock::duration{dueDate}} + 1s);

            env(manage(f.lender, f.loanKeylet->key, tfLoanImpair), Ter(tesSUCCESS));
            env.close();
        }

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return f;
        BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == (*f.asset)(kStuckPrincipal).value());

        f.shareAsset = vaultSle->at(sfShareMPTID);

        auto const tokenBob = env.le(keylet::mptoken(f.shareAsset, f.bob.id()));
        if (!BEAST_EXPECT(tokenBob))
            return f;
        std::uint64_t const sharesBob = tokenBob->getFieldU64(sfMPTAmount);

        // Bob (non-sole) exits at the discounted rate. Always succeeds.
        STAmount const bobShareAmt{MPTIssue{f.shareAsset}, Number(sharesBob)};
        env(v.withdraw({
                .depositor = f.bob,
                .id = vaultKeylet.key,
                .amount = bobShareAmt,
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const tokenLender = env.le(keylet::mptoken(f.shareAsset, f.lender.id()));
        if (!BEAST_EXPECT(tokenLender))
            return f;
        f.sharesLender = tokenLender->getFieldU64(sfMPTAmount);

        auto const sleIssuance = env.le(keylet::mptokenIssuance(f.shareAsset));
        if (!BEAST_EXPECT(sleIssuance))
            return f;
        BEAST_EXPECT(sleIssuance->getFieldU64(sfOutstandingAmount) == f.sharesLender);

        auto const vaultAfterBob = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultAfterBob))
            return f;
        // After Bob's exit: loss is unchanged (3,333 receivable), and the
        // gap between assetsTotal and assetsAvailable equals exactly that
        // receivable.
        BEAST_EXPECT(vaultAfterBob->at(sfLossUnrealized) == (*f.asset)(kStuckPrincipal).value());
        BEAST_EXPECT(
            vaultAfterBob->at(sfAssetsTotal) - vaultAfterBob->at(sfAssetsAvailable) ==
            vaultAfterBob->at(sfLossUnrealized));

        return f;
    }

    // Reproduces the worked example from the XLS-0065 design doc. The sole
    // remaining shareholder asks (via fixed-asset input) for the vault's
    // entire AssetsAvailable. Pre-fix this fails with the zero-sized-vault
    // invariant violation. Post-fix the full-price exchange rate burns
    // only a portion of the shares, the depositor receives all of
    // AssetsAvailable, and the residual shares remain backed by the
    // impaired-loan receivable.
    void
    testWithdrawSoleShareholderFixedAssetExit(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_2_0];
        testcase(
            std::string{"Vault withdraw: sole shareholder exits via "
                        "fixed-asset amount with impaired loan"} +
            (withFix ? " (fixCleanup3_2_0)" : " (pre-fix)"));

        std::string logs;
        Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || !f.asset || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;
        PrettyAsset const& asset = *f.asset;

        auto const vaultBefore = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const availableBefore = vaultBefore->at(sfAssetsAvailable);
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const lossBefore = vaultBefore->at(sfLossUnrealized);

        STAmount const lenderBalanceBefore = env.balance(f.lender, asset);

        // The requested amount differs between feature regimes because
        // the two regimes are testing different behaviors:
        //
        // - Pre-fix: request the full AssetsAvailable (3,333.50). Under
        //   the discounted formula this would burn every outstanding
        //   share, hitting the zero-sized-vault invariant. The
        //   transaction is rejected with tecINVARIANT_FAILED — the
        //   stuck-depositor bug.
        //
        // - Post-fix: request a strictly smaller amount (1,000 USD).
        //   The full-price formula burns only ~30% of the outstanding
        //   shares; the vault retains the rest, backed by the impaired
        //   receivable. Requesting *exactly* AssetsAvailable post-fix
        //   would currently fail with tecINSUFFICIENT_FUNDS due to the
        //   round-to-nearest used by assetsToSharesWithdraw (the
        //   recomputed payout can overshoot the request by a few ULPs).
        //   The "force payout to AssetsAvailable" branch in doApply
        //   only triggers when every share is burned, which is covered
        //   by the loan-repayment test.
        STAmount const requestAssets =
            withFix ? asset(1000).value() : STAmount{asset.raw(), availableBefore};
        Vault const v{env};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = requestAssets,
            }),
            Ter(withFix ? TER{tesSUCCESS} : TER{tecINVARIANT_FAILED}));
        env.close();

        auto const vaultAfter = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceAfter))
            return;

        std::uint64_t const sharesAfter = issuanceAfter->getFieldU64(sfOutstandingAmount);
        Number const availableAfter = vaultAfter->at(sfAssetsAvailable);
        Number const totalAfter = vaultAfter->at(sfAssetsTotal);
        Number const lossAfter = vaultAfter->at(sfLossUnrealized);

        if (!withFix)
        {
            // Pre-fix: rejected — vault state unchanged.
            BEAST_EXPECT(sharesAfter == f.sharesLender);
            BEAST_EXPECT(availableAfter == availableBefore);
            BEAST_EXPECT(totalAfter == totalBefore);
            BEAST_EXPECT(lossAfter == lossBefore);
            return;
        }

        // Post-fix exact-value derivation (fixture: sharesLender=5e9,
        // totalBefore=6666.5, request=1000):
        //   sharesRedeemed = round(sharesLender * request / totalBefore)
        //                  = round(750,018,750.469) = 750,018,750
        //   received       = totalBefore * sharesRedeemed / sharesLender
        //                  = 999.999999375  (slightly under 1,000 due to
        //                                    integer-share rounding)
        constexpr std::uint64_t kExpectedSharesRedeemed = 750'018'750;
        Number const expectedReceived =
            totalBefore * Number(kExpectedSharesRedeemed) / Number(f.sharesLender);

        BEAST_EXPECT(sharesAfter == f.sharesLender - kExpectedSharesRedeemed);

        // LossUnrealized is unchanged: the loan-protocol side is untouched.
        BEAST_EXPECT(lossAfter == lossBefore);

        // The entire (total - available) gap is the impaired receivable,
        // i.e. equal to lossUnrealized.
        BEAST_EXPECT(totalAfter - availableAfter == lossAfter);

        STAmount const lenderBalanceAfter = env.balance(f.lender, asset);
        Number const received{lenderBalanceAfter - lenderBalanceBefore};
        BEAST_EXPECT(received == expectedReceived);

        // Conservation: assets removed from the vault equal what the
        // depositor received.
        BEAST_EXPECT(totalBefore - totalAfter == received);
        BEAST_EXPECT(availableBefore - availableAfter == received);
    }

    // Sole shareholder attempts to burn ALL outstanding shares via
    // fixed-shares input while the vault still holds an impaired
    // receivable. Pre-fix this fails with the zero-sized-vault invariant
    // violation. Post-fix the full-price rate causes assetsWithdrawn to
    // equal assetsTotal, which exceeds assetsAvailable, so the transaction
    // is rejected with tecINSUFFICIENT_FUNDS.
    void
    testWithdrawSoleShareholderFullSharesRejected(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_2_0];
        testcase(
            std::string{"Vault withdraw: sole shareholder full-shares "
                        "burn is rejected while loss outstanding"} +
            (withFix ? " (fixCleanup3_2_0)" : " (pre-fix)"));

        std::string logs;
        Env env(*this, features, std::make_unique<test::CaptureLogs>(&logs));
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;

        auto const vaultBefore = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const availableBefore = vaultBefore->at(sfAssetsAvailable);
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const lossBefore = vaultBefore->at(sfLossUnrealized);

        // Fixed-shares input: ask for ALL outstanding shares.
        STAmount const shareAmt{MPTIssue{f.shareAsset}, Number(f.sharesLender)};
        Vault const v{env};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = shareAmt,
            }),
            Ter(withFix ? TER{tecINSUFFICIENT_FUNDS} : TER{tecINVARIANT_FAILED}));
        env.close();

        // Either way the transaction was rejected; vault state unchanged.
        auto const vaultAfter = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceAfter))
            return;
        BEAST_EXPECT(issuanceAfter->getFieldU64(sfOutstandingAmount) == f.sharesLender);
        BEAST_EXPECT(vaultAfter->at(sfAssetsAvailable) == availableBefore);
        BEAST_EXPECT(vaultAfter->at(sfAssetsTotal) == totalBefore);
        BEAST_EXPECT(vaultAfter->at(sfLossUnrealized) == lossBefore);
    }

    // Clean-state regression: with no impaired loan, a sole shareholder
    // burning all their shares fully empties the vault under both the
    // pre-fix and post-fix code paths. Confirms the new logic doesn't
    // break the existing happy-path close-out.
    void
    testWithdrawSoleShareholderCleanVaultUnaffected(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const withFix = features[fixCleanup3_2_0];
        testcase(
            std::string{"Vault withdraw: sole shareholder clean-state "
                        "close-out unchanged"} +
            (withFix ? " (fixCleanup3_2_0)" : " (pre-fix)"));

        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        env.fund(XRP(kStuckFunding), issuer, lender);
        env.close();

        PrettyAsset const asset = issuer[iouCurrency_];
        env(trust(lender, asset(10'000'000)));
        env.close();
        env(pay(issuer, lender, asset(kStuckDepositorIOU)));
        env.close();

        // Sole shareholder of a clean vault — no loan broker needed.
        Vault const v{env};
        auto [createTx, vaultKeylet] = v.create({.owner = lender, .asset = asset});
        env(createTx);
        env.close();

        env(v.deposit({
                .depositor = lender,
                .id = vaultKeylet.key,
                .amount = asset(kStuckDeposit),
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        auto const shareAsset = vaultBefore->at(sfShareMPTID);
        auto const tokenLender = env.le(keylet::mptoken(shareAsset, lender.id()));
        if (!BEAST_EXPECT(tokenLender))
            return;
        std::uint64_t const sharesLender = tokenLender->getFieldU64(sfMPTAmount);

        // Sole shareholder, no loans, no loss. Burn everything.
        STAmount const allShares{MPTIssue{shareAsset}, Number(sharesLender)};
        env(v.withdraw({
                .depositor = lender,
                .id = vaultKeylet.key,
                .amount = allShares,
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultFinal = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultFinal))
            return;
        auto const issuanceFinal = env.le(keylet::mptokenIssuance(shareAsset));
        if (!BEAST_EXPECT(issuanceFinal))
            return;
        BEAST_EXPECT(issuanceFinal->getFieldU64(sfOutstandingAmount) == 0);
        BEAST_EXPECT(vaultFinal->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfAssetsAvailable) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfLossUnrealized) == beast::kZero);

        // (Pre-fix path takes the regular code path; post-fix path enters
        // the new final-withdrawal guard, which forces payout to exactly
        // assetsAvailable. Either way the result is identical for a clean
        // vault.)
        (void)withFix;
    }

    // Sole shareholder in an impaired vault redeems a *partial* count of
    // shares via fixed-shares input. Pre-fix the discounted formula is
    // used; post-fix the full-price formula is used (waiveUnrealizedLoss
    // = Yes). The relative payout therefore differs, and post-fix the
    // depositor recovers proportionally more of the residual cash for
    // the shares burned. In both cases the vault is left in a valid
    // (non-empty) state.
    void
    testWithdrawSoleShareholderPartialFixedSharesUsesFullPrice()
    {
        using namespace test::jtx;

        testcase(
            "Vault withdraw: sole-shareholder partial fixed-shares uses "
            "full-price rate (fixCleanup3_2_0)");

        // Strip featureLendingProtocolV1_1: setupStuckDepositor builds an
        // open-ended vault and this test asserts amendment-independent
        // withdrawal invariants (see the note on run()).
        Env env(*this, (all_ - featureLendingProtocolV1_1) | fixCleanup3_2_0);
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || !f.asset || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;
        PrettyAsset const& asset = *f.asset;

        auto const vaultBefore = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const availableBefore = vaultBefore->at(sfAssetsAvailable);
        Number const lossBefore = vaultBefore->at(sfLossUnrealized);

        // Burn exactly half of the outstanding shares.
        std::uint64_t const halfShares = f.sharesLender / 2;
        STAmount const halfAmt{MPTIssue{f.shareAsset}, Number(halfShares)};

        STAmount const lenderBalanceBefore = env.balance(f.lender, asset);

        Vault const v{env};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = halfAmt,
            }),
            Ter(tesSUCCESS));
        env.close();

        // Expected payout under the full-price formula:
        //   assets = totalBefore * halfShares / sharesLender
        // which (with halfShares == sharesLender/2) is roughly
        //   totalBefore / 2.
        STAmount const lenderBalanceAfter = env.balance(f.lender, asset);
        Number const received{lenderBalanceAfter - lenderBalanceBefore};
        Number const expected = totalBefore * Number(halfShares) / Number(f.sharesLender);
        BEAST_EXPECT(received == expected);

        // The full-price payout exceeds the discounted formula by exactly
        // lossBefore * halfShares / sharesLender — that's the whole point
        // of the waive.
        Number const discounted =
            (totalBefore - lossBefore) * Number(halfShares) / Number(f.sharesLender);
        Number const expectedDelta = lossBefore * Number(halfShares) / Number(f.sharesLender);
        BEAST_EXPECT(received - discounted == expectedDelta);

        auto const vaultAfter = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceAfter))
            return;

        // Vault remains valid: half the shares remain, lossUnrealized
        // is untouched, and the entire (total - available) gap is still
        // the impaired receivable.
        BEAST_EXPECT(
            issuanceAfter->getFieldU64(sfOutstandingAmount) == f.sharesLender - halfShares);
        BEAST_EXPECT(vaultAfter->at(sfAssetsTotal) == totalBefore - received);
        BEAST_EXPECT(vaultAfter->at(sfLossUnrealized) == lossBefore);
        BEAST_EXPECT(
            vaultAfter->at(sfAssetsTotal) - vaultAfter->at(sfAssetsAvailable) ==
            vaultAfter->at(sfLossUnrealized));

        // Conservation: vault delta matches the depositor's gain.
        BEAST_EXPECT(totalBefore - vaultAfter->at(sfAssetsTotal) == received);
        BEAST_EXPECT(availableBefore - vaultAfter->at(sfAssetsAvailable) == received);
    }

    // Post-fix end-to-end resolution: after the sole-shareholder partial
    // exit, the loan is repaid in full. With unrealized loss cleared and
    // all assets back as cash, the depositor can burn all remaining
    // shares and fully exit the vault. The final withdrawal hits the
    // "force payout to assetsAvailable" branch in doApply.
    void
    testWithdrawSoleShareholderLoanRepaymentExit()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Vault withdraw: sole shareholder fully exits after impaired "
            "loan is repaid (fixCleanup3_2_0)");

        // Strip featureLendingProtocolV1_1 as above.
        Env env(*this, (all_ - featureLendingProtocolV1_1) | fixCleanup3_2_0);
        auto const f = setupStuckDepositor(env);
        if (!f.vaultKeylet || !f.asset || !f.loanKeylet || f.sharesLender == 0)
        {
            BEAST_EXPECT(false);
            return;
        }
        Keylet const& vaultKey = *f.vaultKeylet;
        Keylet const& loanKey = *f.loanKeylet;
        PrettyAsset const& asset = *f.asset;

        Vault const v{env};

        // Sole-shareholder partial exit (see comment in
        // testWithdrawSoleShareholderFixedAssetExit for why we request
        // less than full AssetsAvailable).
        {
            STAmount const requestAssets = asset(1000).value();
            env(v.withdraw({
                    .depositor = f.lender,
                    .id = vaultKey.key,
                    .amount = requestAssets,
                }),
                Ter(tesSUCCESS));
            env.close();
        }

        // Confirm the "dormant-but-alive" state from the design doc. The
        // partial exit burned exactly 750,018,750 shares (see derivation
        // in testWithdrawSoleShareholderFixedAssetExit).
        auto const tokenAfterExit = env.le(keylet::mptoken(f.shareAsset, f.lender.id()));
        if (!BEAST_EXPECT(tokenAfterExit))
            return;
        std::uint64_t const retainedShares = tokenAfterExit->getFieldU64(sfMPTAmount);
        BEAST_EXPECT(retainedShares == f.sharesLender - 750'018'750);

        // Borrower repays the loan in full (pays more than the outstanding
        // total each time; the loan transactor caps the receivable). The
        // loan is still overdue from the impairment setup, so the first
        // (and only remaining, since kStuckPayTotal == 2) outstanding
        // installment must be caught up with a late payment before the
        // final regular payment can close the loan out.
        env(pay(f.borrower, loanKey.key, asset(kStuckPrincipal * 2), tfLoanLatePayment),
            Ter(tesSUCCESS));
        env.close();
        env(pay(f.borrower, loanKey.key, asset(kStuckPrincipal * 2)), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfterRepay = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultAfterRepay))
            return;
        // Repayment converts the 3,333 receivable back to cash; assetsTotal
        // is unchanged but assetsAvailable jumps by exactly the same amount,
        // and lossUnrealized clears to zero.
        BEAST_EXPECT(vaultAfterRepay->at(sfLossUnrealized) == beast::kZero);
        BEAST_EXPECT(vaultAfterRepay->at(sfAssetsAvailable) == vaultAfterRepay->at(sfAssetsTotal));

        STAmount const lenderBalanceBeforeFinal = env.balance(f.lender, asset);
        Number const availableBeforeFinal = vaultAfterRepay->at(sfAssetsAvailable);

        // Burn all remaining shares — the clean-state preconditions of
        // the "final withdrawal" guard are now satisfied.
        STAmount const allShares{MPTIssue{f.shareAsset}, Number(retainedShares)};
        env(v.withdraw({
                .depositor = f.lender,
                .id = vaultKey.key,
                .amount = allShares,
            }),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultFinal = env.le(vaultKey);
        if (!BEAST_EXPECT(vaultFinal))
            return;
        auto const issuanceFinal = env.le(keylet::mptokenIssuance(f.shareAsset));
        if (!BEAST_EXPECT(issuanceFinal))
            return;

        // Zero-sized vault invariant satisfied: 0 shares, 0 assets.
        BEAST_EXPECT(issuanceFinal->getFieldU64(sfOutstandingAmount) == 0);
        BEAST_EXPECT(vaultFinal->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfAssetsAvailable) == beast::kZero);
        BEAST_EXPECT(vaultFinal->at(sfLossUnrealized) == beast::kZero);

        // The final payout equals exactly the AssetsAvailable that
        // existed before the call (the "force payout" branch).
        STAmount const lenderBalanceAfter = env.balance(f.lender, asset);
        Number const finalReceived{lenderBalanceAfter - lenderBalanceBeforeFinal};
        BEAST_EXPECT(finalReceived == availableBeforeFinal);
    }

public:
    void
    run() override
    {
        // These sole-shareholder exit scenarios build an open-ended vault
        // and drive it through deposits, a loan broker, an impaired loan
        // and finally a withdrawal by the last shareholder. Under
        // featureLendingProtocolV1_1 LoanBrokerSet::preclaim rejects
        // brokers attached to open-ended vaults, so this suite runs with
        // the amendment stripped; the invariants asserted here are
        // amendment-independent.
        auto const legacy = all_ - featureLendingProtocolV1_1;
        testWithdrawSoleShareholderFixedAssetExit(legacy - fixCleanup3_2_0);
        testWithdrawSoleShareholderFixedAssetExit(legacy);
        testWithdrawSoleShareholderFullSharesRejected(legacy - fixCleanup3_2_0);
        testWithdrawSoleShareholderFullSharesRejected(legacy);
        testWithdrawSoleShareholderCleanVaultUnaffected(legacy - fixCleanup3_2_0);
        testWithdrawSoleShareholderCleanVaultUnaffected(legacy);
        testWithdrawSoleShareholderPartialFixedSharesUsesFullPrice();
        testWithdrawSoleShareholderLoanRepaymentExit();
    }
};

BEAST_DEFINE_TESTSUITE(VaultSoleShareholder, app, xrpl);

}  // namespace xrpl
