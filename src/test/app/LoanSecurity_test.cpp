#include <test/app/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstdint>
#include <ostream>

namespace xrpl::test {

class LoanSecurity_test : public LoanTestBase
{
private:
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
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

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

            // Value-based proof: underflowed window yields a payoff larger than
            // the clamped (non-underflow) reference.
            BEAST_EXPECT(fullDue == fullDueClamped);
            if (fullDue > fullDueClamped)
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
            auto const jResult = jr[jss::result];
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

        Env env(*this);
        Vault const vault(env);

        env.fund(XRP(10'000), lender, issuer, borrower, depositor);
        env.close();

        auto [tx, vaultKeyLet] = vault.create({.owner = lender, .asset = xrpIssue()});
        env(tx, txFee);
        env.close();

        env(vault.deposit({.depositor = depositor, .id = vaultKeyLet.key, .amount = XRP(1'000)}),
            txFee);
        env.close();

        auto const brokerKeyLet = keylet::loanBroker(lender.id(), env.seq(lender));

        env(loanBroker::set(lender, vaultKeyLet.key), txFee);
        env.close();

        // BrokerInfo brokerInfo{xrpIssue(), keylet, vaultKeyLet, {}};

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
        auto const loanKeylet = keylet::loan(brokerKeyLet.key, loanSequence);

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

    void
    runAmendmentIndependent()
    {
        testRIPD3901();
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
