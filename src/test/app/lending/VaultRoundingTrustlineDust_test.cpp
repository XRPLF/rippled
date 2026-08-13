#include <test/app/lending/LoanTestBase.h>
#include <test/app/lending/VaultDustProbe.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/Units.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

// ============================================================================
// VaultRoundingTrustlineDust_test.cpp — solution B'-specific tests
// (docs/plan-vault-dust-b-prime-field-accounting-kept.md §13). This file is
// NOT shared: it is free to reference sfDust, DustSplit, useVaultDust, and
// friends directly, unlike src/test/app/lending/VaultRounding_test.cpp.
// ============================================================================

namespace xrpl::test {

class VaultRoundingTrustlineDust_test : public LoanTestBase
{
private:
    // Guards against silent-pass fixture failure. Every test that
    // exercises a dust-producing scenario increments this counter when
    // it confirms the fixture actually produced non-zero sfDust. If the
    // fixture regresses (loan/vault scales drift, promotion path
    // changes), the individual tests would each silently `return;`
    // (see the `if (dust == kZero) return;` early-outs below) and this
    // whole suite would appear green while covering nothing. run()
    // asserts a floor at the end.
    int dustObservations_ = 0;

    struct DustFixture
    {
        jtx::Account issuer;
        jtx::Account lender;
        jtx::Account borrower;
        jtx::PrettyAsset asset;
        BrokerInfo broker;
        Keylet tinyLoanKeylet;
    };

    // Same recipe as VaultRounding_test.cpp's withDustSetup (testsuite doc
    // §5), parameterized by owner-account NAME so callers can search for
    // both trust-line sign orientations (plan §13.2): the vault
    // pseudo-account's address is a hash that depends on the owner and
    // ledger state, so varying the owner varies which side of the issuer
    // the pseudo-account lands on.
    std::optional<DustFixture>
    makeDustFixture(jtx::Env& env, std::string const& ownerSuffix)
    {
        using namespace jtx;
        using namespace loan;

        Account const issuer{"issuer" + ownerSuffix};
        Account const lender{"lender" + ownerSuffix};
        Account const borrower{"borrower" + ownerSuffix};
        env.fund(XRP(1'000'000'00), issuer, lender, borrower);
        env.close();

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
            .managementFeeRate = TenthBips16{0}};

        BrokerInfo const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        auto const brokerSle1 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle1))
            return std::nullopt;
        Keylet const tinyLoanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle1->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, Number{1, -2}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{1'922}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        Vault const vault{env};
        env(vault.deposit(
            {.depositor = lender, .id = broker.vaultKeylet().key, .amount = asset(9'500)}));
        env.close();

        auto const tinyLoanSle = env.le(tinyLoanKeylet);
        auto const vaultSle = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(tinyLoanSle) || !BEAST_EXPECT(vaultSle))
            return std::nullopt;
        if (tinyLoanSle->at(sfLoanScale) != -12 || getVaultScale(vaultSle) != -11)
        {
            log << "VaultRoundingTrustlineDust: fixture did not reproduce -12/-11 "
                   "for owner suffix '"
                << ownerSuffix << "'" << std::endl;
            return std::nullopt;
        }

        return DustFixture{
            .issuer = issuer,
            .lender = lender,
            .borrower = borrower,
            .asset = asset,
            .broker = broker,
            .tinyLoanKeylet = tinyLoanKeylet};
    }

    void
    payTinyLoanInFull(jtx::Env& env, DustFixture const& fx)
    {
        using namespace jtx;
        auto const loanSle = env.le(fx.tinyLoanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        auto const periodicPayment = loanSle->at(sfPeriodicPayment);
        auto const serviceFee = loanSle->at(sfLoanServiceFee);
        std::int32_t const loanScale = loanSle->at(sfLoanScale);
        auto const payment = roundPeriodicPayment(fx.asset.raw(), periodicPayment, loanScale);
        auto const payAmt = STAmount{fx.asset.raw(), payment + serviceFee};
        env(jtx::loan::pay(fx.borrower, fx.tinyLoanKeylet.key, payAmt), Fee(XRP(10)));
        env.close();
    }

    //--------------------------------------------------------------------
    // Multi-loan fixture and helpers.
    //
    // Goal: create three Loans on the SAME Vault/Broker whose sfLoanScale
    // values differ. Because sfLoanScale is
    //     std::max(vaultScaleAtLoanCreation, amount.exponent())
    // (see xrpl::computeLoanProperties), we drive the vault posterior
    // scale (which is derived from sfAssetsTotal via STAmount's canonical
    // exponent) to a coarser value by depositing between loan creations.
    // Concretely:
    //   * loanA is created when Vault T == 1'000  -> vault scale = -12,
    //     so loanA.sfLoanScale = -12.
    //   * A big deposit lifts T to ~10'000        -> vault scale = -11.
    //   * loanB is created                        -> loanB.sfLoanScale
    //                                                = -11.
    //   * Another deposit lifts T to ~100'000    -> vault scale = -10.
    //   * loanC is created                        -> loanC.sfLoanScale
    //                                                = -10.
    // Every loan's periodic payment carries sub-quantum digits (interest
    // that will not round to the vault's posterior scale exactly), so
    // interleaved repayments exercise dust accumulation across scales.
    //--------------------------------------------------------------------
    struct MultiLoanDustFixture
    {
        jtx::Account issuer;
        jtx::Account lender;
        jtx::Account borrower;
        jtx::PrettyAsset asset;
        BrokerInfo broker;
        // In creation order, each at a distinct (progressively coarser)
        // sfLoanScale.
        Keylet loanA;
        Keylet loanB;
        Keylet loanC;
        // Loan scales captured at fixture time (fixture must have
        // observed at least two distinct scales or it fails).
        std::int32_t loanAScale;
        std::int32_t loanBScale;
        std::int32_t loanCScale;
    };

    std::optional<MultiLoanDustFixture>
    makeMultiLoanDustFixture(jtx::Env& env, std::string const& ownerSuffix)
    {
        using namespace jtx;
        using namespace loan;

        Account const issuer{"issuer_ml" + ownerSuffix};
        Account const lender{"lender_ml" + ownerSuffix};
        Account const borrower{"borrower_ml" + ownerSuffix};
        env.fund(XRP(1'000'000'00), issuer, lender, borrower);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(2'000'000)));
        env(trust(borrower, asset(2'000'000)));
        env.close();
        // Lender needs enough to seed the vault and top it up between
        // loan creations; borrower needs enough to service every loan.
        env(pay(issuer, lender, asset(500'000)));
        env(pay(issuer, borrower, asset(1'000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = Number{0},  // 0 = unlimited (LoanSet:498)
            .coverRateMin = TenthBips32{13'370},
            .coverDeposit = 5'000,
            .managementFeeRate = TenthBips16{0}};
        BrokerInfo const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        // -- loanA: created at T == 1'000, vault scale = -12.
        auto const brokerSle0 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle0))
            return std::nullopt;
        Keylet const loanAKl =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle0->at(sfLoanSequence)));
        env(set(borrower, broker.brokerID, Number{1, -2}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{1'922}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        // Push the vault to a coarser scale before creating loanB.
        Vault const vault{env};
        env(vault.deposit(
            {.depositor = lender, .id = broker.vaultKeylet().key, .amount = asset(9'000)}));
        env.close();

        // -- loanB: created at T ~ 10'000, vault scale = -11.
        auto const brokerSle1 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle1))
            return std::nullopt;
        Keylet const loanBKl =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle1->at(sfLoanSequence)));
        env(set(borrower, broker.brokerID, Number{5, -2}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{2'537}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        // Push again for loanC.
        env(vault.deposit(
            {.depositor = lender, .id = broker.vaultKeylet().key, .amount = asset(90'000)}));
        env.close();

        // -- loanC: created at T ~ 100'000, vault scale = -10.
        auto const brokerSle2 = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle2))
            return std::nullopt;
        Keylet const loanCKl =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle2->at(sfLoanSequence)));
        env(set(borrower, broker.brokerID, Number{1, -3}),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{1'341}),
            kPaymentTotal(2),
            kPaymentInterval(86400 * 365),
            Fee(XRP(10)));
        env.close();

        // Health checks — read each loan's captured scale and refuse the
        // fixture if the multi-scale narrative did not materialise, so
        // the test does not silently pass on a degenerate setup.
        auto const readScale = [&](Keylet const& kl) -> std::optional<std::int32_t> {
            auto const sle = env.le(kl);
            if (!BEAST_EXPECT(sle))
                return std::nullopt;
            return sle->at(sfLoanScale);
        };
        auto const scaleA = readScale(loanAKl);
        auto const scaleB = readScale(loanBKl);
        auto const scaleC = readScale(loanCKl);
        if (!scaleA || !scaleB || !scaleC)
            return std::nullopt;

        auto const vaultSle = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSle))
            return std::nullopt;
        int const vaultScale = getVaultScale(vaultSle);

        // Require at least two distinct loan scales — this is what makes
        // the multi-loan scale-drift narrative non-vacuous.
        if (*scaleA == *scaleB && *scaleB == *scaleC)
        {
            log << "MultiLoanDust fixture: loan scales did not diverge for "
                   "suffix '"
                << ownerSuffix << "': " << *scaleA << "/" << *scaleB << "/" << *scaleC
                << " (vault=" << vaultScale << ")" << std::endl;
            return std::nullopt;
        }

        return MultiLoanDustFixture{
            .issuer = issuer,
            .lender = lender,
            .borrower = borrower,
            .asset = asset,
            .broker = broker,
            .loanA = loanAKl,
            .loanB = loanBKl,
            .loanC = loanCKl,
            .loanAScale = *scaleA,
            .loanBScale = *scaleB,
            .loanCScale = *scaleC};
    }

    // Pay one regular installment for the specified loan. Modelled on
    // payTinyLoanInFull but parameterised by loan keylet so the caller
    // can interleave payments across the three fixture loans.
    void
    payOneInstallment(jtx::Env& env, MultiLoanDustFixture const& fx, Keylet const& loanKeylet)
    {
        using namespace jtx;
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        auto const periodicPayment = loanSle->at(sfPeriodicPayment);
        auto const serviceFee = loanSle->at(sfLoanServiceFee);
        std::int32_t const loanScale = loanSle->at(sfLoanScale);
        auto const payment = roundPeriodicPayment(fx.asset.raw(), periodicPayment, loanScale);
        auto const payAmt = STAmount{fx.asset.raw(), payment + serviceFee};
        env(jtx::loan::pay(fx.borrower, loanKeylet.key, payAmt), Fee(XRP(10)));
        env.close();
    }

    // Sum sfPrincipalOutstanding across every fixture Loan that is still
    // on-ledger. A fully-paid Loan that has been deleted is treated as
    // contributing zero (its principal debt is discharged), which is the
    // ValidVault contract: `sfAssetsTotal - sfAssetsAvailable ==
    // Σ live-Loan sfPrincipalOutstanding`.
    Number
    sumPrincipalOutstanding(jtx::Env const& env, MultiLoanDustFixture const& fx) const
    {
        Number total{};
        for (auto const& k : {fx.loanA, fx.loanB, fx.loanC})
        {
            if (auto const loanSle = env.le(k))
                total += loanSle->at(sfPrincipalOutstanding);
        }
        return total;
    }

    // Assert the O8 (multi-loan) invariants at a checkpoint. Any
    // failure includes `label` so a failing step is identifiable in the
    // test output. Returns true iff all invariants held.
    //
    // NOTE ON EXTENDED BOOKKEEPING:
    // sfAssetsTotal and sfAssetsAvailable are stored on the vault SLE at
    // *ledger-recognised* precision; the sub-quantum residual living on
    // the custody line's sfDust is not folded into either field. When
    // reconciling against the loan book, however, we treat both fields
    // as their *extended* forms — adding the vault-terms dust to BOTH T
    // and A. The dust cancels in the subtraction (T + d) - (A + d) =
    // T - A, so the numerical identity is unchanged, but the framing is
    // now symmetric: the receivable is computed from extended totals on
    // both sides. This makes the invariant robust against any future
    // change that would fold dust into either field individually — the
    // extended form always holds, regardless of which side of the
    // dust/recognised split any given field lands on.
    //
    //   O8.1  Extended receivable identity:
    //         (T + d) - (A + d) == Σ sfPrincipalOutstanding
    //         ≡ T - A == Σ sfPrincipalOutstanding
    //         (Number precision — exact equality).
    //
    //   O8.2  Dust bound at the *vault's own posterior scale*:
    //         |sfDust (vault terms)| < 1 quantum at vaultScale.
    //         Note: after Override reconciliation across scale drift the
    //         reservoir can transiently equal one quantum's worth of a
    //         coarser scale, so we take a one-decade slack: strictly
    //         less than 10 * quantum. This matches the O2 relaxation
    //         used elsewhere in this file
    //         (testSenderLegOverrideNonTerminalPromotes).
    //
    //   O8.3  Custody-line mirror (extended): the vault's extended
    //         available balance (A + d) must equal the vault
    //         pseudo-account's extended custody-line balance
    //         (accountHolds + d). Equivalent to
    //         accountHolds == sfAssetsAvailable, since the same dust
    //         reservoir appears on both sides — but expressed in
    //         extended form so a regression that dips one side into
    //         the reservoir without the other still surfaces.
    bool
    assertO8Exact(jtx::Env const& env, MultiLoanDustFixture const& fx, std::string const& label)
    {
        auto const vaultSle = env.le(fx.broker.vaultKeylet());
        if (!BEAST_EXPECTS(vaultSle, label + ": vault SLE missing"))
            return false;

        Number const T = vaultSle->at(sfAssetsTotal);
        Number const A = vaultSle->at(sfAssetsAvailable);
        Number const dust = readVaultDust(env, fx.broker.vaultKeylet());
        Number const extT = T + dust;
        Number const extA = A + dust;
        Number const poSum = sumPrincipalOutstanding(env, fx);

        // Extended O8.1 — dust appears on both operands and cancels.
        bool const o81 = ((extT - extA) == poSum);
        BEAST_EXPECTS(
            o81,
            label + ": O8.1 (T+d) - (A+d) != Σ PO. T=" + to_string(T) + " A=" + to_string(A) +
                " d=" + to_string(dust) + " Σ PO=" + to_string(poSum));

        int const vaultScale = getVaultScale(vaultSle);
        Number const oneDecadeAtScale{10, vaultScale};
        bool const o82 = (dust < oneDecadeAtScale) && (dust > -oneDecadeAtScale);
        BEAST_EXPECTS(
            o82,
            label + ": O8.2 |dust| >= 10*quantum. dust=" + to_string(dust) +
                " scale=" + std::to_string(vaultScale));

        AccountID const vaultAccount = vaultSle->at(sfAccount);
        Number const holds = accountHolds(
            *env.current(),
            vaultAccount,
            fx.asset.raw(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            beast::Journal{beast::Journal::getNullSink()});
        Number const extHolds = holds + dust;
        bool const o83 = (extHolds == extA);
        BEAST_EXPECTS(
            o83,
            label +
                ": O8.3 extended accountHolds != extended sfAssetsAvailable. "
                "holds+d=" +
                to_string(extHolds) + " A+d=" + to_string(extA));

        return o81 && o82 && o83;
    }

    // ------------------------------------------------------------------
    // Multi-loan STATE INSTRUMENTATION
    //
    // A compact snapshot of every Vault and Loan field that the O8
    // narrative touches, plus a pretty-printer that emits a labelled
    // block through the beast::unit_test log stream. When a `prev`
    // snapshot is provided, deltas are printed alongside the absolute
    // values, so scale drift, dust promotion, and PO progression are
    // visible at a glance.
    //
    // Every value is emitted at Number precision (to_string(Number)).
    // Deleted Loans are reported as "(deleted)" so a fully-paid Loan is
    // visually distinct from a zero-PO live one.
    // ------------------------------------------------------------------
    struct LoanSnap
    {
        bool present = false;
        Number po{};                         // sfPrincipalOutstanding
        Number periodicPayment{};            // sfPeriodicPayment
        std::uint32_t paymentRemaining = 0;  // sfPaymentRemaining
        std::int32_t loanScale = 0;          // sfLoanScale
    };

    struct MultiLoanSnap
    {
        // Vault-side (all Number-precision on-ledger fields).
        Number T{};                // sfAssetsTotal
        Number A{};                // sfAssetsAvailable
        Number dust{};             // readVaultDust (vault terms)
        int vaultScale = 0;        // getVaultScale(vault)
        Number custodyRaw{};       // sfBalance on custody line (raw)
        Number custodyDustRaw{};   // sfDust on custody line (raw)
        Number accountHoldsVal{};  // accountHolds(vault, asset)

        // Loan-side (indexed A/B/C).
        LoanSnap a{}, b{}, c{};
    };

    MultiLoanSnap
    snapshotMultiLoan(jtx::Env const& env, MultiLoanDustFixture const& fx) const
    {
        MultiLoanSnap s{};
        auto const vaultSle = env.le(fx.broker.vaultKeylet());
        if (!vaultSle)
            return s;

        s.T = vaultSle->at(sfAssetsTotal);
        s.A = vaultSle->at(sfAssetsAvailable);
        s.dust = readVaultDust(env, fx.broker.vaultKeylet());
        s.vaultScale = getVaultScale(vaultSle);

        AccountID const vaultAccount = vaultSle->at(sfAccount);
        Issue const iouIssue = fx.asset.raw().get<Issue>();
        s.accountHoldsVal = accountHolds(
            *env.current(),
            vaultAccount,
            fx.asset.raw(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            beast::Journal{beast::Journal::getNullSink()});

        if (auto const line = env.le(keylet::trustLine(vaultAccount, iouIssue)))
        {
            s.custodyRaw = Number{line->at(sfBalance)};
            s.custodyDustRaw = line->at(sfDust);
        }

        auto readLoan = [&](Keylet const& kl, LoanSnap& out) {
            auto const sle = env.le(kl);
            if (!sle)
                return;
            out.present = true;
            out.po = sle->at(sfPrincipalOutstanding);
            out.periodicPayment = sle->at(sfPeriodicPayment);
            out.paymentRemaining = sle->at(sfPaymentRemaining);
            out.loanScale = sle->at(sfLoanScale);
        };
        readLoan(fx.loanA, s.a);
        readLoan(fx.loanB, s.b);
        readLoan(fx.loanC, s.c);

        return s;
    }

    // Format `cur` — and, if provided, a delta from `prev` — as
    // "value (+/-diff)". If prev is null (no baseline), emit just the
    // value.
    static std::string
    fmtNum(Number const& cur, Number const* prev)
    {
        if (prev == nullptr)
            return to_string(cur);
        Number const d = cur - *prev;
        if (d == beast::kZero)
            return to_string(cur) + " (unchanged)";
        std::string const sign = (d > beast::kZero) ? "+" : "";
        return to_string(cur) + " (" + sign + to_string(d) + ")";
    }

    static std::string
    fmtScale(int cur, int const* prev)
    {
        auto const s = std::to_string(cur);
        if (prev == nullptr || *prev == cur)
            return s;
        return s + " (was " + std::to_string(*prev) + ")";
    }

    static std::string
    fmtLoan(LoanSnap const& cur, LoanSnap const* prev)
    {
        if (!cur.present)
            return "(deleted)";
        std::string out;
        Number const* prevPo = (prev && prev->present) ? &prev->po : nullptr;
        Number const* prevPP = (prev && prev->present) ? &prev->periodicPayment : nullptr;
        out += "PO=" + fmtNum(cur.po, prevPo);
        out += ", periodicPayment=" + fmtNum(cur.periodicPayment, prevPP);
        out += ", paymentsRemaining=" + std::to_string(cur.paymentRemaining);
        if (prev && prev->present && prev->paymentRemaining != cur.paymentRemaining)
        {
            out += " (was " + std::to_string(prev->paymentRemaining) + ")";
        }
        out += ", loanScale=" + std::to_string(cur.loanScale);
        return out;
    }

    void
    dumpMultiLoanState(
        std::string const& label,
        MultiLoanSnap const& cur,
        MultiLoanSnap const* prev = nullptr)
    {
        Number const extT = cur.T + cur.dust;
        Number const extA = cur.A + cur.dust;
        Number const extReceivable = extT - extA;  // == cur.T - cur.A
        Number const poSum = (cur.a.present ? cur.a.po : Number{}) +
            (cur.b.present ? cur.b.po : Number{}) + (cur.c.present ? cur.c.po : Number{});
        Number const extResidual = extReceivable - poSum;  // Extended O8.1

        Number const prevExtT = prev ? Number{prev->T + prev->dust} : Number{};
        Number const prevExtA = prev ? Number{prev->A + prev->dust} : Number{};

        Number const* pT = prev ? &prev->T : nullptr;
        Number const* pA = prev ? &prev->A : nullptr;
        Number const* pDust = prev ? &prev->dust : nullptr;
        Number const* pHolds = prev ? &prev->accountHoldsVal : nullptr;
        Number const* pCustody = prev ? &prev->custodyRaw : nullptr;
        Number const* pCustodyDust = prev ? &prev->custodyDustRaw : nullptr;
        Number const* pExtT = prev ? &prevExtT : nullptr;
        Number const* pExtA = prev ? &prevExtA : nullptr;
        int const* pScale = prev ? &prev->vaultScale : nullptr;

        log << "\n--- " << label << " ---\n"
            << "  Vault.sfAssetsTotal     T = " << fmtNum(cur.T, pT) << "\n"
            << "  Vault.sfAssetsAvailable A = " << fmtNum(cur.A, pA) << "\n"
            << "  Vault dust (vault terms)d = " << fmtNum(cur.dust, pDust) << "\n"
            << "  Extended  T+d             = " << fmtNum(extT, pExtT) << "\n"
            << "  Extended  A+d             = " << fmtNum(extA, pExtA) << "\n"
            << "  Vault posterior scale     = " << fmtScale(cur.vaultScale, pScale)
            << " (quantum = 10^" << cur.vaultScale << ")\n"
            << "  Custody sfBalance (raw)   = " << fmtNum(cur.custodyRaw, pCustody) << "\n"
            << "  Custody sfDust    (raw)   = " << fmtNum(cur.custodyDustRaw, pCustodyDust) << "\n"
            << "  accountHolds(vault, USD)  = " << fmtNum(cur.accountHoldsVal, pHolds) << "\n"
            << "  receivable (T+d) - (A+d)  = " << to_string(extReceivable) << "\n"
            << "  Σ sfPrincipalOutstanding  = " << to_string(poSum) << "\n"
            << "  O8.1 residual (ext-recv-ΣPO)= " << to_string(extResidual) << " (must be 0)\n"
            << "  Loan A: " << fmtLoan(cur.a, prev ? &prev->a : nullptr) << "\n"
            << "  Loan B: " << fmtLoan(cur.b, prev ? &prev->b : nullptr) << "\n"
            << "  Loan C: " << fmtLoan(cur.c, prev ? &prev->c : nullptr) << std::endl;
    }

    //--------------------------------------------------------------------
    // §13.1 The field
    //
    // testDustAbsentReadsZero and testSfDustGoldenByteCompat live in
    // src/test/app/RippleStateSfDust_test.cpp (protocol-level tests
    // that stand alone without the vault_dust:: overlay).
    //--------------------------------------------------------------------

    void
    testNoDustForLegacyOrIntegralVaults(FeatureBitset features)
    {
        testcase("Legacy and integral-asset vaults never carry sfDust");

        using namespace jtx;
        Env env{*this, features};  // featureLendingProtocolV1_1 excluded => Legacy
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        env.fund(XRP(1'000'000), issuer, lender);
        env.close();
        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(500'000)));
        env.close();
        env(pay(issuer, lender, asset(400'000)));
        env.close();

        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::Legacy);
        BEAST_EXPECT(!vault_dust::useVaultDust(*env.current(), vaultSle));
        BEAST_EXPECT(readVaultDust(env, vaultKeylet) == beast::kZero);
    }

    //--------------------------------------------------------------------
    // §13.2 Sign convention — both orientations, explicitly forced.
    //--------------------------------------------------------------------

    void
    testDustBothSignOrientations(FeatureBitset features)
    {
        testcase("Dust lifecycle holds with the Vault as both low and high account");

        using namespace jtx;

        bool sawLow = false, sawHigh = false;
        for (int attempt = 0; attempt < 12 && !(sawLow && sawHigh); ++attempt)
        {
            Env env{*this, features | featureLendingProtocolV1_1};
            auto const fx = makeDustFixture(env, std::to_string(attempt));
            if (!fx)
                continue;

            auto const vaultSle = env.le(fx->broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSle))
                continue;
            AccountID const vaultAccount = vaultSle->at(sfAccount);
            AccountID const issuerAccount = fx->issuer.id();
            bool const vaultIsHigh = vaultAccount > issuerAccount;

            if (vaultIsHigh && sawHigh)
                continue;
            if (!vaultIsHigh && sawLow)
                continue;

            payTinyLoanInFull(env, *fx);

            Number const dust = readVaultDust(env, fx->broker.vaultKeylet());
            if (dust == beast::kZero)
                continue;  // this attempt didn't generate dust; try another
            ++dustObservations_;

            // Read the raw ledger field and confirm the probe undid the
            // low/high convention correctly: the raw sfDust value's sign
            // (in the line's own convention, positive = low account holds
            // high account's IOUs) must match vaultIsHigh appropriately.
            auto const line = env.le(
                keylet::trustLine(
                    vaultAccount, issuerAccount, fx->asset.raw().get<Issue>().currency));
            if (!BEAST_EXPECT(line))
                continue;
            Number const rawDust = line->at(sfDust);

            if (vaultIsHigh)
            {
                sawHigh = true;
                // Vault-positive dust (dust > 0, meaning the Vault is
                // carrying unrecognized value) corresponds to a NEGATIVE
                // raw field when the Vault is the high account, since the
                // raw convention is "low account's terms".
                BEAST_EXPECT((dust > beast::kZero) == (rawDust < beast::kZero));
            }
            else
            {
                sawLow = true;
                BEAST_EXPECT((dust > beast::kZero) == (rawDust > beast::kZero));
            }

            // The mirror still holds regardless of orientation (O1).
            Number const avail = vaultSle->at(sfAssetsAvailable);
            (void)avail;
            auto const vaultSleAfter = env.le(fx->broker.vaultKeylet());
            if (BEAST_EXPECT(vaultSleAfter))
            {
                Number const a = vaultSleAfter->at(sfAssetsAvailable);
                Number const b = accountHolds(
                    *env.current(),
                    vaultSleAfter->at(sfAccount),
                    fx->asset.raw(),
                    FreezeHandling::IgnoreFreeze,
                    AuthHandling::IgnoreAuth,
                    beast::Journal{beast::Journal::getNullSink()});
                BEAST_EXPECT(a == b);
            }
        }

        BEAST_EXPECT(sawLow);
        BEAST_EXPECT(sawHigh);
    }

    //--------------------------------------------------------------------
    // §13.3 The credit path (driven through real transactions)
    //--------------------------------------------------------------------

    void
    testDustCreatedAndPromoted(FeatureBitset features)
    {
        testcase("Credit path: split with remainder, and promotion on accumulation");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "promote");
        if (!fx)
            return;

        Number const dustBefore = readVaultDust(env, fx->broker.vaultKeylet());
        BEAST_EXPECT(dustBefore == beast::kZero);

        payTinyLoanInFull(env, *fx);

        Number const dustAfter = readVaultDust(env, fx->broker.vaultKeylet());
        // The fixture is specifically built so this repayment carries digits
        // finer than the vault's scale (testsuite doc §5) — dust MUST be
        // non-zero, or the whole fixture is vacuous.
        BEAST_EXPECT(dustAfter != beast::kZero);
        BEAST_EXPECT(dustAfter > beast::kZero);
        if (dustAfter > beast::kZero)
            ++dustObservations_;

        auto const vaultSle = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSle))
            return;
        Number const q{1, getVaultScale(vaultSle)};
        BEAST_EXPECT(dustAfter < q);
    }

    //--------------------------------------------------------------------
    // §13.5 Lifecycle guards
    //
    // testNullptrPathUnchanged, testNullptrPathPreservesExistingDust,
    // and testRemoveEmptyHoldingBlockedByDust live in
    // src/test/app/DustSplitCreditPath_test.cpp (trust-line-layer tests
    // for the DustSplit primitive that do not exercise the vault_dust::
    // overlay).
    //--------------------------------------------------------------------

    void
    testAccountHoldsExcludesDust(FeatureBitset features)
    {
        testcase("accountHolds returns sfBalance alone, never sfBalance + sfDust");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "acctholds");
        if (!fx)
            return;

        payTinyLoanInFull(env, *fx);

        Number const dust = readVaultDust(env, fx->broker.vaultKeylet());
        if (dust == beast::kZero)
            return;  // fixture failed to generate dust this run; nothing to check
        ++dustObservations_;

        auto const vaultSle = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSle))
            return;

        Number const holds = accountHolds(
            *env.current(),
            vaultSle->at(sfAccount),
            fx->asset.raw(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            beast::Journal{beast::Journal::getNullSink()});
        Number const assetsAvailable = vaultSle->at(sfAssetsAvailable);

        // If accountHolds leaked sfDust into its result, holds would exceed
        // assetsAvailable by roughly `dust`. It must not.
        BEAST_EXPECT(holds == assetsAvailable);
    }

    void
    testVaultDeleteRequiresZeroDust(FeatureBitset features)
    {
        testcase("VaultDelete is blocked while sfDust is non-zero, and succeeds once it is zero");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};

        Account const issuer{"issuer2"};
        Account const lender{"lender2"};
        env.fund(XRP(1'000'000), issuer, lender);
        env.close();
        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(500'000)));
        env.close();
        env(pay(issuer, lender, asset(400'000)));
        env.close();

        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = asset(1'000)}));
        env.close();

        auto const vaultSleBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        STAmount const allAssets{asset.raw(), vaultSleBefore->at(sfAssetsAvailable)};

        env(vault.withdraw({.depositor = lender, .id = vaultKeylet.key, .amount = allAssets}));
        env.close();

        BEAST_EXPECT(readVaultDust(env, vaultKeylet) == beast::kZero);

        env(vault.del({.owner = lender, .id = vaultKeylet.key}));
        env.close();

        BEAST_EXPECT(!env.le(vaultKeylet));
    }

    //--------------------------------------------------------------------
    // §13.6 Re-normalisation
    //--------------------------------------------------------------------

    // Exercise the interaction between a dust-producing repayment and a
    // subsequent NON-terminal VaultWithdraw. Historically this combination
    // tripped
    //   "withdrawal must change vault and destination balance by equal
    //   amount"
    // in ValidVault (src/libxrpl/tx/invariants/VaultInvariant.cpp) when the
    // withdrawal drove the Vault's posterior scale finer than the custody
    // line's, letting the credit-path re-split inside directSendNoFeeIOU
    // promote whole quanta from sfDust into sfBalance. That promotion is a
    // pure recognition
    // move (no external cash flow), so comparing sfBalance-only deltas
    // between the pseudo (which sees the promotion) and the destination
    // (which does not) mis-attributed a quantum-worth of drift to the
    // wrong side. The fix compares each side's EXTENDED balance
    // (sfBalance + sfDust) — see the "extended balance" branch of the
    // destination check in @c ttVAULT_WITHDRAW.
    //
    // Under the current implementation the non-terminal branch mutates the
    // Vault SLE and the pseudo-account's custody line by the same amount,
    // and any renormalisation adds identical whole-quantum deltas to both
    // T and A. The ValidVault destination check now uses extended balance
    // and therefore holds even when M > 0.
    void
    testNonTerminalWithdrawAfterDust(FeatureBitset features)
    {
        testcase(
            "Non-terminal VaultWithdraw after a dust-producing repayment satisfies ValidVault");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "nontermwd");
        if (!fx)
            return;

        payTinyLoanInFull(env, *fx);

        Number const dustAfterRepay = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustAfterRepay == beast::kZero)
            return;  // fixture did not generate dust this run
        ++dustObservations_;

        auto const vaultSleAfterRepay = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterRepay))
            return;
        Number const availAfterRepay = vaultSleAfterRepay->at(sfAssetsAvailable);
        Number const totalAfterRepay = vaultSleAfterRepay->at(sfAssetsTotal);

        // Partial (non-terminal) withdrawal — take a chunk large enough
        // to refine the Vault's posterior scale (10500 -> ~2), which is
        // what triggers the credit-path re-split to promote whole quanta
        // out of the custody line's sfDust. Values chosen to leave
        // outstanding shares (avoiding the terminal branch).
        Vault const vault{env};
        STAmount const withdrawAmount{fx->asset.raw(), Number{10'498}};
        env(vault.withdraw(
            {.depositor = fx->lender,
             .id = fx->broker.vaultKeylet().key,
             .amount = withdrawAmount}));
        env.close();

        // If ValidVault trips, the transaction is not committed — a
        // successful commit is the primary oracle. But also cross-check
        // the surviving invariant explicitly.
        auto const vaultSleAfterWithdraw = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterWithdraw))
            return;

        Number const availAfterWd = vaultSleAfterWithdraw->at(sfAssetsAvailable);
        Number const totalAfterWd = vaultSleAfterWithdraw->at(sfAssetsTotal);

        // Delta(sfAssetsTotal) must equal Delta(sfAssetsAvailable) —
        // non-terminal removal subtracts `amount` from both, and any
        // renormalisation adds the same movable delta to both.
        BEAST_EXPECT((totalAfterWd - totalAfterRepay) == (availAfterWd - availAfterRepay));

        // Dust must still be strictly less than one quantum at the new
        // scale (O2).
        Number const dustAfterWd = readVaultDust(env, fx->broker.vaultKeylet());
        Number const q{1, getVaultScale(vaultSleAfterWithdraw)};
        BEAST_EXPECT(dustAfterWd >= beast::kZero);
        BEAST_EXPECT(dustAfterWd < q);
    }

    // Companion: another non-terminal step (a second partial withdraw)
    // exercises the case where sfDust is non-zero going *into* the
    // withdraw, potentially getting promoted by renormalisation. Same
    // invariant contract: the ValidVault check must hold.
    void
    testSecondNonTerminalWithdrawAfterDust(FeatureBitset features)
    {
        testcase(
            "Two successive non-terminal VaultWithdraws after dust-producing repayment satisfy "
            "ValidVault");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "twowd");
        if (!fx)
            return;

        payTinyLoanInFull(env, *fx);

        if (readVaultDust(env, fx->broker.vaultKeylet()) == beast::kZero)
            return;

        Vault const vault{env};
        STAmount const firstWd{fx->asset.raw(), Number{5'000}};
        env(vault.withdraw(
            {.depositor = fx->lender, .id = fx->broker.vaultKeylet().key, .amount = firstWd}));
        env.close();

        auto const vaultSleMid = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleMid))
            return;
        Number const availMid = vaultSleMid->at(sfAssetsAvailable);
        Number const totalMid = vaultSleMid->at(sfAssetsTotal);

        STAmount const secondWd{fx->asset.raw(), Number{5'490}};
        env(vault.withdraw(
            {.depositor = fx->lender, .id = fx->broker.vaultKeylet().key, .amount = secondWd}));
        env.close();

        auto const vaultSleEnd = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleEnd))
            return;

        Number const availEnd = vaultSleEnd->at(sfAssetsAvailable);
        Number const totalEnd = vaultSleEnd->at(sfAssetsTotal);

        BEAST_EXPECT((totalEnd - totalMid) == (availEnd - availMid));

        Number const dustEnd = readVaultDust(env, fx->broker.vaultKeylet());
        Number const q{1, getVaultScale(vaultSleEnd)};
        BEAST_EXPECT(dustEnd >= beast::kZero);
        BEAST_EXPECT(dustEnd < q);
    }

    //--------------------------------------------------------------------
    // Two-leg refactor tests (§ Two-leg DustSplit refactor)
    //
    // These exercise the plan's new sender-leg policies (Override on
    // clawback / non-terminal withdrawal / move, Drain on terminal
    // withdrawal) end-to-end via real transactors, so they cover both
    // the trust-line layer's per-leg mechanics and the Vault-side
    // reconciliation through split.sender->dustDelta.
    //--------------------------------------------------------------------

    // Sender-leg Override renormalisation: a scale-refining withdrawal
    // promotes stranded dust from sfDust back into sfBalance, and the
    // Vault's sfAssetsTotal / sfAssetsAvailable both grow by the
    // promoted amount so the receivable invariant is preserved.
    void
    testSenderLegOverrideNonTerminalPromotes(FeatureBitset features)
    {
        testcase("Sender-leg Override renormalises stranded dust on non-terminal withdraw");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "senderoverride");
        if (!fx)
            return;

        // Generate dust via the tiny loan repayment.
        payTinyLoanInFull(env, *fx);

        Number const dustAfterRepay = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustAfterRepay == beast::kZero)
            return;  // fixture didn't generate dust this run
        ++dustObservations_;

        auto const vaultSleAfterRepay = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterRepay))
            return;
        int const scaleBefore = getVaultScale(vaultSleAfterRepay);
        Number const totalBefore = vaultSleAfterRepay->at(sfAssetsTotal);
        Number const availBefore = vaultSleAfterRepay->at(sfAssetsAvailable);

        // Large partial withdrawal — refines the posterior scale so
        // the dust reservoir crosses a decade boundary and gets
        // promoted by the sender-leg Override re-split.
        Vault const vault{env};
        STAmount const withdrawAmount{fx->asset.raw(), Number{10'498}};
        env(vault.withdraw(
            {.depositor = fx->lender,
             .id = fx->broker.vaultKeylet().key,
             .amount = withdrawAmount}));
        env.close();

        auto const vaultSleAfterWd = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterWd))
            return;
        int const scaleAfter = getVaultScale(vaultSleAfterWd);
        Number const dustAfterWd = readVaultDust(env, fx->broker.vaultKeylet());

        // The withdrawal was sized to refine the scale.
        BEAST_EXPECT(scaleAfter < scaleBefore);

        // The Vault's T and A both moved by the SAME extended delta —
        // exactly the invariant the sender-leg Override reconciliation
        // preserves (both fields shift by `-amount` plus the promoted
        // dust). If Override's reconciliation were miscoded, T and A
        // would diverge.
        Number const totalAfter = vaultSleAfterWd->at(sfAssetsTotal);
        Number const availAfter = vaultSleAfterWd->at(sfAssetsAvailable);
        BEAST_EXPECT((totalAfter - totalBefore) == (availAfter - availBefore));

        // The remaining dust on the custody line is bounded by one
        // quantum at the new posterior scale (up to a decade of
        // Override drift, which the O2 relaxation tolerates).
        Number const bound{10, scaleAfter};
        BEAST_EXPECT(dustAfterWd >= beast::kZero);
        BEAST_EXPECT(dustAfterWd < bound);
    }

    // Sender-leg Drain end-to-end: a terminal removal empties the
    // custody line's reservoir into the destination. Vault code does
    // no manual sfDust write; the trust-line layer folds sfDust into
    // sfBalance, adjusts the outgoing amount, and zeroes sfDust. Post-
    // condition: line has sfBalance == 0 and sfDust == 0; the Vault SLE
    // has sfAssetsTotal == 0 and sfAssetsAvailable == 0.
    void
    testSenderLegDrainTerminalRemoval(FeatureBitset features)
    {
        testcase("Sender-leg Drain drains reservoir end-to-end on terminal removal");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeDustFixture(env, "senderdrain");
        if (!fx)
            return;

        // Seed a non-zero reservoir on the custody line by paying the
        // first periodic instalment (returns interest with sub-quantum
        // residual).
        payTinyLoanInFull(env, *fx);

        Number const dustBeforeTerminal = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustBeforeTerminal == beast::kZero)
            return;  // fixture didn't produce dust this run
        BEAST_EXPECT(dustBeforeTerminal > beast::kZero);

        // The remainder of the tiny loan is outstanding, which keeps
        // sfAssetsAvailable < sfAssetsTotal and blocks a terminal
        // withdrawal (the lender's shares still back the outstanding
        // principal). Advance time past the next payment due date +
        // grace window (loan is 2 payments at 1-year intervals; one
        // has been made) and default the loan so no principal is
        // outstanding and the vault SLE's sfAssetsTotal drops to
        // sfAssetsAvailable.
        env.close(std::chrono::seconds(86400 * 800));
        env(jtx::loan::manage(fx->lender, fx->tinyLoanKeylet.key, tfLoanDefault));
        env.close();

        auto const vaultSleBefore = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleBefore))
            return;

        Number const dustAfterDefault = readVaultDust(env, fx->broker.vaultKeylet());
        if (dustAfterDefault == beast::kZero)
            return;  // default consumed the reservoir; nothing left to test

        // Only counts as a genuine Drain-end-to-end observation once we
        // know the reservoir survived long enough to actually reach the
        // withdrawal below — counting it any earlier would let this test
        // satisfy the suite-wide dustObservations_ floor without ever
        // exercising Drain.
        ++dustObservations_;

        Number const lenderBalanceBefore = env.balance(fx->lender, fx->asset).number();
        Number const availBefore = Number{vaultSleBefore->at(sfAssetsAvailable)};

        // Withdraw the full available balance in one shot. With the
        // loan defaulted, sfAssetsAvailable == sfAssetsTotal and the
        // lender holds all outstanding shares, so this burns every
        // share and triggers FinalRemoval::Yes inside VaultWithdraw —
        // the path that installs the Drain policy on the sender leg.
        STAmount const allAssets{fx->asset.raw(), vaultSleBefore->at(sfAssetsAvailable)};
        Vault const vault{env};
        env(vault.withdraw(
            {.depositor = fx->lender, .id = fx->broker.vaultKeylet().key, .amount = allAssets}));
        env.close();

        auto const vaultSleAfter = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfter))
            return;

        // Vault SLE ends terminal: both totals reset to zero.
        BEAST_EXPECT(Number{vaultSleAfter->at(sfAssetsAvailable)} == beast::kZero);
        BEAST_EXPECT(Number{vaultSleAfter->at(sfAssetsTotal)} == beast::kZero);

        // Custody line's sfDust must be zero — Drain zeroed it inside
        // the trust-line layer, not via a manual fold in Vault code.
        Number const dustAfter = readVaultDust(env, fx->broker.vaultKeylet());
        BEAST_EXPECT(dustAfter == beast::kZero);

        // The destination (lender) received at least the vault's
        // pre-terminal sfAssetsAvailable plus the reservoir that was
        // drained — the observable end-to-end effect of Drain mode:
        // value that used to be stranded in sfDust reaches the
        // destination in the same transaction.
        Number const lenderBalanceAfter = env.balance(fx->lender, fx->asset).number();
        Number const received = lenderBalanceAfter - lenderBalanceBefore;
        BEAST_EXPECT(received >= availBefore);
        BEAST_EXPECT(received >= availBefore + dustAfterDefault);
    }

    //--------------------------------------------------------------------
    // §13.7 Multi-loan (O8) tests
    //
    // A single Vault backing multiple concurrently-active Loans at
    // *different* sfLoanScale values is the most stressful narrative
    // for the two-field (sfBalance / sfDust) accounting: every
    // repayment credits the vault at its own loan's scale, but the
    // vault's posterior scale (getVaultScale, derived from
    // sfAssetsTotal via STAmount's canonical exponent) can be either
    // finer or coarser than the paying loan's scale. Any silent
    // truncation in the credit path — anything that quietly invokes
    // associateAsset on a Vault Number, for instance — would shred the
    // fine digits accumulated on sfAssetsTotal from a prior finer-scale
    // repayment. The base O8 identity
    //     sfAssetsTotal - sfAssetsAvailable == Σ sfPrincipalOutstanding
    // is therefore also a *digit-preservation* oracle: if any
    // truncation happens on one side and not the other, this equation
    // breaks at Number precision.
    //--------------------------------------------------------------------

    void
    testMultiLoanScaleDriftPreservesO8(FeatureBitset features)
    {
        testcase(
            "Multi-loan interleaved repayments preserve T == A + Σ PO across "
            "scale drift");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeMultiLoanDustFixture(env, "drift");
        if (!fx)
            return;

        log << "\n=== testMultiLoanScaleDriftPreservesO8: fixture summary ===\n"
            << "  loanA sfLoanScale = " << fx->loanAScale << " (fine, created at T=1'000)\n"
            << "  loanB sfLoanScale = " << fx->loanBScale << " (mid,  created at T~10'000)\n"
            << "  loanC sfLoanScale = " << fx->loanCScale << " (coarse, created at T~100'000)"
            << std::endl;

        // Sanity: baseline invariants hold immediately after fixture
        // setup, before any repayment.
        MultiLoanSnap snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("post-setup (before any repayment)", snap);
        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "post-setup")))
            return;

        // Repay each loan once, in a deliberately scale-non-monotonic
        // order (fine, coarse, mid) — the order that historically was
        // most likely to expose truncation, since it mixes a fine-digit
        // contribution with a coarse-scale one before the next
        // fine-digit contribution lands.
        MultiLoanSnap prev = snap;
        payOneInstallment(env, *fx, fx->loanA);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState(
            "after pay loanA (fine-scale, sfLoanScale=" + std::to_string(fx->loanAScale) + ")",
            snap,
            &prev);
        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "after payA1")))
            return;

        prev = snap;
        payOneInstallment(env, *fx, fx->loanC);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState(
            "after pay loanC (coarse-scale, sfLoanScale=" + std::to_string(fx->loanCScale) + ")",
            snap,
            &prev);
        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "after payC1")))
            return;

        prev = snap;
        payOneInstallment(env, *fx, fx->loanB);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState(
            "after pay loanB (mid-scale, sfLoanScale=" + std::to_string(fx->loanBScale) + ")",
            snap,
            &prev);
        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "after payB1")))
            return;

        // Verify at least one repayment actually produced dust — else
        // the test degenerates to a plain-payment regression, and the
        // per-scale contract is not being exercised.
        Number const dust = readVaultDust(env, fx->broker.vaultKeylet());
        if (dust != beast::kZero)
            ++dustObservations_;
        BEAST_EXPECTS(
            dust != beast::kZero,
            "Multi-loan fixture did not produce sfDust on any repayment; "
            "loan scales " +
                std::to_string(fx->loanAScale) + "/" + std::to_string(fx->loanBScale) + "/" +
                std::to_string(fx->loanCScale) +
                " may all be aligned with the vault posterior scale.");

        // Force a scale refinement mid-sequence via a partial
        // withdrawal, then repay again. This is the code path where a
        // hidden associateAsset call on sfAssetsTotal would surface:
        // the withdraw promotes stranded dust via sender-leg Override,
        // and if the subsequent repayment's Number arithmetic lost
        // digits, the receivable identity would break.
        Vault const vault{env};
        auto const vaultSleBeforeWd = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleBeforeWd))
            return;
        int const scaleBeforeWd = getVaultScale(vaultSleBeforeWd);

        // Withdraw ~99% to drive T back into the -12 / -11 band. The
        // exact amount is chosen large enough to shift the STAmount
        // canonical exponent (so scale actually drifts), but small
        // enough to leave enough available for the final repayment
        // installments and outstanding principal cover.
        STAmount const withdrawAmount{fx->asset.raw(), Number{99'000}};
        prev = snap;
        env(vault.withdraw(
            {.depositor = fx->lender,
             .id = fx->broker.vaultKeylet().key,
             .amount = withdrawAmount}));
        env.close();

        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState(
            "after partial withdraw of 99'000 (forces scale refinement)", snap, &prev);

        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "after withdraw")))
            return;

        auto const vaultSleAfterWd = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfterWd))
            return;
        int const scaleAfterWd = getVaultScale(vaultSleAfterWd);
        BEAST_EXPECTS(
            scaleAfterWd < scaleBeforeWd,
            "withdraw did not refine vault scale: before=" + std::to_string(scaleBeforeWd) +
                " after=" + std::to_string(scaleAfterWd));
    }

    // Companion test that pins the *digit-preservation* claim
    // explicitly. Steps:
    //   1. Snapshot sfAssetsTotal S0 (has 3-4 fine sub-quantum digits
    //      after loanA is paid, at scale -12).
    //   2. Pay loanC (scale = -10, coarser).
    //   3. Verify sfAssetsTotal changed by *exactly* what LoanPay
    //      credited on the custody line (fine digits from S0 must
    //      survive the coarser-scale write, because sfAssetsTotal is
    //      Number, not STAmount, and no associateAsset is called on it).
    //
    // The failure mode this catches: if a future refactor sneaks
    // associateAsset onto the sfAssetsTotal update path, the S0-fine
    // digits get truncated to the coarser scale and step (3)'s equation
    // breaks by exactly the amount of dust that was silently thrown
    // away.
    // Fully repay every fixture Loan (both installments per Loan) so
    // ΣsfPrincipalOutstanding drops to exactly zero, then pin the
    // *terminal* accounting identity in extended form:
    //
    //     (sfAssetsTotal + dust) == (sfAssetsAvailable + dust)
    //
    // dust appears on both sides and cancels, so this reduces to
    // T == A when Σ PO == 0 — but the symmetric framing is deliberate:
    // if any future change starts folding dust into T or A on only
    // one side, the extended-form check will still hold on the field
    // that already includes it and fail on the one that stopped, making
    // the drift immediately localisable to a single field.
    //
    // How to read this against O8.1 (extended (T+d) - (A+d) == Σ PO):
    //   * O8.1 relates the extended totals through the OUTSTANDING
    //     loan book.
    //   * When Σ PO reaches 0, O8.1 collapses to (T+d) == (A+d), i.e.
    //     T == A (dust cancels — it's the same reservoir on both
    //     sides).
    //   * So this test is simultaneously an extended-O8.1 check AND a
    //     cash-out reconciliation check.
    //
    // If the terminal identity fails, the residual is a signed
    // quantity of stranded value that cannot be explained by either
    // A or dust. The dump prints it in vault terms so a regression is
    // immediately interpretable at a glance.
    void
    testMultiLoanFullRepaymentReconciles(FeatureBitset features)
    {
        testcase(
            "Full repayment of all fixture loans reconciles T = A + dust "
            "(no stranded sub-quantum reservoir)");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeMultiLoanDustFixture(env, "fullrepay");
        if (!fx)
            return;

        log << "\n=== testMultiLoanFullRepaymentReconciles: fixture "
               "summary ===\n"
            << "  loanA sfLoanScale = " << fx->loanAScale << " (fine)\n"
            << "  loanB sfLoanScale = " << fx->loanBScale << " (mid)\n"
            << "  loanC sfLoanScale = " << fx->loanCScale << " (coarse)" << std::endl;

        MultiLoanSnap snapInit = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("initial (before any repayment)", snapInit);

        // -- First installment on each loan.
        MultiLoanSnap prev = snapInit;
        payOneInstallment(env, *fx, fx->loanA);
        MultiLoanSnap snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("after pay loanA installment 1", snap, &prev);

        prev = snap;
        payOneInstallment(env, *fx, fx->loanB);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("after pay loanB installment 1", snap, &prev);

        prev = snap;
        payOneInstallment(env, *fx, fx->loanC);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("after pay loanC installment 1", snap, &prev);

        // Advance ~1 year so each loan's second (and final) payment
        // is due at or past its NextPaymentDueDate. Regular payments
        // do not strictly require this (there is no tecTOO_SOON guard
        // on the normal path), but making the ledger time reflect the
        // schedule keeps the fixture faithful to a real-world
        // full-repayment scenario.
        env.close(std::chrono::seconds(86400 * 365 + 86400));

        // -- Second (final) installment on each loan. paymentsRemaining
        // must be 0 and PO must be 0 for each after this round.
        prev = snap;
        payOneInstallment(env, *fx, fx->loanA);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("after pay loanA installment 2 (final)", snap, &prev);

        prev = snap;
        payOneInstallment(env, *fx, fx->loanB);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("after pay loanB installment 2 (final)", snap, &prev);

        prev = snap;
        payOneInstallment(env, *fx, fx->loanC);
        snap = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("after pay loanC installment 2 (final)", snap, &prev);

        // Post-condition 1: every loan is fully paid off (ΣPO == 0).
        // This is what makes the T vs A+dust check meaningful — with
        // any residual PO, O8.1 pins T - A to that PO and the
        // dust-inclusive identity below is not the applicable claim.
        Number const poSum = sumPrincipalOutstanding(env, *fx);
        BEAST_EXPECTS(
            poSum == beast::kZero,
            "Full-repayment prerequisite failed: ΣPO=" + to_string(poSum) +
                " — at least one loan still has outstanding principal.");
        if (poSum != beast::kZero)
        {
            // If we did not actually reach the fully-paid state (e.g.
            // the payment amount was off by a rounding quantum), the
            // terminal identity is not the applicable claim.
            log << "Cannot check terminal T = A + dust identity: ΣPO !=0." << std::endl;
            return;
        }

        auto const vaultSle = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSle))
            return;
        Number const T = vaultSle->at(sfAssetsTotal);
        Number const A = vaultSle->at(sfAssetsAvailable);
        Number const dust = readVaultDust(env, fx->broker.vaultKeylet());
        Number const extT = T + dust;
        Number const extA = A + dust;
        Number const residual = extT - extA;

        log << "\n--- terminal identity check ((T+d) == (A+d)) ---\n"
            << "  sfAssetsTotal          T = " << to_string(T) << "\n"
            << "  sfAssetsAvailable      A = " << to_string(A) << "\n"
            << "  dust (vault terms)     d = " << to_string(dust) << "\n"
            << "  Extended T+d             = " << to_string(extT) << "\n"
            << "  Extended A+d             = " << to_string(extA) << "\n"
            << "  (T+d) - (A+d) (residual) = " << to_string(residual) << " (must be 0)"
            << std::endl;

        // Terminal identity in extended form: with no outstanding
        // principal, the vault's extended total must equal its
        // extended available. Since the same dust reservoir is added
        // to both sides, this reduces mathematically to T == A — the
        // extended form just ensures the symmetric framing is
        // preserved even if a future change starts folding dust into
        // one field but not the other.
        BEAST_EXPECTS(
            residual == beast::kZero,
            "(T+d) != (A+d) after full repayment; residual=" + to_string(residual) +
                " — extended total and extended available diverged with "
                "no outstanding principal left to explain the gap.");

        if (dust != beast::kZero)
            ++dustObservations_;
    }

    void
    testMultiLoanAssetsTotalDigitPreservation(FeatureBitset features)
    {
        testcase(
            "Coarse-scale repayment preserves fine digits already on "
            "sfAssetsTotal");

        using namespace jtx;
        Env env{*this, features | featureLendingProtocolV1_1};
        auto const fx = makeMultiLoanDustFixture(env, "digits");
        if (!fx)
            return;

        log << "\n=== testMultiLoanAssetsTotalDigitPreservation: fixture "
               "summary ===\n"
            << "  loanA sfLoanScale = " << fx->loanAScale << " (fine)\n"
            << "  loanB sfLoanScale = " << fx->loanBScale << " (mid)\n"
            << "  loanC sfLoanScale = " << fx->loanCScale << " (coarse)" << std::endl;

        MultiLoanSnap snapInit = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState("initial (before any repayment)", snapInit);

        // Step 1: pay loanA to seed fine digits into sfAssetsTotal.
        payOneInstallment(env, *fx, fx->loanA);
        MultiLoanSnap snapA = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState(
            "after pay loanA (seeds fine digits at sfLoanScale=" + std::to_string(fx->loanAScale) +
                ")",
            snapA,
            &snapInit);
        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "seed fine digits")))
            return;

        auto const vaultSleBefore = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleBefore))
            return;
        Number const T0 = vaultSleBefore->at(sfAssetsTotal);
        Number const A0 = vaultSleBefore->at(sfAssetsAvailable);
        Number const poSumBefore = sumPrincipalOutstanding(env, *fx);
        Number const dustBefore = readVaultDust(env, fx->broker.vaultKeylet());

        // If loanA didn't create dust (fixture flake), we can't
        // meaningfully assert digit preservation — there are no fine
        // digits to preserve. Bail out.
        if (dustBefore == beast::kZero)
        {
            log << "digit-preservation test: loanA did not create dust; "
                   "skipping the cross-scale assertion."
                << std::endl;
            return;
        }
        ++dustObservations_;

        // Step 2: pay loanC (coarser scale).
        payOneInstallment(env, *fx, fx->loanC);
        MultiLoanSnap snapC = snapshotMultiLoan(env, *fx);
        dumpMultiLoanState(
            "after pay loanC (coarser-scale write at sfLoanScale=" +
                std::to_string(fx->loanCScale) + ") — fine digits from loanA must survive",
            snapC,
            &snapA);

        auto const vaultSleAfter = env.le(fx->broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultSleAfter))
            return;
        Number const T1 = vaultSleAfter->at(sfAssetsTotal);
        Number const A1 = vaultSleAfter->at(sfAssetsAvailable);
        Number const poSumAfter = sumPrincipalOutstanding(env, *fx);

        // Step 3: verify the O8 receivable identity is exact at Number
        // precision after the cross-scale repayment. This is the digit-
        // preservation claim: if any fine sub-quantum digit from T0 got
        // truncated by the LoanC path, T1 would not equal A1 + Σ PO.
        BEAST_EXPECTS(
            (T1 - A1) == poSumAfter,
            "O8.1 broken after cross-scale repayment: T1=" + to_string(T1) +
                " A1=" + to_string(A1) + " Σ PO=" + to_string(poSumAfter) +
                " residual=" + to_string(T1 - A1 - poSumAfter));

        // Total value delta and available delta must match to Number
        // precision as well: the LoanC repayment credits the vault
        // fully (no fee split in this fixture — managementFeeRate=0
        // and serviceFee=0 by default), so both fields advance by the
        // same amount and their delta cancels in the receivable.
        Number const deltaT = T1 - T0;
        Number const deltaA = A1 - A0;
        Number const deltaPO = poSumAfter - poSumBefore;
        BEAST_EXPECTS(
            (deltaT - deltaA) == deltaPO,
            "cross-scale delta mismatch: ΔT-ΔA=" + to_string(deltaT - deltaA) +
                " ΔPO=" + to_string(deltaPO));

        if (!BEAST_EXPECT(assertO8Exact(env, *fx, "post cross-scale")))
            return;
    }

public:
    void
    run() override
    {
        testNoDustForLegacyOrIntegralVaults(all_);
        testDustBothSignOrientations(all_);
        testDustCreatedAndPromoted(all_);
        testAccountHoldsExcludesDust(all_);
        testVaultDeleteRequiresZeroDust(all_);
        testNonTerminalWithdrawAfterDust(all_);
        testSecondNonTerminalWithdrawAfterDust(all_);
        testSenderLegOverrideNonTerminalPromotes(all_);
        testSenderLegDrainTerminalRemoval(all_);
        testMultiLoanScaleDriftPreservesO8(all_);
        testMultiLoanAssetsTotalDigitPreservation(all_);
        testMultiLoanFullRepaymentReconciles(all_);

        // Guard: silent-pass fixture failure would leave every dust
        // test above trivially green. If the loan/vault scales stop
        // producing dust, this failure surfaces immediately at the
        // suite level with a clear reason, instead of leaking through
        // as false-positive green CI. Threshold is intentionally low
        // (>= 4) so a single fixture regression fails; if the fixture
        // is redesigned, adjust with intent.
        BEAST_EXPECTS(
            dustObservations_ >= 4,
            "VaultRoundingTrustlineDust fixture stopped producing dust — "
            "observed " +
                std::to_string(dustObservations_) +
                " dust-carrying runs "
                "(expected >= 4). Suite is likely running vacuously.");
    }
};

BEAST_DEFINE_TESTSUITE(VaultRoundingTrustlineDust, tx, xrpl);

}  // namespace xrpl::test
