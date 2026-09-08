#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>

namespace xrpl::test {

class LoanSecurity_test : public LoanTestBase
{
private:
    // Env::close() cannot land the ledger's parentCloseTime on an arbitrary
    // instant: it always rounds the requested time forward to the next
    // close-time-resolution boundary (see Env::close() and
    // roundCloseTime()/effCloseTime() in LedgerTiming.h), so it can only be
    // used to reach times strictly *after* a given due date, never exactly
    // on it. To pin the exact-boundary behavior of isPaymentLate(), directly
    // overwrite the loan's NextPaymentDueDate instead, without closing the
    // ledger again.
    void
    setLoanNextPaymentDueDate(jtx::Env& env, Keylet const& loanKeylet, std::uint32_t dueDate)
    {
        using namespace jtx;
        bool const ok = env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            auto const sle = view.read(loanKeylet);
            if (!sle)
                return false;
            auto replacement = std::make_shared<SLE>(*sle);
            (*replacement)[sfNextPaymentDueDate] = dueDate;
            view.rawReplace(replacement);
            return true;
        });
        BEAST_EXPECT(ok);
    }

    void
    testPoCUnsignedUnderflowOnFullPayAfterEarlyPeriodic(FeatureBitset features)
    {
        // --- PoC Summary ----------------------------------------------------
        // Scenario: Borrower makes one periodic payment early (before next due)
        // so doPayment sets sfPreviousPaymentDueDate to the (future)
        // sfNextPaymentDueDate and advances sfNextPaymentDueDate by one
        // interval. Borrower then immediately performs a full-payment
        // (tfLoanFullPayment). Why it matters: Full-payment interest accrual
        // uses
        //   delta = now - max(prevPaymentDate, startDate)
        // with an unsigned clock representation (uint32). If prevPaymentDate is
        // in the future, the subtraction underflows to a very large positive
        // number. This inflates roundedFullInterest and total full-close due,
        // and LoanPay applies the inflated valueChange to the vault
        // (sfAssetsTotal), increasing NAV.
        // --------------------------------------------------------------------
        testcase("PoC: Unsigned-underflow full-pay accrual after early periodic");

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const lender{"poc_lender4"};
        Account const borrower{"poc_borrower4"};
        env.fund(XRP(3'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1'000'000};
        BrokerParameters const brokerParams{};
        auto const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        // Create a 3-payment loan so full-payment path is enabled after 1
        // periodic payment.
        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest = asset(1000).value();
        auto const originationFee = asset(0).value();
        auto const serviceFee = asset(1).value();
        auto const serviceFeePA = asset(1);
        auto const lateFee = asset(0).value();
        auto const closeFee = asset(0).value();
        auto const interest = percentageToTenthBips(12);
        auto const lateInterest = percentageToTenthBips(12) / 10;
        auto const closeInterest = percentageToTenthBips(12) / 10;
        auto const overpaymentInterest = percentageToTenthBips(12) / 10;
        auto const total = 3u;
        auto const interval = 600u;
        auto const grace = 60u;

        auto createJtx = env.jt(
            set(borrower, broker.brokerID, principalRequest, 0),
            Sig(sfCounterpartySignature, lender),
            kLoanOriginationFee(originationFee),
            kLoanServiceFee(serviceFee),
            kLatePaymentFee(lateFee),
            kClosePaymentFee(closeFee),
            kOverpaymentFee(percentageToTenthBips(5) / 10),
            kInterestRate(interest),
            kLateInterestRate(lateInterest),
            kCloseInterestRate(closeInterest),
            kOverpaymentInterestRate(overpaymentInterest),
            kPaymentTotal(total),
            kPaymentInterval(interval),
            kGracePeriod(grace),
            Fee(loanSetFee));

        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        BEAST_EXPECT(brokerSle);
        auto const loanSequence = brokerSle ? brokerSle->at(sfLoanSequence) : 0;
        auto const loanKeylet = keylet::loan(broker.brokerID, SeqProxy::rawSequence(loanSequence));

        env(createJtx);
        env.close();

        // Compute a regular periodic due and pay it early (before next due).
        auto state = getCurrentState(env, broker, loanKeylet);
        Number const periodicRate = loanPeriodicRate(state.interestRate, state.paymentInterval);
        auto const components = xrpl::detail::computePaymentComponents(
            env.current()->rules(),
            asset.raw(),
            state.loanScale,
            state.totalValue,
            state.principalOutstanding,
            state.managementFeeOutstanding,
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            brokerParams.managementFeeRate);
        STAmount const regularDue{asset, components.trackedValueDelta + serviceFeePA.number()};
        // now < nextDue immediately after creation, so this is an early pay.
        env(pay(borrower, loanKeylet.key, regularDue));
        env.close();

        // Immediately attempt a full payoff. Compute the exact full-payment
        // due to ensure the tx applies.
        auto after = getCurrentState(env, broker, loanKeylet);
        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        auto const brokerSle2 = env.le(keylet::loanBroker(broker.brokerID));
        BEAST_EXPECT(brokerSle2);

        auto const closePaymentFee = loanSle ? loanSle->at(sfClosePaymentFee) : Number{};
        auto const closeInterestRate =
            loanSle ? TenthBips32{loanSle->at(sfCloseInterestRate)} : TenthBips32{};
        auto const managementFeeRate =
            brokerSle2 ? TenthBips16{brokerSle2->at(sfManagementFeeRate)} : TenthBips16{};

        Number const periodicRate2 = loanPeriodicRate(after.interestRate, after.paymentInterval);
        // Accrued + prepayment-penalty interest based on current periodic
        // schedule
        auto const fullPaymentInterest = computeFullPaymentInterest(
            xrpl::detail::loanPrincipalFromPeriodicPayment(
                env.current()->rules(),
                after.periodicPayment,
                periodicRate2,
                after.paymentRemaining),
            periodicRate2,
            env.current()->parentCloseTime(),
            after.paymentInterval,
            after.previousPaymentDate,
            static_cast<std::uint32_t>(after.startDate.time_since_epoch().count()),
            closeInterestRate);

        // Round to asset scale and split interest/fee parts
        auto const roundedInterest =
            roundToAsset(asset.raw(), fullPaymentInterest, after.loanScale);
        Number const roundedFullMgmtFee =
            computeManagementFee(asset.raw(), roundedInterest, managementFeeRate, after.loanScale);
        Number const roundedFullInterest = roundedInterest - roundedFullMgmtFee;

        // Show both signed and unsigned deltas to highlight the underflow.
        auto const nowSecs =
            static_cast<std::uint32_t>(env.current()->parentCloseTime().time_since_epoch().count());
        auto const startSecs =
            static_cast<std::uint32_t>(after.startDate.time_since_epoch().count());
        auto const lastPaymentDate = std::max(after.previousPaymentDate, startSecs);
        auto const signedDelta =
            static_cast<std::int64_t>(nowSecs) - static_cast<std::int64_t>(lastPaymentDate);
        auto const unsignedDelta = static_cast<std::uint32_t>(nowSecs - lastPaymentDate);
        log << "PoC window: prev=" << after.previousPaymentDate << " start=" << startSecs
            << " now=" << nowSecs << " signedDelta=" << signedDelta
            << " unsignedDelta=" << unsignedDelta << std::endl;

        // Reference (clamped) computation: emulate a non-negative accrual
        // window by clamping prevPaymentDate to 'now' for the full-pay path.
        auto const prevClamped = std::min(after.previousPaymentDate, nowSecs);
        auto const fullPaymentInterestClamped = computeFullPaymentInterest(
            xrpl::detail::loanPrincipalFromPeriodicPayment(
                env.current()->rules(),
                after.periodicPayment,
                periodicRate2,
                after.paymentRemaining),
            periodicRate2,
            env.current()->parentCloseTime(),
            after.paymentInterval,
            prevClamped,
            startSecs,
            closeInterestRate);
        auto const roundedInterestClamped =
            roundToAsset(asset.raw(), fullPaymentInterestClamped, after.loanScale);
        Number const roundedFullMgmtFeeClamped = computeManagementFee(
            asset.raw(), roundedInterestClamped, managementFeeRate, after.loanScale);
        Number const roundedFullInterestClamped =
            roundedInterestClamped - roundedFullMgmtFeeClamped;
        STAmount const fullDueClamped{
            asset,
            after.principalOutstanding + roundedFullInterestClamped + roundedFullMgmtFeeClamped +
                closePaymentFee};

        // Collect vault NAV before closing payment
        auto const vaultId2 = brokerSle2 ? brokerSle2->at(sfVaultID) : uint256{};
        auto const vaultKey2 = keylet::vault(vaultId2);
        auto const vaultBefore = env.le(vaultKey2);
        BEAST_EXPECT(vaultBefore);
        Number const assetsTotalBefore = vaultBefore ? vaultBefore->at(sfAssetsTotal) : Number{};

        STAmount const fullDue{
            asset,
            after.principalOutstanding + roundedFullInterest + roundedFullMgmtFee +
                closePaymentFee};

        log << "PoC payoff: principalOutstanding=" << after.principalOutstanding
            << " roundedFullInterest=" << roundedFullInterest
            << " roundedFullMgmtFee=" << roundedFullMgmtFee << " closeFee=" << closePaymentFee
            << " fullDue=" << to_string(fullDue.getJson()) << std::endl;
        log << "PoC reference (clamped): roundedFullInterestClamped=" << roundedFullInterestClamped
            << " roundedFullMgmtFeeClamped=" << roundedFullMgmtFeeClamped
            << " fullDueClamped=" << to_string(fullDueClamped.getJson()) << std::endl;

        env(pay(borrower, loanKeylet.key, fullDue), Txflags(tfLoanFullPayment));
        env.close();

        // Sanity: underflow present (unsigned delta very large relative to
        // interval)
        BEAST_EXPECT(unsignedDelta > after.paymentInterval);

        // Compare vault NAV before/after the full close
        auto const vaultAfter = env.le(vaultKey2);
        BEAST_EXPECT(vaultAfter);
        if (vaultAfter)
        {
            auto const assetsTotalAfter = vaultAfter->at(sfAssetsTotal);
            log << "PoC NAV: assetsTotalBefore=" << assetsTotalBefore
                << " assetsTotalAfter=" << assetsTotalAfter
                << " delta=" << (assetsTotalAfter - assetsTotalBefore) << std::endl;

            // Regression check: the underflowed window must be clamped so the
            // payoff matches the non-underflow reference, i.e. no overcharge.
            BEAST_EXPECT(fullDue == fullDueClamped);
            if (fullDue != fullDueClamped)
                log << "PoC delta: overcharge (fullDue > clamped)" << std::endl;
        }

        // Loan should be paid off
        auto const finalLoan = env.le(loanKeylet);
        BEAST_EXPECT(finalLoan);
        if (finalLoan)
        {
            BEAST_EXPECT(finalLoan->at(sfPaymentRemaining) == 0);
            BEAST_EXPECT(finalLoan->at(sfPrincipalOutstanding) == 0);
        }
    }

    void
    testRIPD3831(FeatureBitset features)
    {
        using namespace jtx;

        testcase("RIPD-3831");

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            // .managementFeeRate = TenthBips16{5919},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{200'000, -6},
            .lateFee = Number{200, -6},
            .interest = TenthBips32{50'000},
            .payTotal = 10,
            .payInterval = 150};

        auto const assetType = AssetType::XRP;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);

        using tp = NetClock::time_point;
        using d = NetClock::duration;

        auto state = getCurrentState(env, broker, loanKeylet);
        if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
        {
            env.close(tp{d{loan->at(sfNextPaymentDueDate) + loan->at(sfGracePeriod) + 1}});
        }

        topUpBorrower(env, broker, issuer, borrower, state, loanParams.serviceFee);

        using namespace jtx::loan;

        auto jv = pay(borrower, loanKeylet.key, drops(XRPAmount(state.totalValue)));

        {
            auto const submitParam = to_string(jv);
            auto const jr = env.rpc("submit", borrower.name(), submitParam);

            BEAST_EXPECT(jr.isMember(jss::result));
        }

        env.close();

        // Make sure the system keeps responding
        env(noop(borrower));
        env.close();
        env(noop(issuer));
        env.close();
        env(noop(lender));
        env.close();
    }

    void
    testRIPD3459(FeatureBitset features)
    {
        testcase("RIPD-3459 - LoanBroker incorrect debt total");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 200'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{500},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{100'000, -4},
            .interest = TenthBips32{100'000},
            .payTotal = 10};

        auto const assetType = AssetType::MPT;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == loanSle->at(sfTotalValueOutstanding));
            }
        }

        makeLoanPayments(
            env,
            broker,
            loanParams,
            loanKeylet,
            verifyLoanStatus,
            issuer,
            lender,
            borrower,
            PaymentParameters{.showStepBalances = true});

        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == loanSle->at(sfTotalValueOutstanding));
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == beast::kZero);
            }
        }
    }

    void
    testRIPD3901()
    {
        testcase("Crash with tfLoanOverpayment");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Account const depositor{"depositor"};
        auto const txFee = Fee(XRP(100));

        // Under featureLendingProtocolV1_1 LoanBrokerSet::preclaim only
        // accepts closed-ended vaults, so build one and advance past
        // SubscriptionDate before creating the broker and the loan.
        Env env(*this);
        Vault const vault(env);

        env.fund(XRP(10'000), lender, issuer, borrower, depositor);
        env.close();

        auto [tx, vaultKeyLet, subscriptionDate] =
            vault.createClosedEnded({.owner = lender, .asset = xrpIssue()});
        env(tx, txFee);
        env.close();

        env(vault.deposit({.depositor = depositor, .id = vaultKeyLet.key, .amount = XRP(1'000)}),
            txFee);
        env.close();

        // Move into the Investment phase before creating the broker and
        // the loan.
        vault.closePastSubscription(subscriptionDate);

        auto const brokerKeyLet =
            keylet::loanBroker(lender.id(), SeqProxy::rawSequence(env.seq(lender)));

        env(loan_broker::set(lender, vaultKeyLet.key), txFee);
        env.close();

        STAmount const debtMaximumRequest = XRPAmount(200'000);

        env(set(borrower, brokerKeyLet.key, debtMaximumRequest),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32(50'000)),
            kPaymentTotal(2),
            kPaymentInterval(150),
            Txflags(tfLoanOverpayment),
            txFee);
        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerKeyLet.key, SeqProxy::rawSequence(loanSequence));

        if (auto loan = env.le(loanKeylet); env.test.BEAST_EXPECT(loan))
        {
            env(loan::pay(borrower, loanKeylet.key, XRPAmount(150'001)),
                Txflags(tfLoanOverpayment),
                txFee);
            env.close();
        }
    }

    void
    testRIPD3902(FeatureBitset features)
    {
        testcase("RIPD-3902 - 1 IOU loan payments");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 10,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{1, 0},
            .interest = TenthBips32{100'000},
            .payTotal = 5,
            .payInterval = 150,
            .gracePd = 60};

        auto const assetType = AssetType::IOU;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        makeLoanPayments(
            env,
            broker,
            loanParams,
            loanKeylet,
            verifyLoanStatus,
            issuer,
            lender,
            borrower,
            PaymentParameters{.showStepBalances = true});
    }

    // Verify that with fixCleanup3_4_0:
    // 1. A loan cannot be impaired before its payment is late.
    // 2. Impairing a late loan does not change sfNextPaymentDueDate.
    // 3. The unimpair operation does not change sfNextPaymentDueDate.
    void
    testImpairmentPaymentDateUnchanged()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        testcase("Impairment does not change payment due date");

        Env env(*this, all_ | fixCleanup3_4_0);
        BEAST_EXPECT(env.enabled(fixCleanup3_4_0));

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        Number const principalRequest{1, 3};
        env(set(borrower, broker.brokerID, broker.asset(principalRequest).value()),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(12),
            kPaymentInterval(600),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const originalNextDueDate = loanSle->at(sfNextPaymentDueDate);
        BEAST_EXPECT(originalNextDueDate > 0);

        // 1. Impairment must fail when payment is not yet late
        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecTOO_SOON));

        {
            auto const loan = env.le(loanKeylet);
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
        }

        // 1b. Impairment must still fail at the exact due date instant: a
        // payment due "now" is not yet late (strict/Exclusive comparison).
        // Temporarily set NextPaymentDueDate to exactly the current
        // parentCloseTime (without closing the ledger again), exercise the
        // check, then restore the original due date.
        {
            std::uint32_t const exactNow =
                env.current()->parentCloseTime().time_since_epoch().count();
            setLoanNextPaymentDueDate(env, loanKeylet, exactNow);

            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecTOO_SOON));

            setLoanNextPaymentDueDate(env, loanKeylet, originalNextDueDate);
        }

        {
            auto const loan = env.le(loanKeylet);
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
        }

        env.close(NetClock::time_point{NetClock::duration{originalNextDueDate}} + 1s);

        // 2. Impairment succeeds when payment is late
        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));

        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
        }

        // 3. Unimpair also does not change sfNextPaymentDueDate
        env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tesSUCCESS));

        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
        }
    }

    // Verify that without fixCleanup3_4_0, the pre-amendment
    // impair/unimpair behaviour is preserved:
    // 1. Impairing a loan before its payment is late moves
    //    sfNextPaymentDueDate to "now".
    // 2a. Unimpair within the original payment interval restores
    //     sfNextPaymentDueDate to StartDate + PaymentInterval.
    // 2b. Unimpair after the original due date sets
    //     sfNextPaymentDueDate to now + PaymentInterval.
    void
    testImpairmentPaymentDatePreAmendment()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        testcase("Pre-amendment impair/unimpair date restoration");

        Env env(*this, all_ - fixCleanup3_4_0);
        BEAST_EXPECT(!env.enabled(fixCleanup3_4_0));

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        Number const principalRequest{1, 3};
        auto createNewLoan = [&]() {
            auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(sleBroker))
                return keylet::loan(uint256{});
            auto const lk =
                keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));
            env(set(borrower, broker.brokerID, broker.asset(principalRequest).value()),
                Sig(sfCounterpartySignature, lender),
                kPaymentTotal(12),
                kPaymentInterval(600),
                Fee(env.current()->fees().base * 2));
            env.close();
            return lk;
        };

        // Default + delete a loan and replenish first-loss capital so the
        // broker is ready for the next loan.
        auto cleanupLoan = [&](Keylet const& loanKeylet, std::uint32_t dueDate) {
            env.close(NetClock::time_point{NetClock::duration{dueDate + 60}} + 1s);
            env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
            env.close();

            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return;
            auto const coverNeeded =
                broker.asset(broker.params.coverDeposit).value() - brokerSle->at(sfCoverAvailable);
            if (coverNeeded > 0)
            {
                env(loan_broker::coverDeposit(
                    lender, broker.brokerID, STAmount{broker.asset, coverNeeded}));
                env.close();
            }
            env(del(lender, loanKeylet.key));
            env.close();
        };

        // ---- Case A: impair before late, unimpair within original interval ----
        {
            auto const loanKeylet = createNewLoan();
            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            std::uint32_t const startDate = loanSle->at(sfStartDate);
            std::uint32_t const originalNextDueDate = loanSle->at(sfNextPaymentDueDate);
            BEAST_EXPECT(originalNextDueDate == startDate + 600);

            // Payment is not late yet - impair succeeds and moves due date
            // to now (pre-amendment allows immediate impairment)
            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));

            {
                auto const loan = env.le(loanKeylet);
                if (!BEAST_EXPECT(loan))
                    return;
                BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));
                std::uint32_t const movedDueDate = loan->at(sfNextPaymentDueDate);
                BEAST_EXPECT(movedDueDate != originalNextDueDate);
                BEAST_EXPECT(movedDueDate < originalNextDueDate);
            }

            // Unimpair while still within the original payment interval. The
            // normal due date (startDate + 600) has not yet expired, so it
            // should be restored.
            env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tesSUCCESS));

            {
                auto const loan = env.le(loanKeylet);
                if (!BEAST_EXPECT(loan))
                    return;
                BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));
                BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
            }

            cleanupLoan(loanKeylet, originalNextDueDate);
        }

        // ---- Case B: impair before late, unimpair after original due date ----
        {
            auto const loanKeylet = createNewLoan();
            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            std::uint32_t const startDate = loanSle->at(sfStartDate);
            std::uint32_t const originalNextDueDate = loanSle->at(sfNextPaymentDueDate);
            BEAST_EXPECT(originalNextDueDate == startDate + 600);

            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));

            env.close(NetClock::time_point{NetClock::duration{originalNextDueDate}} + 10s);

            auto const timeBeforeUnimpair =
                env.current()->header().parentCloseTime.time_since_epoch().count();

            env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tesSUCCESS));

            {
                auto const loan = env.le(loanKeylet);
                if (!BEAST_EXPECT(loan))
                    return;
                BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));
                std::uint32_t const newDueDate = loan->at(sfNextPaymentDueDate);
                BEAST_EXPECT(newDueDate > originalNextDueDate);
                BEAST_EXPECT(newDueDate == timeBeforeUnimpair + 600);
            }
        }
    }

    // FN-68: a borrower must not be able to bypass late-payment charges by
    // paying an impaired, overdue loan with a plain LoanPay. Under
    // fixCleanup3_4_0 impairment no longer moves the due date, so
    // the payment logic sees the real (overdue) date: a regular payment is
    // rejected with tecEXPIRED, and only a tfLoanLatePayment (which charges
    // the late fee + late interest) is accepted.
    void
    testImpairedOverdueLoanPayRequiresLateFlag()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        testcase("Impaired overdue LoanPay requires late-payment flag");

        Env env(*this, all_ | fixCleanup3_4_0);
        BEAST_EXPECT(env.enabled(fixCleanup3_4_0));

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        // Loan with non-zero late-payment terms, so the late path carries a
        // real penalty that the exploit would otherwise avoid.
        Number const principalRequest{1, 3};
        env(set(borrower, broker.brokerID, broker.asset(principalRequest).value()),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(12),
            kPaymentInterval(600),
            kLatePaymentFee(broker.asset(3).number()),
            kLateInterestRate(TenthBips32{30322}),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const originalNextDueDate = loanSle->at(sfNextPaymentDueDate);
        std::uint32_t const paymentsBefore = loanSle->at(sfPaymentRemaining);
        BEAST_EXPECT(originalNextDueDate > 0);

        // Advance past the due date so the loan is overdue, then impair it
        // (impairment is only allowed once the payment is late).
        env.close(NetClock::time_point{NetClock::duration{originalNextDueDate}} + 1s);
        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();

        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
        }

        auto const payAmount = broker.asset(500).value();

        // The exploit: a plain LoanPay (Flags = 0) on an impaired, overdue
        // loan must be rejected. Before FN-9 the auto-unimpair pushed the due
        // date into the future and this returned tesSUCCESS, letting the
        // borrower skip the late fee and late interest.
        env(pay(borrower, loanKeylet.key, payAmount), Ter(tecEXPIRED));
        env.close();

        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == paymentsBefore);
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == originalNextDueDate);
        }

        env(pay(borrower, loanKeylet.key, payAmount, tfLoanLatePayment), Ter(tesSUCCESS));
        env.close();
        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == paymentsBefore - 1);
        }

        {
            auto const vaultSle = env.le(broker.vaultKeylet());
            if (!BEAST_EXPECT(vaultSle))
                return;
            BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == 0);
        }
    }

    // FN-68 (pre-amendment): documents the original vulnerability. Without
    // fixCleanup3_4_0, impairing moves the due date and LoanPay
    // auto-unimpair pushes it into the future before the late check, so a
    // plain (Flags = 0) LoanPay on an impaired, overdue loan is accepted as
    // on-time (tesSUCCESS) and the borrower dodges the late-payment charges.
    // This is what testImpairedOverdueLoanPayRequiresLateFlag closes once the
    // amendment is enabled.
    void
    testImpairedOverdueLoanPayBypassPreAmendment()
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        testcase("Impaired overdue LoanPay bypass (pre-amendment)");

        Env env(*this, all_ - fixCleanup3_4_0);
        BEAST_EXPECT(!env.enabled(fixCleanup3_4_0));

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        Number const principalRequest{1, 3};
        env(set(borrower, broker.brokerID, broker.asset(principalRequest).value()),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(12),
            kPaymentInterval(600),
            kLatePaymentFee(broker.asset(3).number()),
            kLateInterestRate(TenthBips32{30322}),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        std::uint32_t const originalNextDueDate = loanSle->at(sfNextPaymentDueDate);
        BEAST_EXPECT(originalNextDueDate > 0);

        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();

        env.close(NetClock::time_point{NetClock::duration{originalNextDueDate}} + 1s);

        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));
        }

        auto const payAmount = broker.asset(500).value();

        // The bug: a plain LoanPay is accepted as on-time and clears the
        // loan's impaired flag, so the late fee / late interest are never
        // charged.
        env(pay(borrower, loanKeylet.key, payAmount), Ter(tesSUCCESS));
        env.close();
        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));
        }
    }

    // Default uses NextPaymentDueDate + GracePeriod. Once fixCleanup3_4_0
    // is enabled, that gate is Exclusive, matching impair/isPaymentLate:
    // default is allowed only after grace has passed, not at the instant
    // it expires.
    void
    testLoanDefaultAtExactGraceExpiryRejectedPostAmendment()
    {
        testcase("LoanManage default at exact grace expiry rejected with fixCleanup3_4_0");

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        Env env(*this, all_);
        BEAST_EXPECT(env.enabled(fixCleanup3_4_0));

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, broker.asset(Number{1, 3}).value()),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(12),
            kPaymentInterval(600),
            kGracePeriod(60),
            Fee(env.current()->fees().base * 2));
        env.close();

        // Advance far enough that parentCloseTime > GracePeriod, so
        // (now - grace) cannot underflow when pinning the exact expiry.
        env.close(env.now() + 1000s);

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        auto const grace = loanSle->at(sfGracePeriod);
        std::uint32_t const now = env.current()->parentCloseTime().time_since_epoch().count();
        BEAST_EXPECT(now > grace + 1);

        // parentCloseTime == NextPaymentDueDate + GracePeriod: grace expires
        // this instant, so default must still be too soon.
        setLoanNextPaymentDueDate(env, loanKeylet, now - grace);
        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecTOO_SOON));
        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(!loan->isFlag(lsfLoanDefault));
        }

        // One second after grace expires, default succeeds.
        setLoanNextPaymentDueDate(env, loanKeylet, now - grace - 1);
        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(loan->isFlag(lsfLoanDefault));
        }
    }

    void
    testLoanDefaultAtExactGraceExpirySucceedsPreAmendment()
    {
        testcase("LoanManage default at exact grace expiry succeeds without fixCleanup3_4_0");

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        Env env(*this, all_ - fixCleanup3_4_0);
        BEAST_EXPECT(!env.enabled(fixCleanup3_4_0));

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

        env(set(borrower, broker.brokerID, broker.asset(Number{1, 3}).value()),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(12),
            kPaymentInterval(600),
            kGracePeriod(60),
            Fee(env.current()->fees().base * 2));
        env.close();

        env.close(env.now() + 1000s);

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        auto const grace = loanSle->at(sfGracePeriod);
        std::uint32_t const now = env.current()->parentCloseTime().time_since_epoch().count();
        BEAST_EXPECT(now > grace);

        setLoanNextPaymentDueDate(env, loanKeylet, now - grace);
        env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        {
            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(loan->isFlag(lsfLoanDefault));
        }
    }

    // Every signature on a transaction covered the same bytes before
    // fixCleanup3_4_0, so a signature could be moved from the role that made
    // it into another role. Here the lender signs a LoanSet as the
    // counterparty, and the borrower copies that signature into the
    // SponsorSignature, making the lender pay the fee without the lender ever
    // agreeing to sponsor it.
    void
    testSignatureCopiedBetweenRoles(bool fixEnabled)
    {
        testcase(
            std::string("Counterparty signature copied into the sponsor slot") +
            (fixEnabled ? "" : " (pre-amendment)"));

        using namespace jtx;
        using namespace loan;

        Env env(*this, fixEnabled ? all_ : all_ - fixCleanup3_4_0);
        BEAST_EXPECT(env.enabled(fixCleanup3_4_0) == fixEnabled);

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, xrpAsset, lender);

        auto const feeAmt = XRP(1);

        // The lender agrees to the loan by signing the Counterparty slot of a
        // LoanSet that names the lender as the fee sponsor. The lender signs
        // nothing else.
        auto loanSet = env.json(
            set(borrower, broker.brokerID, broker.asset(Number{1, 3}).value()),
            sponsor::As(lender, spfSponsorFee),
            Sig(sfCounterpartySignature, lender),
            Fee(feeAmt));

        // The borrower copies the lender's signature into the sponsor slot.
        loanSet[sfSponsorSignature.jsonName] = loanSet[sfCounterpartySignature.jsonName];

        auto const lenderBalance = env.balance(lender);
        auto const borrowerBalance = env.balance(borrower);

        env(loanSet, Ter(fixEnabled ? TER{telENV_RPC_FAILED} : TER{tesSUCCESS}));
        env.close();

        if (fixEnabled)
        {
            // The copied signature does not verify in the sponsor slot, so
            // nothing happens at all.
            BEAST_EXPECT(env.balance(lender) == lenderBalance);
            BEAST_EXPECT(env.balance(borrower) == borrowerBalance);
        }
        else
        {
            // The lender paid the fee, and the borrower got the loan.
            BEAST_EXPECT(env.balance(lender) == lenderBalance - feeAmt);
            BEAST_EXPECT(env.balance(borrower).value() > borrowerBalance.value());
        }
    }

    void
    runAmendmentIndependent()
    {
        testSignatureCopiedBetweenRoles(true);
        testSignatureCopiedBetweenRoles(false);
        testRIPD3901();
        testImpairmentPaymentDateUnchanged();
        testImpairmentPaymentDatePreAmendment();
        testImpairedOverdueLoanPayRequiresLateFlag();
        testImpairedOverdueLoanPayBypassPreAmendment();
        testLoanDefaultAtExactGraceExpiryRejectedPostAmendment();
        testLoanDefaultAtExactGraceExpirySucceedsPreAmendment();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testPoCUnsignedUnderflowOnFullPayAfterEarlyPeriodic(features);
        testRIPD3831(features);
        testRIPD3459(features);
        testRIPD3902(features);
    }

public:
    void
    run() override
    {
        runAmendmentIndependent();
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2}, all_))
            runAmendmentSensitive(features);
    }
};

BEAST_DEFINE_TESTSUITE(LoanSecurity, tx, xrpl);

}  // namespace xrpl::test
