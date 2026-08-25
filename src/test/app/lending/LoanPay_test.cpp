#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace xrpl::test {

class LoanPay_test : public LoanTestBase
{
private:
#if LOAN_TODO
    void
    testLoanPayLateFullPaymentBypassesPenalties(FeatureBitset features)
    {
        testcase("LoanPay full payment skips late penalties");
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const asset = issuer[iouCurrency];
        env(trust(lender, asset(100'000'000)));
        env(trust(borrower, asset(100'000'000)));
        env(pay(issuer, lender, asset(50'000'000)));
        env(pay(issuer, borrower, asset(5'000'000)));
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, asset, lender)};

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        auto const brokerPreLoan = env.le(keylet::loanBroker(broker.brokerID));
        if (BEAST_EXPECT(brokerPreLoan); !brokerPreLoan.has_value())
            return;

        auto const loanSequence = brokerPreLoan->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(broker.brokerID, SeqProxy::rawSequence(loanSequence));

        Number const principal = asset(1'000).value();
        Number const serviceFee = asset(2).value();
        Number const lateFee = asset(5).value();
        Number const closeFee = asset(4).value();

        env(set(borrower, broker.brokerID, principal),
            Sig(sfCounterpartySignature, lender),
            kLoanServiceFee(serviceFee),
            kLatePaymentFee(lateFee),
            kClosePaymentFee(closeFee),
            kInterestRate(percentageToTenthBips(12)),
            kLateInterestRate(percentageToTenthBips(24) / 10),
            kCloseInterestRate(percentageToTenthBips(5)),
            kPaymentTotal(12),
            kPaymentInterval(600),
            kGracePeriod(0),
            Fee(loanSetFee));
        env.close();

        auto state1 = getCurrentState(env, broker, loanKeylet);
        if (!BEAST_EXPECT(state1.paymentRemaining > 1))
            return;

        using d = NetClock::duration;
        using tp = NetClock::time_point;
        auto const overdueClose = tp{d{state1.nextPaymentDate + state1.paymentInterval}};
        env.close(overdueClose);

        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(brokerSle && loanSle))
            return;

        auto state = getCurrentState(env, broker, loanKeylet);

        TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
        TenthBips32 const interestRateValue{loanSle->at(sfInterestRate)};
        TenthBips32 const lateInterestRateValue{loanSle->at(sfLateInterestRate)};
        TenthBips32 const closeInterestRateValue{loanSle->at(sfCloseInterestRate)};

        Number const closePaymentFeeRounded =
            roundToAsset(broker.asset, loanSle->at(sfClosePaymentFee), state.loanScale);
        Number const latePaymentFeeRounded =
            roundToAsset(broker.asset, loanSle->at(sfLatePaymentFee), state.loanScale);

        auto const roundedLoanState = constructLoanState(
            state.totalValue, state.principalOutstanding, state.managementFeeOutstanding);
        Number const totalInterestOutstanding = roundedLoanState.interestDue;

        auto const periodicRate = loanPeriodicRate(interestRateValue, state.paymentInterval);
        auto const rawLoanState = computeTheoreticalLoanState(
            env.current()->rules(),
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            managementFeeRate);

        auto const parentCloseTime = env.current()->parentCloseTime();
        auto const startDateSeconds =
            static_cast<std::uint32_t>(state.startDate.time_since_epoch().count());

        Number const fullPaymentInterest = computeFullPaymentInterest(
            rawLoanState.principalOutstanding,
            periodicRate,
            parentCloseTime,
            state.paymentInterval,
            state.previousPaymentDate,
            startDateSeconds,
            closeInterestRateValue);

        Number const roundedFullInterestAmount =
            roundToAsset(broker.asset, fullPaymentInterest, state.loanScale);
        Number const roundedFullManagementFee = computeManagementFee(
            broker.asset, roundedFullInterestAmount, managementFeeRate, state.loanScale);
        Number const roundedFullInterest = roundedFullInterestAmount - roundedFullManagementFee;

        Number const trackedValueDelta =
            state.principalOutstanding + totalInterestOutstanding + state.managementFeeOutstanding;
        Number const untrackedManagementFee =
            closePaymentFeeRounded + roundedFullManagementFee - state.managementFeeOutstanding;
        Number const untrackedInterest = roundedFullInterest - totalInterestOutstanding;

        Number const baseFullDue = trackedValueDelta + untrackedInterest + untrackedManagementFee;
        BEAST_EXPECT(baseFullDue == roundToAsset(broker.asset, baseFullDue, state.loanScale));

        auto const overdueSeconds =
            parentCloseTime.time_since_epoch().count() - state.nextPaymentDate;
        if (!BEAST_EXPECT(overdueSeconds > 0))
            return;

        Number const overdueRate = loanPeriodicRate(lateInterestRateValue, overdueSeconds);
        Number const lateInterestRaw = state.principalOutstanding * overdueRate;
        Number const lateInterestRounded =
            roundToAsset(broker.asset, lateInterestRaw, state.loanScale);
        Number const lateManagementFeeRounded = computeManagementFee(
            broker.asset, lateInterestRounded, managementFeeRate, state.loanScale);
        Number const penaltyDue =
            lateInterestRounded + lateManagementFeeRounded + latePaymentFeeRounded;
        BEAST_EXPECT(penaltyDue > Number{});

        auto const balanceBefore = env.balance(borrower, broker.asset).number();

        STAmount const paymentAmount{broker.asset.raw(), baseFullDue};
        env(pay(borrower, loanKeylet.key, paymentAmount, tfLoanFullPayment));
        env.close();

        if (auto const meta = env.meta(); BEAST_EXPECT(meta))
            BEAST_EXPECT(meta->at(sfTransactionResult) == tesSUCCESS);

        auto const balanceAfter = env.balance(borrower, broker.asset).number();
        Number const actualPaid = balanceBefore - balanceAfter;
        BEAST_EXPECT(actualPaid == baseFullDue);

        Number const expectedWithPenalty = baseFullDue + penaltyDue;
        BEAST_EXPECT(expectedWithPenalty > actualPaid);
        BEAST_EXPECT(expectedWithPenalty - actualPaid == penaltyDue);
    }
#endif

    void
    testOverpaymentManagementFee(FeatureBitset features)
    {
        testcase("testOverpaymentManagementFee");

        using namespace jtx;
        using namespace loan;

        Env env{*this, features};

        Account const lender{"lender"}, borrower{"borrower"};

        env.fund(XRP(10'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1000};

        auto const result = createVaultAndBroker(
            env,
            asset,
            lender,
            {
                .vaultDeposit = asset(100'000).value(),
                .managementFeeRate = TenthBips16(10'000),
            });

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        auto const brokerSle = env.le(result.brokerKeylet());
        if (!BEAST_EXPECT(brokerSle))
            return;
        auto const loanKeylet = keylet::loan(
            result.brokerKeylet().key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        env(loan::set(
                borrower, result.brokerKeylet().key, asset(10'000).value(), tfLoanOverpayment),
            Sig(sfCounterpartySignature, lender),
            loan::kPaymentInterval(86400 * 30),
            loan::kPaymentTotal(3),
            loan::kOverpaymentInterestRate(TenthBips32(percentageToTenthBips(20))),
            loanSetFee);

        // From calculator
        auto const expectedOverpaymentManagementFee = Number{33333, 0};
        auto const loanBrokerBalanceBefore = env.balance(lender);

        auto const loanPayFee = Fee(env.current()->fees().base * 2);
        env(pay(borrower, loanKeylet.key, asset(5'000).value(), tfLoanOverpayment), loanPayFee);
        env.close();

        BEAST_EXPECTS(
            env.balance(lender) - loanBrokerBalanceBefore == expectedOverpaymentManagementFee,
            "overpayment management fee mismatch; expected:" +
                to_string(expectedOverpaymentManagementFee) +
                " got: " + to_string(env.balance(lender) - loanBrokerBalanceBefore));
    }

    void
    testDosLoanPay(FeatureBitset features)
    {
        bool const feeCapped = features[fixCleanup3_1_3];

        // From FIND-005
        testcase << "DoS LoanPay: fee calculation " << (feeCapped ? "capped" : "uncapped");

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace lending;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        BEAST_EXPECT(feeCapped == env.current()->rules().enabled(fixCleanup3_1_3));

        PrettyAsset const iouAsset = issuer[iouCurrency_];
        env(trust(lender, iouAsset(100'000'000)));
        env(trust(borrower, iouAsset(100'000'000)));
        env(pay(issuer, lender, iouAsset(10'000'000)));
        env(pay(issuer, borrower, iouAsset(1'000)));
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{3959'37, -2};
        auto const baseFee = env.current()->fees().base;

        auto const createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            Fee(loanSetFee),
            Json(sfCounterpartySignature, json::ValueType::Object),
            kClosePaymentFee(0),
            kGracePeriod(60),
            kInterestRate(TenthBips32(20930)),
            kLateInterestRate(TenthBips32(77049)),
            kLatePaymentFee(0),
            kLoanServiceFee(0),
            kOverpaymentFee(TenthBips32(7)),
            kOverpaymentInterestRate(TenthBips32(66653)),
            kPaymentInterval(60),
            kPaymentTotal(3239184));

        // There are enough payments due on this loan that it only needs to be
        // created once, and can be paid on multiple times. Just don't create a
        // gazillion test cases.
        auto const keylet = nextLoanKeylet(env, broker);

        env(createJson, Sig(sfCounterpartySignature, lender));
        env.close();

        auto const roundedPayment = [&]() {
            auto const stateBefore = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(stateBefore.paymentRemaining == 3239184);
            BEAST_EXPECT(stateBefore.paymentRemaining > kLoanMaximumPaymentsPerTransaction);

            return roundToAsset(
                iouAsset,
                stateBefore.periodicPayment,
                stateBefore.loanScale,
                Number::RoundingMode::Upward);
        }();

        auto test = [&](int const payFactor,
                        int const feeFactor,
                        TER const expectedTer = tesSUCCESS) {
            auto const stateBefore = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(stateBefore.paymentRemaining <= 3239184);
            BEAST_EXPECT(stateBefore.paymentRemaining > kLoanMaximumPaymentsPerTransaction);

            Number const amount = roundedPayment * payFactor;
            auto loanPayTx = env.json(pay(borrower, keylet.key, STAmount{broker.asset, amount}));
            XRPAmount const payFee{baseFee * feeFactor};
            env(loanPayTx, Ter(expectedTer), Fee(payFee));
            env.close();
            auto const expectedChange = isTesSuccess(expectedTer)
                ? std::min(kLoanMaximumPaymentsPerTransaction, payFactor)
                : 0;

            auto const stateAfter = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(
                stateAfter.paymentRemaining == stateBefore.paymentRemaining - expectedChange);
        };

        static constexpr std::int64_t kMaxFeeIncrements =
            kLoanMaximumPaymentsPerTransaction / kLoanPaymentsPerFeeIncrement;

        TER const failWithoutFix = feeCapped ? (TER)tesSUCCESS : (TER)telINSUF_FEE_P;

        // * Amount well above threshold -> capped fee
        // The original test case - way over the limit - more fee is always ok
        test(1819878, 363976);
        // The capped fee is only sufficient if the amendment is enabled.
        test(1819878, kMaxFeeIncrements, failWithoutFix);

        // * Amount exactly at threshold -> capped fee
        test(kLoanMaximumPaymentsPerTransaction, kMaxFeeIncrements);
        // More fee is always ok
        test(kLoanMaximumPaymentsPerTransaction, kMaxFeeIncrements + 10);

        // * Amount below threshold -> normal calculation
        test(1, 1);
        test(kLoanPaymentsPerFeeIncrement * 2, 2);
        test(0, 0, temBAD_AMOUNT);
        test(0, 1, temBAD_AMOUNT);
        // Fee difference rounds evenly
        test(
            kLoanMaximumPaymentsPerTransaction - 10,
            ((kLoanMaximumPaymentsPerTransaction - 10) / kLoanPaymentsPerFeeIncrement) - 1,
            telINSUF_FEE_P);
        test(
            kLoanMaximumPaymentsPerTransaction - 10,
            ((kLoanMaximumPaymentsPerTransaction - 10) / kLoanPaymentsPerFeeIncrement));
        // More fee is always ok
        test(
            kLoanMaximumPaymentsPerTransaction - 10,
            ((kLoanMaximumPaymentsPerTransaction - 10) / kLoanPaymentsPerFeeIncrement) + 3);
        // Fee rounds up
        for (int under = 1; under < kLoanPaymentsPerFeeIncrement; ++under)
        {
            test(kLoanMaximumPaymentsPerTransaction - under, kMaxFeeIncrements - 1, telINSUF_FEE_P);
            test(kLoanMaximumPaymentsPerTransaction - under, kMaxFeeIncrements);
        }
        // Only when you get one less fee increment can you pay less
        test(
            kLoanMaximumPaymentsPerTransaction - kLoanPaymentsPerFeeIncrement,
            kMaxFeeIncrements - 1);
        // And again, more fee is always ok.
        test(kLoanMaximumPaymentsPerTransaction - kLoanPaymentsPerFeeIncrement, kMaxFeeIncrements);
    }

    // A LoanSet with InterestRate = 1 (0.001% annualized, the minimum non-zero
    // rate). At such a near-zero rate the closed-form payment factor
    // (1 + r)^n - 1 cancels catastrophically.
    //
    // Without fixCleanup3_2_0 the resulting amortization is degenerate and the
    // LoanSet is rejected with tecPRECISION_LOSS (no loan created). With the
    // amendment, computePowerMinusOneHybrid uses a numerically-stable series
    // expansion, so the loan is created and the scheduled payments
    // (2 * periodicPayment) cover the principal — no economic underpayment
    // (yield theft).
    //
    // The test runs the same LoanSet under both amendment settings and pins the
    // exact outcome for each.
    void
    testLoanSetNearZeroInterestRateSucceeds()
    {
        testcase("LoanSet near-zero interest rate covers principal");

        using namespace jtx;
        using namespace loan;

        Number const principalRequested{1000};

        struct Result
        {
            TER ter = tesSUCCESS;
            bool created = false;
            std::int32_t loanScale = 0;
            Number principal;
            Number totalValue;
            Number managementFee;
            Number periodicPayment;
        };

        auto runScenario = [&](FeatureBitset features, TER expectedTer) -> Result {
            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"vaultOwner"};
            Account const borrower{"borrower"};

            PrettyAsset const iouAsset = createFundedRippleIouAsset(env, issuer, lender, borrower);

            auto const broker = createVaultAndBroker(
                env,
                iouAsset,
                lender,
                {.vaultDeposit = 100'000, .debtMax = 0, .managementFeeRate = TenthBips16{0}});

            auto const brokerSle = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerSle);
            auto const loanSequence = brokerSle ? brokerSle->at(sfLoanSequence) : 0;
            auto const loanKeylet =
                keylet::loan(broker.brokerID, SeqProxy::rawSequence(loanSequence));

            env(set(borrower, broker.brokerID, principalRequested),
                Sig(sfCounterpartySignature, lender),
                kInterestRate(TenthBips32{1}),
                kPaymentTotal(2),
                kPaymentInterval(400),
                Fee(env.current()->fees().base * 2),
                Ter(expectedTer));
            env.close();

            Result r;
            r.ter = env.ter();
            if (auto const loanSle = env.le(loanKeylet))
            {
                r.created = true;
                r.loanScale = loanSle->at(sfLoanScale);
                r.principal = loanSle->at(sfPrincipalOutstanding);
                r.totalValue = loanSle->at(sfTotalValueOutstanding);
                r.managementFee = loanSle->at(sfManagementFeeOutstanding);
                r.periodicPayment = loanSle->at(sfPeriodicPayment);
            }
            return r;
        };

        Result const fixed = runScenario(all_, tesSUCCESS);
        Result const legacy = runScenario(all_ - fixCleanup3_2_0, tecPRECISION_LOSS);

        // Without the amendment, the catastrophically-cancelling closed-form
        // payment factor produces a degenerate amortization that fails
        // checkLoanGuards: the LoanSet is rejected with tecPRECISION_LOSS and no
        // loan is created.
        BEAST_EXPECT(legacy.ter == tecPRECISION_LOSS);
        BEAST_EXPECT(!legacy.created);

        // With the amendment the stable series expansion produces a valid loan
        // at loanScale -10.
        BEAST_EXPECT(fixed.ter == tesSUCCESS);
        BEAST_EXPECT(fixed.created);
        BEAST_EXPECT(fixed.loanScale == -10);
        BEAST_EXPECT(fixed.principal == principalRequested);
        BEAST_EXPECT((fixed.totalValue == Number{10000000001903, -10}));
        BEAST_EXPECT(fixed.managementFee == beast::kZero);

        // Periodic payment from the numerically-stable series expansion, and the
        // scheduled total (2 * periodicPayment) which exceeds the 1000 principal
        // — no economic underpayment / yield theft.
        BEAST_EXPECT((fixed.periodicPayment == Number{5000000000951293762, -16}));
        BEAST_EXPECT((fixed.periodicPayment * 2 == Number{1000000000190258752, -15}));
        BEAST_EXPECT(fixed.periodicPayment * 2 > principalRequested);
    }

    void
    testLoanNextPaymentDueDateOverflow(FeatureBitset features)
    {
        // For FIND-013
        testcase << "Prevent nextPaymentDueDate overflow";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace lending;
        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        PrettyAsset const iouAsset =
            createFundedIouAsset(env, issuer, lender, borrower, 100'000'000, 10'000'000);

        BrokerParameters const brokerParams{.debtMax = Number{0}, .coverRateMin = TenthBips32{1}};
        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender, brokerParams)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        using timeType = decltype(sfNextPaymentDueDate)::type::value_type;
        static_assert(std::is_same_v<timeType, std::uint32_t>);
        constexpr timeType kMaxTime = std::numeric_limits<timeType>::max();
        static_assert(kMaxTime == 4'294'967'295);

        auto const baseJson = [&]() {
            auto createJson = env.json(
                set(borrower, broker.brokerID, Number{55524'81, -2}),
                Fee(loanSetFee),
                kClosePaymentFee(0),
                kGracePeriod(LoanSet::kDefaultGracePeriod),
                kInterestRate(TenthBips32(12833)),
                kLateInterestRate(TenthBips32(77048)),
                kLatePaymentFee(0),
                kLoanOriginationFee(218),
                Json(sfCounterpartySignature, json::ValueType::Object));

            createJson.removeMember(sfSequence.getJsonName());

            return createJson;
        }();

        auto const baseFee = env.current()->fees().base;

        auto parentCloseTime = [&]() {
            return env.current()->parentCloseTime().time_since_epoch().count();
        };
        auto maxLoanTime = [&]() {
            auto const startDate = parentCloseTime();

            BEAST_EXPECT(startDate >= 50);

            return kMaxTime - startDate;
        };

        {
            // straight-up overflow: interval
            auto const interval = maxLoanTime() + 1;
            auto const total = 1;
            auto createJson = env.json(baseJson, kPaymentInterval(interval), kPaymentTotal(total));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tecKILLED));
            env.close();
        }
        {
            // straight-up overflow: total
            // min interval is 60
            auto const interval = 60;
            auto const total = maxLoanTime() + 1;
            auto createJson = env.json(baseJson, kPaymentInterval(interval), kPaymentTotal(total));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tecKILLED));
            env.close();
        }
        {
            // straight-up overflow: grace period
            // min interval is 60
            auto const interval = maxLoanTime() + 1;
            auto const total = 1;
            auto const grace = interval;
            auto createJson = env.json(
                baseJson, kPaymentInterval(interval), kPaymentTotal(total), kGracePeriod(grace));

            // The grace period can't be larger than the interval.
            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tecKILLED));
            env.close();
        }
        {
            // Overflow with multiplication of a few large intervals
            auto const interval = 1'000'000'000;
            auto const total = 10;
            auto createJson = env.json(baseJson, kPaymentInterval(interval), kPaymentTotal(total));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tecKILLED));
            env.close();
        }
        {
            // Overflow with multiplication of many small payments
            // min interval is 60
            auto const interval = 60;
            auto const total = 1'000'000'000;
            auto createJson = env.json(baseJson, kPaymentInterval(interval), kPaymentTotal(total));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tecKILLED));
            env.close();
        }
        {
            // Overflow with an absurdly large grace period
            // min interval is 60
            auto const total = 60;
            auto const interval = (maxLoanTime() - total) / total;
            auto const grace = interval;
            auto createJson = env.json(
                baseJson, kPaymentInterval(interval), kPaymentTotal(total), kGracePeriod(grace));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tecKILLED));
            env.close();
        }
        {
            // Start date when the ledger is closed will be larger
            auto const keylet = nextLoanKeylet(env, broker);

            auto const grace = 100;
            auto const interval = maxLoanTime() - grace;
            auto const total = 1;
            auto createJson = env.json(
                baseJson, kPaymentInterval(interval), kPaymentTotal(total), kGracePeriod(grace));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tesSUCCESS));
            env.close();

            // The transaction is killed in the closed ledger
            auto const meta = env.meta();
            if (BEAST_EXPECT(meta))
            {
                BEAST_EXPECT(meta->at(sfTransactionResult) == tecKILLED);
            }

            // If the transaction had succeeded, the loan would exist
            auto const loanSle = env.le(keylet);
            // but it doesn't
            BEAST_EXPECT(!loanSle);
        }
        {
            // Start date when the ledger is closed will be larger
            auto const keylet = nextLoanKeylet(env, broker);

            auto const closeStartDate = ((parentCloseTime() / 10) + 1) * 10;
            auto const grace = 5'000;
            auto const interval = kMaxTime - closeStartDate - grace;
            auto const total = 1;
            auto createJson = env.json(
                baseJson, kPaymentInterval(interval), kPaymentTotal(total), kGracePeriod(grace));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tesSUCCESS));
            env.close();

            // The transaction succeeds in the closed ledger
            auto const meta = env.meta();
            if (BEAST_EXPECT(meta))
            {
                BEAST_EXPECT(meta->at(sfTransactionResult) == tesSUCCESS);
            }

            // This loan exists
            auto const afterState = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(afterState.nextPaymentDate == kMaxTime - grace);
            BEAST_EXPECT(afterState.previousPaymentDate == 0);
            BEAST_EXPECT(afterState.paymentRemaining == 1);
        }

        {
            // Ensure the borrower has funds to pay back the loan
            env(pay(issuer, borrower, iouAsset(Number{1'055'524'81, -2})));

            // Start date when the ledger is closed will be larger
            auto const closeStartDate = ((parentCloseTime() / 10) + 1) * 10;
            auto const grace = 5'000;
            auto const maxLoanTime = kMaxTime - closeStartDate - grace;
            auto const total = [&]() {
                if (maxLoanTime % 5 == 0)
                    return 5;
                if (maxLoanTime % 3 == 0)
                    return 3;
                if (maxLoanTime % 2 == 0)
                    return 2;
                return 0;
            }();
            if (!BEAST_EXPECT(total != 0))
                return;

            auto const brokerState = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerState))
                return;
            // Intentionally shadow the outer values
            auto const loanSequence = brokerState->at(sfLoanSequence);
            auto const keylet = keylet::loan(broker.brokerID, SeqProxy::rawSequence(loanSequence));

            auto const interval = maxLoanTime / total;
            auto createJson = env.json(
                baseJson, kPaymentInterval(interval), kPaymentTotal(total), kGracePeriod(grace));

            env(createJson, Sig(sfCounterpartySignature, lender), Ter(tesSUCCESS));
            env.close();

            // This loan exists
            auto const beforeState = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(beforeState.nextPaymentDate == closeStartDate + interval);
            BEAST_EXPECT(beforeState.previousPaymentDate == 0);
            BEAST_EXPECT(beforeState.paymentRemaining == total);
            BEAST_EXPECT(beforeState.periodicPayment > 0);

            // pay all but the last payment
            {
                NumberRoundModeGuard const mg{Number::RoundingMode::Upward};
                Number const payment = beforeState.periodicPayment * (total - 1);
                XRPAmount const payFee{baseFee * ((total - 1) / kLoanPaymentsPerFeeIncrement + 1)};
                STAmount const paymentAmount =
                    roundToScale(STAmount{broker.asset, payment}, beforeState.loanScale);
                auto loanPayTx = env.json(pay(borrower, keylet.key, paymentAmount), Fee(payFee));
                env(loanPayTx, Ter(tesSUCCESS));
                env.close();
            }

            // The loan is on the last payment
            auto const afterState = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(afterState.paymentRemaining == 1);
            BEAST_EXPECT(afterState.nextPaymentDate == kMaxTime - grace);
            BEAST_EXPECT(afterState.previousPaymentDate == kMaxTime - grace - interval);
        }
    }

    // Which pseudo-account is left holding an unauthorized trust line when the
    // repayment lands.
    enum class UnauthorizedPayee {
        // The vault's own line, as VaultCreate leaves it.
        Vault,
        // Same vault, but the issuer authorized the line by hand first.
        VaultAuthorized,
        // Vault line authorized, broker owner unable to take the fee, so the
        // fee goes to the loan broker's pseudo-account instead.
        Broker,
    };

    // A vault holding an IOU whose issuer requires authorization ends up with
    // its own trust line unauthorized: VaultCreate opens the line without the
    // auth flag, and the pseudo-account has no key to sign a TrustSet for
    // itself. Neither deposits nor loan origination look at that line, so the
    // vault appears to work right up to the first repayment, which is the only
    // step that has to credit the vault back.
    //
    // The loan broker's pseudo-account has the same defect for the same reason,
    // and LoanPay reaches it whenever the broker owner cannot take the fee.
    //
    // The issuer can still repair either line by hand, because TrustSet accepts
    // a line that already exists even when its owner is a pseudo-account.
    void
    testRepayIntoUnauthorizedVault()
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        auto runTestCases = [&](FeatureBitset features, UnauthorizedPayee payee) {
            bool const pseudoExempt = features[fixCleanup3_4_0];
            // With the vault's line repaired by the issuer, the only remaining
            // unauthorized payee is the broker's pseudo-account.
            bool const expectSuccess = pseudoExempt || payee == UnauthorizedPayee::VaultAuthorized;

            auto const payeeLabel = [payee]() -> char const* {
                switch (payee)
                {
                    case UnauthorizedPayee::Vault:
                        return "vault";
                    case UnauthorizedPayee::VaultAuthorized:
                        return "vault authorized by the issuer";
                    case UnauthorizedPayee::Broker:
                        return "loan broker";
                }
                return "";  // LCOV_EXCL_LINE
            }();

            testcase << "LoanPay crediting an unauthorized " << payeeLabel << ": pseudo-account "
                     << (pseudoExempt ? "exempt" : "not exempt");

            Env env{*this, features};

            env.fund(XRP(1'000'000), issuer, lender, borrower);
            env.close();

            env(fset(issuer, asfRequireAuth));
            env.close();

            PrettyAsset const asset = issuer[iouCurrency_];
            env(trust(lender, asset(100'000'000)));
            env(trust(borrower, asset(100'000'000)));
            env.close();

            // Authorize the two participants. Nothing asks the issuer to also
            // authorize the vault, which is the whole point of this test.
            env(trust(issuer, asset(0), lender, tfSetfAuth));
            env(trust(issuer, asset(0), borrower, tfSetfAuth));
            env.close();

            env(pay(issuer, lender, asset(10'000'000)));
            env(pay(issuer, borrower, asset(10'000)));
            env.close();

            // Creating the vault and funding it with deposits succeeds even
            // though the vault cannot be authorized to hold the asset.
            BrokerInfo const broker{createVaultAndBroker(env, asset, lender)};

            auto const vaultSle = env.le(broker.vaultKeylet());
            auto const brokerSle = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(vaultSle && brokerSle))
                return;

            Account const vaultPseudo{"vault pseudo-account", vaultSle->at(sfAccount)};
            Account const brokerPseudo{"broker pseudo-account", brokerSle->at(sfAccount)};

            auto const lineIsAuthorized = [&](Account const& holder) -> bool {
                auto const line = env.le(keylet::trustLine(holder, asset.raw().get<Issue>()));
                if (!BEAST_EXPECT(line))
                    return false;
                return line->isFlag(holder.id() > issuer.id() ? lsfLowAuth : lsfHighAuth);
            };

            BEAST_EXPECT(!lineIsAuthorized(vaultPseudo));
            BEAST_EXPECT(!lineIsAuthorized(brokerPseudo));

            if (payee != UnauthorizedPayee::Vault)
            {
                env(trust(issuer, asset(0), vaultPseudo, tfSetfAuth));
                env.close();
                BEAST_EXPECT(lineIsAuthorized(vaultPseudo));
            }

            using namespace loan;

            // The service fee guarantees the broker is owed something on the
            // first payment, so the broker leg of the transfer is exercised.
            Number const serviceFee = asset(2).value();
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(set(borrower, broker.brokerID, asset(1'000).value()),
                Sig(sfCounterpartySignature, lender),
                kLoanServiceFee(serviceFee),
                kInterestRate(percentageToTenthBips(12)),
                kPaymentTotal(12),
                kPaymentInterval(600),
                Fee(env.current()->fees().base * 2));
            env.close();

            // Paying the principal out of the vault never needed authorization.
            BEAST_EXPECT(env.le(loanKeylet));

            if (payee == UnauthorizedPayee::Broker)
            {
                // A deep-frozen owner cannot take the fee, so LoanPay pays it
                // into the broker's pseudo-account instead.
                env(trust(issuer, asset(0), lender, tfSetFreeze | tfSetDeepFreeze));
                env.close();
            }

            auto const state = getCurrentState(env, broker, loanKeylet);
            STAmount const payment{
                broker.asset,
                roundPeriodicPayment(
                    broker.asset, state.periodicPayment + serviceFee, state.loanScale)};

            // Repayment turns an outstanding loan back into cash the vault can
            // lend again, so AssetsAvailable is what moves. AssetsTotal already
            // counted the loan.
            auto const assetsAvailable = [&]() -> Number {
                auto const sle = env.le(broker.vaultKeylet());
                if (!BEAST_EXPECT(sle))
                    return Number{};
                return sle->at(sfAssetsAvailable);
            };

            auto const borrowerBefore = env.balance(borrower, asset).number();
            auto const vaultBefore = env.balance(vaultPseudo, asset).number();
            auto const brokerBefore = env.balance(brokerPseudo, asset).number();
            auto const assetsAvailableBefore = assetsAvailable();

            env(pay(borrower, loanKeylet.key, payment),
                Ter(expectSuccess ? TER{tesSUCCESS} : TER{tecNO_AUTH}));
            env.close();

            if (expectSuccess)
            {
                BEAST_EXPECT(env.balance(borrower, asset).number() < borrowerBefore);
                BEAST_EXPECT(env.balance(vaultPseudo, asset).number() > vaultBefore);
                BEAST_EXPECT(assetsAvailable() > assetsAvailableBefore);
                // Confirms the broker variant really did route the fee to the
                // pseudo-account rather than to the owner.
                BEAST_EXPECT(
                    (env.balance(brokerPseudo, asset).number() > brokerBefore) ==
                    (payee == UnauthorizedPayee::Broker));

                // The payee is skipped by the check, not authorized by it: the line that just
                // took the credit is still missing its auth flag.
                if (payee == UnauthorizedPayee::Vault)
                    BEAST_EXPECT(!lineIsAuthorized(vaultPseudo));
                if (payee == UnauthorizedPayee::Broker)
                    BEAST_EXPECT(!lineIsAuthorized(brokerPseudo));
            }
            else
            {
                // A rejected repayment must leave every balance untouched.
                BEAST_EXPECT(env.balance(borrower, asset).number() == borrowerBefore);
                BEAST_EXPECT(env.balance(vaultPseudo, asset).number() == vaultBefore);
                BEAST_EXPECT(env.balance(brokerPseudo, asset).number() == brokerBefore);
                BEAST_EXPECT(assetsAvailable() == assetsAvailableBefore);
            }
        };

        for (auto const& features : {all_, all_ - fixCleanup3_4_0})
        {
            runTestCases(features, UnauthorizedPayee::Vault);
            runTestCases(features, UnauthorizedPayee::VaultAuthorized);
            runTestCases(features, UnauthorizedPayee::Broker);
        }
    }

    void
    testLoanPayFundsConservedPayeeBelowReserve(FeatureBitset features)
    {
        // Regression test: LoanPay::doApply's fund-conservation check used to
        // read XRP balances via accountHolds(..., SpendableHandling::
        // FullBalance), which for XRP always defers to xrpLiquid (balance
        // minus reserve, clamped at zero). When the broker fee landed on a
        // payee sitting below its own reserve, that payee's clamped balance
        // stayed zero and the fee vanished from the conservation sum,
        // tripping "funds are conserved (with rounding)".
        testcase("LoanPay funds conserved: broker fee payee below reserve");

        using namespace jtx;

        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        // Broker defaults match the fuzz workload: ManagementFeeRate = 100
        // tenth-bips. The service fee guarantees feePaid > 0 on the first
        // regular payment.
        BrokerParameters const brokerParams;
        Number const serviceFeeValue{2};
        LoanParameters const loanParams{
            .account = borrower,
            .counter = lender,
            .principalRequest = 1000,
            .serviceFee = serviceFeeValue,
            .interest = TenthBips32{percentageToTenthBips(12)},
            .payTotal = 12,
            .payInterval = 3600};

        auto const loanOpt =
            createLoan(env, AssetType::XRP, brokerParams, loanParams, issuer, lender, borrower);
        if (BEAST_EXPECT(loanOpt); !loanOpt.has_value())
            return;
        auto const& [broker, loanKeylet, brokerPseudo] = *loanOpt;

        auto const vaultPseudo = [&]() {
            auto const vaultSle = env.le(keylet::vault(broker.vaultID));
            if (!BEAST_EXPECT(vaultSle))
                return AccountID{};
            return vaultSle->at(sfAccount);
        }();

        // Raw AccountRoot balance, matching LoanPay::doApply's conservation
        // check (not the reserve-clamped accountHolds()/xrpLiquid() value).
        auto rawBalance = [&](AccountID const& id) -> STAmount {
            auto const sle = env.le(keylet::account(id));
            if (!BEAST_EXPECT(sle))
                return STAmount{};
            return sle->getFieldAmount(sfBalance);
        };
        auto lenderReserve = [&] {
            return env.current()->fees().accountReserve(ownerCount(env, lender), 1);
        };

        STAmount const baseFee{env.current()->fees().base};

        // Park the lender (broker owner, fee payee) exactly at its reserve,
        // then burn part of the reserve with an oversized transaction fee.
        // Fees are exempt from the reserve check, so the balance ends up
        // below the reserve.
        env(pay(lender, issuer, rawBalance(lender.id()) - lenderReserve() - baseFee));
        env(noop(lender), Fee(XRP(100)));
        env.close();
        BEAST_EXPECT(env.balance(lender) < lenderReserve());

        // First regular payment, exactly the amount due.
        auto const state = getCurrentState(env, broker, loanKeylet);
        STAmount const serviceFee = broker.asset(serviceFeeValue);
        STAmount const roundedPeriodicPayment{
            broker.asset,
            roundPeriodicPayment(broker.asset, state.periodicPayment, state.loanScale)};
        STAmount const totalDue = roundToScale(
            roundedPeriodicPayment + serviceFee, state.loanScale, Number::RoundingMode::Upward);

        auto const borrowerBefore = rawBalance(borrower.id());
        auto const vaultBefore = rawBalance(vaultPseudo);
        auto const lenderBefore = rawBalance(lender.id());

        // Before the fix, this aborted inside LoanPay::doApply on
        // XRPL_ASSERT_PARTS(goodRounding, "xrpl::LoanPay::doApply", "funds
        // are conserved (with rounding)").
        env(loan::pay(borrower, loanKeylet.key, totalDue));
        env.close();

        auto const borrowerAfter = rawBalance(borrower.id());
        auto const vaultAfter = rawBalance(vaultPseudo);
        auto const lenderAfter = rawBalance(lender.id());

        // The broker fee reached the lender's AccountRoot, even though the
        // lender's balance remains below its reserve.
        BEAST_EXPECT(lenderAfter > lenderBefore);
        BEAST_EXPECT(lenderAfter < lenderReserve());

        // Total funds conserved across the payer, vault, and fee payee.
        BEAST_EXPECT(
            borrowerBefore - baseFee + vaultBefore + lenderBefore ==
            borrowerAfter + vaultAfter + lenderAfter);
    }

    void
    runAmendmentIndependent()
    {
        testLoanSetNearZeroInterestRateSucceeds();
        testRepayIntoUnauthorizedVault();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
#if LOAN_TODO
        testLoanPayLateFullPaymentBypassesPenalties(features);
#endif
        testLoanPayFundsConservedPayeeBelowReserve(features);
        testOverpaymentManagementFee(features);
        testDosLoanPay(features);
        testLoanNextPaymentDueDateOverflow(features);
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

BEAST_DEFINE_TESTSUITE(LoanPay, tx, xrpl);

}  // namespace xrpl::test
