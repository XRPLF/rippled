#include <test/app/lending/LoanTestBase.h>
#include <test/app/lending/VaultDustProbe.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>

// ============================================================================
// VaultRounding_test.cpp — the suite shared by the base branch and both
// solution branches for the Vault rounding / dust bug (see
// docs/plan-vault-dust-00-common.md, docs/plan-vault-dust-testsuite.md).
//
// This file MUST be byte-identical on all three branches
// (the base branch, …-pseudo-account, …-trustline-dust). The only
// file that may differ between branches is VaultDustProbe.h. Do not
// reference sfDustAccount, sfDust, or any other implementation-specific
// field here — go through readVaultDust() instead.
//
// RED/GREEN CONTRACT. The Tier 2 tests below FAIL on the base branch, by
// design: they assert the dust oracles, and the base branch has no dust
// mechanism, so the value it loses shows up as a failure. That failure IS
// the bug's demonstration. Each solution branch makes them pass by giving
// the dust a home. Tier 1 passes everywhere — it is the regression net for
// behaviour that is already correct, so a Tier 1 failure is a real problem
// on any branch.
//
// Consequently: do NOT "fix" a Tier 2 failure by weakening an assertion.
// On the base branch it is expected; on a solution branch it is that
// branch's bug, or a finding about the suite — report it, never edit it.
// ============================================================================

namespace xrpl::test {

class VaultRounding_test : public LoanTestBase
{
private:
    // The dust-generating fixture (testsuite doc §5 / base-branch plan §3.1).
    //
    // A tiny loan is created first (while the Vault's scale is still fine),
    // then a big loan is created that pushes the Vault's scale one decade
    // coarser. The tiny loan's own sfLoanScale is frozen at origination, so
    // it stays one digit finer than the Vault can now represent — exactly
    // the §1 condition. Reproduces LoanInvariants_test.cpp's verified
    // scale-mismatch setup (tinyLoan scale -12, bigLoan scale -11, vault
    // scale -11), with a non-zero interest rate on the tiny loan so that its
    // payments actually carry a non-zero digit at the finer scale (0%
    // interest on a round principal would not).
    struct DustCtx
    {
        jtx::Account issuer{"issuer"};
        jtx::Account lender{"lender"};
        jtx::Account borrower{"borrower"};
        jtx::PrettyAsset asset;
        BrokerInfo broker;
        Keylet tinyLoanKeylet;
        Keylet bigLoanKeylet;
    };

    // Builds the fixture in a freshly-constructed Env and invokes `body`
    // with it. Returns false (having already reported failures via
    // BEAST_EXPECT) if the scale mismatch does not reproduce, so callers can
    // bail out early rather than asserting on a meaningless setup.
    bool
    withDustSetup(FeatureBitset features, std::function<void(jtx::Env&, DustCtx const&)> body)
    {
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000'00), issuer, lender, borrower);
        env.close();

        // Keep balances small (5-digit magnitude, matching the vault's own
        // scale of -11) so that an STAmount trust-line balance can exactly
        // represent a transfer already rounded to the vault's scale.
        // A 6-digit balance (e.g. 500,000) is only representable to 1e-10,
        // which would silently swallow a rounded transfer at the vault's
        // finer -11 scale — a *different* precision effect than the one
        // this fixture exists to exercise, and it must not be allowed to
        // masquerade as vault dust.
        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(50'000)));
        env(trust(borrower, asset(50'000)));
        env.close();
        env(pay(issuer, lender, asset(30'000)));
        env(pay(issuer, borrower, asset(1'000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{13'370},
            .coverDeposit = 5'000,
            // No management fee: keeps sfTotalValueOutstanding == principal
            // + interest exactly, so recognitionDelta can be derived from
            // the Loan's own before/after state without also having to
            // disentangle a third, independently-rounded fee component.
            .managementFeeRate = TenthBips16{0}};

        BrokerInfo const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        // TINY loan first, while the vault scale is still fine (small
        // principal, non-zero interest so the payment carries fine digits).
        auto const brokerSle1 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle1))
            return false;
        Keylet const tinyLoanKeylet = keylet::loan(broker.brokerID, brokerSle1->at(sfLoanSequence));

        env(set(borrower, broker.brokerID, Number{1, -2}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{1'922}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        Vault const vault{env};

        // Push the vault's scale one decade coarser with a plain deposit —
        // recognitionDelta == cashIn for a deposit under EITHER accrual or
        // cash-basis (common §5.1's table), so this step reproduces the
        // scale mismatch regardless of which model the Vault uses, unlike
        // relying on a second loan's origination (whose recognitionDelta is
        // model-dependent: accrual books interestDue up front, cash-basis
        // books nothing).
        env(vault.deposit(
            {.depositor = lender, .id = broker.vaultKeylet().key, .amount = asset(9'500)}));
        env.close();

        // BIG loan second: needed only for the LoanManage::defaultLoan
        // scenario (common §2.2). Created after the scale bump so its own
        // sfLoanScale matches the vault's now-coarser scale.
        auto const brokerSle2 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle2))
            return false;
        Keylet const bigLoanKeylet = keylet::loan(broker.brokerID, brokerSle2->at(sfLoanSequence));

        env(set(borrower, broker.brokerID, Number{500}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{100'000}),
            kPaymentTotal(20),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        auto const tinyLoanSle = env.le(tinyLoanKeylet);
        auto const bigLoanSle = env.le(bigLoanKeylet);
        auto const vaultSle = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(tinyLoanSle) || !BEAST_EXPECT(bigLoanSle) || !BEAST_EXPECT(vaultSle))
            return false;

        if (!BEAST_EXPECT(tinyLoanSle->at(sfLoanScale) == -12) ||
            !BEAST_EXPECT(bigLoanSle->at(sfLoanScale) == -11) ||
            !BEAST_EXPECT(getVaultScale(vaultSle) == -11))
        {
            // Base-branch plan §9 / common §9: the reproduction recipe is
            // stale. Report loudly rather than silently adapting the
            // expected scales.
            log << "VaultRounding: dust-generating fixture did NOT reproduce "
                   "-12 / -11 / -11. tinyLoanScale="
                << tinyLoanSle->at(sfLoanScale) << " bigLoanScale=" << bigLoanSle->at(sfLoanScale)
                << " vaultScale=" << getVaultScale(vaultSle) << std::endl;
            return false;
        }

        body(
            env,
            DustCtx{
                .issuer = issuer,
                .lender = lender,
                .borrower = borrower,
                .asset = asset,
                .broker = broker,
                .tinyLoanKeylet = tinyLoanKeylet,
                .bigLoanKeylet = bigLoanKeylet});
        return true;
    }

    // Pays off a loan that has exactly one payment remaining, in full,
    // on-schedule (no overpayment, no lateness), by reading the loan's own
    // sfPeriodicPayment / sfLoanServiceFee / sfLoanScale rather than
    // recomputing the amortization schedule.
    void
    payLoanInFull(
        jtx::Env& env,
        jtx::Account const& borrower,
        Asset const& asset,
        Keylet const& loanKeylet)
    {
        using namespace jtx;

        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        if (!loanSle)
            return;
        auto const periodicPayment = loanSle->at(sfPeriodicPayment);
        auto const serviceFee = loanSle->at(sfLoanServiceFee);
        std::int32_t const loanScale = loanSle->at(sfLoanScale);

        auto const payment = roundPeriodicPayment(asset, periodicPayment, loanScale);
        auto const payAmt = STAmount{asset, payment + serviceFee};

        env(jtx::loan::pay(borrower, loanKeylet.key, payAmt), Fee(XRP(10)));
        env.close();
    }

    // Sum of sfPrincipalOutstanding over every Loan the fixture created
    // (testsuite doc §3 precondition 2: PO must come from the Loans
    // themselves, never from the broker's own sfDebtTotal).
    static Number
    principalOutstanding(jtx::Env const& env, DustCtx const& ctx)
    {
        Number total{};
        for (auto const& k : {ctx.tinyLoanKeylet, ctx.bigLoanKeylet})
        {
            if (auto const loanSle = env.le(k))
                total += loanSle->at(sfPrincipalOutstanding);
        }
        return total;
    }

    static jtx::Account
    vaultPseudoAccount(jtx::Env const& env, Keylet const& vaultKeylet)
    {
        auto const vaultSle = env.le(vaultKeylet);
        return jtx::Account("vaultPseudo", vaultSle->at(sfAccount));
    }

    //--------------------------------------------------------------------
    // THE BUG, IN ONE TEST. Start here.
    //--------------------------------------------------------------------

    // Vault -> loan at a fine scale -> big deposit (which coarsens the
    // Vault's scale, while the loan's stays frozen) -> repay the loan.
    //
    // The Vault's books should always satisfy
    //
    //     AssetsTotal == AssetsAvailable + principal outstanding
    //
    // i.e. total value = cash on hand + what borrowers still owe. Nothing
    // else can legitimately live in the difference.
    //
    // Before the repayment that identity holds. After it, on unfixed code,
    // it does not: the loan's receivable falls by the full amount owed, but
    // only the part representable at the Vault's now-coarser scale reaches
    // AssetsAvailable. The remainder — the dust — is simply gone, and the
    // books are left claiming value nobody holds or owes.
    //
    // Under either solution the dust has a home, so the identity still
    // holds after the repayment and this test passes unchanged. That is the
    // whole fix, and this is the whole test for it: no dust probe, no
    // interest/principal decomposition, one identity.
    void
    testDustDisappears(FeatureBitset features)
    {
        testcase("The dust disappears from the Vault's books");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            // AssetsTotal - AssetsAvailable - (principal still owed).
            // Zero when the books balance; positive when they claim value
            // that no longer exists anywhere.
            auto const unaccounted = [&]() -> Number {
                auto const vaultSle = env.le(ctx.broker.vaultKeylet());
                if (!BEAST_EXPECT(vaultSle))
                    return Number{};
                return Number{vaultSle->at(sfAssetsTotal)} -
                    Number{vaultSle->at(sfAssetsAvailable)} - principalOutstanding(env, ctx);
            };

            Number const before = unaccounted();
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            Number const after = unaccounted();

            log << "  unaccounted (AssetsTotal - AssetsAvailable - principalOwed): " << before
                << " -> " << after << std::endl;

            // The books balance before the repayment either way.
            BEAST_EXPECT(before == beast::kZero);

            // Fixed: the dust is accounted for, wherever it lives.
            BEAST_EXPECT(after == beast::kZero);
        });
    }

    //--------------------------------------------------------------------
    // Tier 1 — regression net. Identical assertions on all three branches.
    //--------------------------------------------------------------------

    // O1: sfAssetsAvailable always mirrors the real pseudo-account balance.
    void
    testMirrorHoldsAcrossLoanLifecycle(FeatureBitset features)
    {
        testcase("O1: AssetsAvailable mirrors the vault pseudo balance");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const checkMirror = [&]() {
                auto const vaultSle = env.le(ctx.broker.vaultKeylet());
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Number const a = vaultSle->at(sfAssetsAvailable);
                Number const b = accountHolds(
                    *env.current(),
                    vaultSle->at(sfAccount),
                    ctx.asset.raw(),
                    FreezeHandling::IgnoreFreeze,
                    AuthHandling::IgnoreAuth,
                    beast::Journal{beast::Journal::getNullSink()});
                BEAST_EXPECT(a == b);
            };

            checkMirror();
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            checkMirror();
        });
    }

    // O3: funds conserved — no real balance is created or destroyed by a
    // repayment, only moved.
    void
    testFundsConserved(FeatureBitset features)
    {
        testcase("O3: funds conserved across a repayment");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const pseudo = vaultPseudoAccount(env, ctx.broker.vaultKeylet());
            auto const brokerSle = env.le(ctx.broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerSle))
                return;
            jtx::Account const brokerPseudo("brokerPseudo", brokerSle->at(sfAccount));

            // The broker fee leg's destination is either the broker
            // pseudo-account or the broker's owner (LoanPay.cpp's
            // sendBrokerFeeToOwner), depending on cover sufficiency. Track
            // every account real cash could land on.
            //
            // The dust reservoir must be included too: the payer parts with
            // the raw (unrounded) amount, and every unit of that raw amount
            // has to land *somewhere* on the ledger — either in main
            // custody (the four balances above) or in the reservoir. Without
            // it, this sum only balances on branches where the borrower is
            // debited the rounded amount instead of the raw one.
            auto const before = env.balance(ctx.borrower, ctx.asset).number() +
                env.balance(pseudo, ctx.asset).number() +
                env.balance(brokerPseudo, ctx.asset).number() +
                env.balance(ctx.lender, ctx.asset).number() +
                readVaultDust(env, ctx.broker.vaultKeylet());

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            auto const after = env.balance(ctx.borrower, ctx.asset).number() +
                env.balance(pseudo, ctx.asset).number() +
                env.balance(brokerPseudo, ctx.asset).number() +
                env.balance(ctx.lender, ctx.asset).number() +
                readVaultDust(env, ctx.broker.vaultKeylet());

            BEAST_EXPECT(before == after);
        });
    }

    // XRP and MPT Vaults: roundToAsset is a no-op for an integral asset, so
    // dust is identically zero and ledger state must not depend on the dust
    // mechanism (common §2.4).
    void
    testIntegralAssetsProduceNoDust(FeatureBitset features)
    {
        testcase("Integral (XRP/MPT) vaults never produce dust");

        using namespace jtx;

        for (bool const useXrp : {true, false})
        {
            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};
            env.fund(XRP(1'000'000), issuer, lender, borrower);
            env.close();

            std::optional<MPTTester> mptt;
            Asset asset;
            if (useXrp)
            {
                asset = xrpIssue();
            }
            else
            {
                mptt.emplace(env, issuer, kMptInitNoFund);
                mptt->create({.maxAmt = 1'000'000, .flags = tfMPTCanTransfer});
                mptt->authorize({.account = lender});
                mptt->authorize({.account = borrower});
                asset = mptt->issuanceID();
                env(pay(issuer, lender, STAmount{asset, 500'000}));
                env(pay(issuer, borrower, STAmount{asset, 500'000}));
                env.close();
            }

            Vault const vault{env};
            auto [vaultTx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
            env(vaultTx);
            env.close();

            STAmount const depositAmt = useXrp ? STAmount{XRP(500'000)} : STAmount{asset, 500'000};
            env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = depositAmt}));
            env.close();

            auto const vaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                continue;

            BEAST_EXPECT(readVaultDust(env, vaultKeylet) == beast::kZero);
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == vaultSle->at(sfAssetsTotal));
        }
    }

    // The new-Vaults-only scope decision (common §2.5): a Legacy Vault
    // (created without featureLendingProtocolV1_1) is exercised the same
    // way and produces the same ledger state as it does on this branch
    // today — this branch implements no version-dependent behaviour at
    // all, so the "same as §4.3's pinned numbers" claim holds trivially,
    // which is the strongest form of "unchanged" available on the base
    // branch.
    void
    testLegacyVaultUnchanged(FeatureBitset features)
    {
        testcase("A Legacy vault's ledger state is unchanged");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const vaultSle = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSle))
                return;
            BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::Legacy);

            auto const loanSleBefore = env.le(ctx.tinyLoanKeylet);
            Number const poBefore = loanSleBefore->at(sfPrincipalOutstanding);
            Number const totalBefore = vaultSle->at(sfAssetsTotal);
            Number const availBefore = vaultSle->at(sfAssetsAvailable);

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            auto const loanSleAfter = env.le(ctx.tinyLoanKeylet);
            if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(loanSleAfter))
                return;

            // PINNED LITERALS, harvested from a real run on this branch.
            //
            // These are the contract for the new-Vaults-only scope decision
            // (common §2.5): a Legacy Vault must produce EXACTLY these
            // numbers on both solution branches too. Neither solution may
            // touch a Vault without sfLEVersion == CashBasis, so any
            // deviation here means the version gate leaked.
            //
            // Do not "re-baseline" these if they fail on a solution branch.
            // A failure here is that branch's bug, or a finding about the
            // gate — never a stale expectation. Legacy uses accrual, so the
            // values legitimately differ from §4.3's cash-basis set.
            BEAST_EXPECT(poBefore == Number(1, -2));  // 0.01
            BEAST_EXPECT(
                loanSleAfter->at(sfPrincipalOutstanding) ==
                Number(5047592635, -12));                                // 0.005047592635
            BEAST_EXPECT(totalBefore == Number(2000000982550614, -11));  // 20000.00982550614
            BEAST_EXPECT(availBefore == Number(999998999953915, -11));   // 9999.98999953915
            BEAST_EXPECT(
                vaultSleAfter->at(sfAssetsAvailable) ==
                Number(999999514414651, -11));  // 9999.99514414651

            // Under accrual a regular on-schedule payment's
            // assetsTotalDelta is 0 (interest was recognized up front, at
            // origination), so AssetsTotal legitimately does not move;
            // AssetsAvailable always does, since cash is credited on every
            // repayment. Keep these as the statements of *why* the literals
            // above look the way they do.
            BEAST_EXPECT(loanSleAfter->at(sfPrincipalOutstanding) < poBefore);
            BEAST_EXPECT(poBefore > 0);
            BEAST_EXPECT(vaultSleAfter->at(sfAssetsAvailable) != availBefore);
            BEAST_EXPECT(vaultSleAfter->at(sfAssetsTotal) == totalBefore);
        });
    }

    // O2: the reservoir never exceeds one quantum. Trivially true here
    // (no reservoir exists here), but check every scale-refining
    // operation anyway, so the assertion shape is exercised.
    void
    testBoundedDust(FeatureBitset features)
    {
        testcase("O2: dust is always bounded by one quantum");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const checkBounded = [&]() {
                auto const vaultSle = env.le(ctx.broker.vaultKeylet());
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Number const d = readVaultDust(env, ctx.broker.vaultKeylet());
                Number const q{1, getVaultScale(vaultSle)};
                BEAST_EXPECT(d >= beast::kZero);
                BEAST_EXPECT(d < q);
            };

            checkBounded();
            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
            checkBounded();

            // A scale-refining removal (LoanManage default) can shrink q;
            // check boundedness holds afterwards too.
            env.close(std::chrono::seconds(86400 * 400));
            env(jtx::loan::manage(ctx.lender, ctx.bigLoanKeylet.key, tfLoanDefault));
            env.close();
            checkBounded();
        });
    }

    //--------------------------------------------------------------------
    // Tier 2 — the dust behaviour. These FAIL on the base branch and pass
    // on both solution branches; see the RED/GREEN CONTRACT note at the top.
    //--------------------------------------------------------------------

    // Common repayment scenario used by several tier-2 tests: pays off the
    // tiny loan and returns everything needed to evaluate O4/O5/O6/O8.
    //
    // Every field below is a raw OBSERVATION — nothing here is derived.
    // That is deliberate: `recognitionDelta` (r) and `raw` cannot be baked
    // into this struct once, because no single derivation is valid on both
    // this branch and a solution branch. Here, T += assetsTotalDelta is the
    // only write to sfAssetsTotal during LoanPay, so ΔT *is* r exactly,
    // with zero interference. On a solution branch the law is
    // ΔT = r − ΔD (maths doc §4), so ΔT alone is r only when ΔD == 0 — the
    // one case dust does NOT exist, which is exactly backwards for tests
    // whose entire purpose is the case where it does. Each caller's own
    // if-constexpr arm derives r/raw for itself, from quantities valid on
    // its own branch — see the two derivations below.
    struct RepaymentResult
    {
        Number totalBefore, totalAfter;                // sfAssetsTotal
        Number availBefore, availAfter;                // sfAssetsAvailable
        Number dustBefore, dustAfter;                  // readVaultDust
        Number poBefore, poAfter;                      // tiny loan's sfPrincipalOutstanding
        Number borrowerBefore, borrowerAfter;          // borrower's real balance
        Number feeRecipientBefore, feeRecipientAfter;  // see below
    };

    std::optional<RepaymentResult>
    payTinyLoanAndMeasure(jtx::Env& env, DustCtx const& ctx)
    {
        auto const vaultSleBefore = env.le(ctx.broker.vaultKeylet());
        auto const loanSleBefore = env.le(ctx.tinyLoanKeylet);
        auto const brokerSle = env.le(ctx.broker.brokerKeylet());
        if (!BEAST_EXPECT(vaultSleBefore) || !BEAST_EXPECT(loanSleBefore) ||
            !BEAST_EXPECT(brokerSle))
            return std::nullopt;
        // The management fee leg's destination is either the broker's
        // pseudo-account or its owner (LoanPay.cpp's sendBrokerFeeToOwner),
        // depending on cover sufficiency. Track the combined balance of
        // both possible destinations — a real balance, immune to any
        // accounting-field change — so callers can read off exactly what
        // the fee leg moved without knowing which one LoanPay picked.
        jtx::Account const brokerPseudo("brokerPseudo", brokerSle->at(sfAccount));

        Number const totalBefore = vaultSleBefore->at(sfAssetsTotal);
        Number const availBefore = vaultSleBefore->at(sfAssetsAvailable);
        Number const dustBefore = readVaultDust(env, ctx.broker.vaultKeylet());
        Number const poBefore = loanSleBefore->at(sfPrincipalOutstanding);
        Number const borrowerBefore = env.balance(ctx.borrower, ctx.asset).number();
        Number const feeRecipientBefore = env.balance(brokerPseudo, ctx.asset).number() +
            env.balance(ctx.lender, ctx.asset).number();

        payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

        auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
        auto const loanSleAfter = env.le(ctx.tinyLoanKeylet);
        if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(loanSleAfter))
            return std::nullopt;

        Number const totalAfter = vaultSleAfter->at(sfAssetsTotal);
        Number const availAfter = vaultSleAfter->at(sfAssetsAvailable);
        Number const dustAfter = readVaultDust(env, ctx.broker.vaultKeylet());
        Number const poAfter = loanSleAfter->at(sfPrincipalOutstanding);
        Number const borrowerAfter = env.balance(ctx.borrower, ctx.asset).number();
        Number const feeRecipientAfter = env.balance(brokerPseudo, ctx.asset).number() +
            env.balance(ctx.lender, ctx.asset).number();

        return RepaymentResult{
            .totalBefore = totalBefore,
            .totalAfter = totalAfter,
            .availBefore = availBefore,
            .availAfter = availAfter,
            .dustBefore = dustBefore,
            .dustAfter = dustAfter,
            .poBefore = poBefore,
            .poAfter = poAfter,
            .borrowerBefore = borrowerBefore,
            .borrowerAfter = borrowerAfter,
            .feeRecipientBefore = feeRecipientBefore,
            .feeRecipientAfter = feeRecipientAfter};
    }

    void
    testDustCreatedOnRepayment(FeatureBitset features)
    {
        testcase("Dust: a single repayment (O4, O5, O6)");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const r = payTinyLoanAndMeasure(env, ctx);
            if (!r)
                return;

            Number const dT = r->totalAfter - r->totalBefore;
            Number const dA = r->availAfter - r->availBefore;
            Number const dD = r->dustAfter - r->dustBefore;
            Number const p = r->poBefore - r->poAfter;

            // Post-fix, branch-independent: raw is read off REAL
            // balances — what the borrower actually parted with, minus
            // whatever the fee recipient actually received — never off
            // a Vault accounting field, so this holds regardless of
            // which dust mechanism is in force.
            Number const raw = -(r->borrowerAfter - r->borrowerBefore) -
                (r->feeRecipientAfter - r->feeRecipientBefore);
            Number const recognitionDelta = raw - p;
            BEAST_EXPECT((dT + dD) == recognitionDelta);          // O4
            BEAST_EXPECT((dA + dD) == raw);                       // O5
            BEAST_EXPECT((dT - dA) == (recognitionDelta - raw));  // O6

            // O8 corroboration (testsuite doc §3): needs no probe and no
            // r/raw derivation at all — T, A, and PO are read directly, so
            // this is the strongest and simplest check available. Prefer
            // it over O4/O5 wherever it applies; kept here alongside them
            // because this test's whole point is exercising O4/O5/O6.
            Number const po = principalOutstanding(env, ctx);
            Number const gap = r->totalAfter - r->availAfter - po;
            BEAST_EXPECT(gap == beast::kZero);
        });
    }

    void
    testDustAccumulatesAndIsRecognised(FeatureBitset features)
    {
        testcase("Dust accumulates and is eventually recognized");

        // Base branch: no reservoir to accumulate in, so this degrades to
        // repeating the single-repayment oracle check twice (the tiny loan
        // in the fixture only has one payment; there is nothing further to
        // accumulate against on this branch). Solution branches replace
        // this body with a real multi-repayment accumulation once they can
        // — see this test's contract in the else arm.
        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const r = payTinyLoanAndMeasure(env, ctx);
            if (!r)
                return;

            Number const p = r->poBefore - r->poAfter;
            // Branch-independent derivation — see testDustCreatedOnRepayment.
            Number const dT = r->totalAfter - r->totalBefore;
            Number const dA = r->availAfter - r->availBefore;
            Number const dD = r->dustAfter - r->dustBefore;
            Number const raw = -(r->borrowerAfter - r->borrowerBefore) -
                (r->feeRecipientAfter - r->feeRecipientBefore);
            Number const recognitionDelta = raw - p;
            BEAST_EXPECT((dT + dD) == recognitionDelta);
            BEAST_EXPECT((dA + dD) == raw);
        });
    }

    void
    testDustPromotionDoesNotInvertFields(FeatureBitset features)
    {
        testcase("Dust promotion never makes AssetsAvailable exceed AssetsTotal");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const r = payTinyLoanAndMeasure(env, ctx);
            if (!r)
                return;
            BEAST_EXPECT(r->availAfter <= r->totalAfter);
        });
    }

    void
    testDustDeferralDoesNotOverRecognise(FeatureBitset features)
    {
        testcase("Dust deferral never lets T rise by more than recognitionDelta");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const r = payTinyLoanAndMeasure(env, ctx);
            if (!r)
                return;
            Number const dT = r->totalAfter - r->totalBefore;
            Number const p = r->poBefore - r->poAfter;
            // Branch-independent derivation — see testDustCreatedOnRepayment.
            Number const raw = -(r->borrowerAfter - r->borrowerBefore) -
                (r->feeRecipientAfter - r->feeRecipientBefore);
            Number const recognitionDelta = raw - p;
            Number const dD = r->dustAfter - r->dustBefore;
            if (dD > beast::kZero)  // pure deferral, no promotion
                BEAST_EXPECT(dT < recognitionDelta);
            BEAST_EXPECT((dT + dD) == recognitionDelta);
        });
    }

    void
    testDustAtMagnitudeBoundary(FeatureBitset features)
    {
        testcase("Dust at a posterior-scale magnitude crossing");

        // Pins common §4.2a: on THIS branch the anterior scale is used
        // (unmodified), so this test only characterises today's behaviour
        // at a knife-edge rather than proving the posterior rule (that
        // rule does not exist here yet).
        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const vaultSle = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSle))
                return;
            std::int32_t const scaleBefore = getVaultScale(vaultSle);

            auto const r = payTinyLoanAndMeasure(env, ctx);
            if (!r)
                return;

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            std::int32_t const scaleAfter = getVaultScale(vaultSleAfter);

            // Not asserting a specific crossing happened (that depends on
            // the exact magnitudes involved); just that the scale
            // computation used before and after is self-consistent and
            // that dust stays bounded regardless.
            BEAST_EXPECT(scaleAfter <= scaleBefore);
            BEAST_EXPECT(readVaultDust(env, ctx.broker.vaultKeylet()) >= beast::kZero);
        });
    }

    // common §2.2: LoanManage::defaultLoan's own asymmetric rounding.
    void
    testDustOnDefault(FeatureBitset features)
    {
        testcase("Dust on LoanManage::defaultLoan (common §2.2)");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            // Let the big loan become late enough to default.
            env.close(std::chrono::seconds(86400 * 400));

            auto const vaultSleBefore = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleBefore))
                return;
            Number const totalBefore = vaultSleBefore->at(sfAssetsTotal);
            Number const availBefore = vaultSleBefore->at(sfAssetsAvailable);
            BEAST_EXPECT(availBefore <= totalBefore);

            env(jtx::loan::manage(ctx.lender, ctx.bigLoanKeylet.key, tfLoanDefault));
            env.close();

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSleAfter))
                return;
            Number const totalAfter = vaultSleAfter->at(sfAssetsTotal);
            Number const availAfter = vaultSleAfter->at(sfAssetsAvailable);

            // LoanManage.cpp:196-212's existing dust-manipulation workaround
            // already snaps AssetsTotal up to AssetsAvailable when the gap
            // looks like dust. This is pre-existing behaviour that this
            // branch does not touch (common §2.2 explicitly forbids
            // removing it).
            BEAST_EXPECT(availAfter <= totalAfter);
        });
    }

    void
    testTerminalWithdrawalDrainsDust(FeatureBitset features)
    {
        testcase("O7: terminal withdrawal drains AssetsTotal/Available/Dust");

        using namespace jtx;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        env.fund(XRP(1'000'000), issuer, lender);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(1'000'000)));
        env.close();
        env(pay(issuer, lender, asset(500'000)));
        env.close();

        Vault const vault{env};
        auto [vaultTx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(vaultTx);
        env.close();

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const vaultSleBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        STAmount const allAssets{asset.raw(), vaultSleBefore->at(sfAssetsAvailable)};

        env(vault.withdraw({.depositor = lender, .id = vaultKeylet.key, .amount = allAssets}));
        env.close();

        auto const vaultSleAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleAfter))
            return;

        BEAST_EXPECT(vaultSleAfter->at(sfAssetsAvailable) == beast::kZero);
        BEAST_EXPECT(vaultSleAfter->at(sfAssetsTotal) == beast::kZero);
        BEAST_EXPECT(readVaultDust(env, vaultKeylet) == beast::kZero);
        BEAST_EXPECT(
            accountHolds(
                *env.current(),
                vaultSleAfter->at(sfAccount),
                asset.raw(),
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                beast::Journal{beast::Journal::getNullSink()}) == beast::kZero);
    }

    void
    testBooksBalance(FeatureBitset features)
    {
        testcase("O8: T == A + PO, exactly (cash-basis only)");

        // O8 is only exact for cash-basis Vaults (testsuite doc §3
        // precondition 1). featureLendingProtocolV1_1 is excluded from
        // `all_` by LoanTestBase, so opt it back in explicitly.
        withDustSetup(
            features | featureLendingProtocolV1_1, [&](jtx::Env& env, DustCtx const& ctx) {
                auto const vaultSle = env.le(ctx.broker.vaultKeylet());
                if (!BEAST_EXPECT(vaultSle))
                    return;
                BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::CashBasis);

                auto const check = [&](bool exact) {
                    auto const v = env.le(ctx.broker.vaultKeylet());
                    if (!BEAST_EXPECT(v))
                        return;
                    Number const t = v->at(sfAssetsTotal);
                    Number const a = v->at(sfAssetsAvailable);
                    Number const po = principalOutstanding(env, ctx);
                    Number const gap = t - a - po;
                    if (exact)
                    {
                        BEAST_EXPECT(gap == beast::kZero);
                    }
                    else
                    {
                        // Pre-fix arm: the gap is the accumulated, unaccounted
                        // dust from every repayment — the leak, measured
                        // directly, with no probe at all.
                        Number const q{1, getVaultScale(v)};
                        BEAST_EXPECT(gap >= beast::kZero);
                        BEAST_EXPECT(gap < q * 2);  // one loan repaid so far
                    }
                };

                check(/*exact=*/true);  // holds from creation, before any dust-producing op
                payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);
                check(/*exact=*/true);
            });
    }

    //--------------------------------------------------------------------
    // Tier 3 — characterization, written first, on the base branch.
    //--------------------------------------------------------------------

    // The single most valuable artefact on this branch: exact literals from
    // a real run, pinning today's (buggy) behaviour so both solution
    // branches, and testLegacyVaultUnchanged on each of them, have a
    // trustworthy reference. See the base-branch plan §4 / testsuite doc
    // §4.3. Values below were harvested from an actual run of this fixture
    // — do not hand-edit them.
    void
    testCharacterizeCurrentRounding(FeatureBitset features)
    {
        testcase("Characterise today's rounding leak (harvested literals)");

        withDustSetup(features, [&](jtx::Env& env, DustCtx const& ctx) {
            auto const vaultSleBefore = env.le(ctx.broker.vaultKeylet());
            auto const loanSleBefore = env.le(ctx.tinyLoanKeylet);
            if (!BEAST_EXPECT(vaultSleBefore) || !BEAST_EXPECT(loanSleBefore))
                return;

            auto const pseudo = vaultPseudoAccount(env, ctx.broker.vaultKeylet());
            auto const brokerSle = env.le(ctx.broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerSle))
                return;
            jtx::Account const brokerPseudo("brokerPseudo", brokerSle->at(sfAccount));

            Number const principalBefore = loanSleBefore->at(sfPrincipalOutstanding);
            Number const totalValueBefore = loanSleBefore->at(sfTotalValueOutstanding);
            Number const assetsTotalBefore = vaultSleBefore->at(sfAssetsTotal);
            Number const assetsAvailBefore = vaultSleBefore->at(sfAssetsAvailable);
            Number const pseudoBefore = env.balance(pseudo, ctx.asset).number();
            Number const borrowerBefore = env.balance(ctx.borrower, ctx.asset).number();
            Number const brokerBefore = env.balance(brokerPseudo, ctx.asset).number();
            Number const lenderBefore = env.balance(ctx.lender, ctx.asset).number();

            payLoanInFull(env, ctx.borrower, ctx.asset.raw(), ctx.tinyLoanKeylet);

            auto const vaultSleAfter = env.le(ctx.broker.vaultKeylet());
            auto const loanSleAfter = env.le(ctx.tinyLoanKeylet);
            if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(loanSleAfter))
                return;

            Number const principalAfter = loanSleAfter->at(sfPrincipalOutstanding);
            Number const totalValueAfter = loanSleAfter->at(sfTotalValueOutstanding);
            Number const assetsTotalAfter = vaultSleAfter->at(sfAssetsTotal);
            Number const assetsAvailAfter = vaultSleAfter->at(sfAssetsAvailable);
            Number const pseudoAfter = env.balance(pseudo, ctx.asset).number();
            Number const borrowerAfter = env.balance(ctx.borrower, ctx.asset).number();
            Number const brokerAfter = env.balance(brokerPseudo, ctx.asset).number();
            Number const lenderAfter = env.balance(ctx.lender, ctx.asset).number();

            log << "VaultRounding characterization ---------------------------" << std::endl;
            log << "  Loan PrincipalOutstanding:    " << principalBefore << " -> " << principalAfter
                << std::endl;
            log << "  Loan TotalValueOutstanding:    " << totalValueBefore << " -> "
                << totalValueAfter << std::endl;
            log << "  Vault AssetsTotal:             " << assetsTotalBefore << " -> "
                << assetsTotalAfter << std::endl;
            log << "  Vault AssetsAvailable:         " << assetsAvailBefore << " -> "
                << assetsAvailAfter << std::endl;
            log << "  Vault pseudo balance:          " << pseudoBefore << " -> " << pseudoAfter
                << std::endl;
            log << "  Borrower balance:              " << borrowerBefore << " -> " << borrowerAfter
                << std::endl;
            log << "  Broker pseudo balance:         " << brokerBefore << " -> " << brokerAfter
                << std::endl;
            log << "  Lender (broker owner) balance: " << lenderBefore << " -> " << lenderAfter
                << std::endl;

            // This test's whole purpose is characterising the leak that
            // exists ONLY pre-fix, so the leak-measuring arithmetic below
            // is deliberately confined to the pre-fix arm and must not be
            // asked to mean anything on a solution branch — see
            // RepaymentResult's comment for why a ΔAssetsTotal-based `raw`
            // is valid here and nowhere else.
            // Post-fix, branch-independent (O8, testsuite doc §3): once
            // the dust mechanism exists, the leak this test pins
            // pre-fix must be gone — T should equal A plus every Loan's
            // own outstanding principal, exactly, with no probe and no
            // r/raw derivation needed at all.
            Number const po = principalOutstanding(env, ctx);
            log << "  T - A - PO (should be exactly 0 post-fix): "
                << (assetsTotalAfter - assetsAvailAfter - po) << std::endl;
            BEAST_EXPECT(assetsTotalAfter == assetsAvailAfter + po);
        });
    }

    //--------------------------------------------------------------------
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        // Dust is scoped to cash-basis Vaults (common §2.5), and the
        // recognitionDelta arithmetic these tests derive from the Loan's
        // own state (payTinyLoanAndMeasure) is the cash-basis formula
        // (recognitionDelta == interestPaid at repayment) — under accrual,
        // a regular on-schedule payment's assetsTotalDelta is 0 instead
        // (interest was recognized up front, at origination). So every
        // fixture-based test below opts featureLendingProtocolV1_1 back in,
        // except testLegacyVaultUnchanged, whose entire point is to
        // exercise the *other* model.
        FeatureBitset const cashBasis = features | featureLendingProtocolV1_1;

        // The bug in one test — read this one first.
        testDustDisappears(cashBasis);

        testMirrorHoldsAcrossLoanLifecycle(cashBasis);
        testFundsConserved(cashBasis);
        testIntegralAssetsProduceNoDust(features);
        testLegacyVaultUnchanged(features);
        testBoundedDust(cashBasis);

        testDustCreatedOnRepayment(cashBasis);
        testDustAccumulatesAndIsRecognised(cashBasis);
        testDustPromotionDoesNotInvertFields(cashBasis);
        testDustDeferralDoesNotOverRecognise(cashBasis);
        testDustAtMagnitudeBoundary(cashBasis);
        testDustOnDefault(cashBasis);
        testTerminalWithdrawalDrainsDust(features);
        testBooksBalance(features);  // already opts in itself, see below

        testCharacterizeCurrentRounding(cashBasis);
    }

public:
    void
    run() override
    {
        runAmendmentSensitive(all_);
    }
};

BEAST_DEFINE_TESTSUITE(VaultRounding, tx, xrpl);

}  // namespace xrpl::test
