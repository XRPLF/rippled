#include <test/app/LoanTestBase.h>

namespace xrpl::test {

class LoanInvariant_test : public LoanTestBase
{
private:
    // Each of these regression tests reproduces a single fuzzer-found (FIND-*)
    // scenario against xrpl::detail::computePeriodicPayment /
    // loanComputePaymentParts. They're merged into one function, one block
    // per finding, because each is a narrow, self-contained repro that
    // shares little beyond the surrounding scaffold.
    void
    testLoanPayComputePeriodicPaymentInvariants(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;

        // From FIND-012
        {
            testcase << "LoanPay xrpl::detail::computePeriodicPayment : "
                        "valid rate";

            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            BrokerParameters const brokerParams;
            env.fund(XRP(brokerParams.vaultDeposit * 100), issuer, lender, borrower);
            env.close();

            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
            BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

            using namespace loan;

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const principalRequest{640562, -5};

            Number const serviceFee{2462611968};
            std::uint32_t const numPayments{4294967295 / 800};

            auto createJson = env.json(
                set(borrower, broker.brokerID, principalRequest),
                Fee(loanSetFee),
                kLoanServiceFee(serviceFee),
                kPaymentTotal(numPayments),
                Json(sfCounterpartySignature, json::ValueType::Object));

            createJson["CloseInterestRate"] = 55374;
            createJson["ClosePaymentFee"] = "3825205248";
            createJson["LatePaymentFee"] = "237";
            createJson["LoanOriginationFee"] = "0";
            createJson["OverpaymentFee"] = 35167;
            createJson["OverpaymentInterestRate"] = 1360;
            createJson["PaymentInterval"] = 727;

            auto const keylet = nextLoanKeylet(env, broker);

            createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
            // Fails in preclaim because principal requested can't be
            // represented as XRP
            env(createJson, Ter(tecPRECISION_LOSS));
            env.close();

            BEAST_EXPECT(!env.le(keylet));

            Number const actualPrincipal{6};

            createJson[sfPrincipalRequested] = actualPrincipal;
            createJson.removeMember(sfSequence.jsonName);
            createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
            // Fails in doApply because the payment is too small to be
            // represented as XRP.
            env(createJson, Ter(tecPRECISION_LOSS));
            env.close();
        }

        // From FIND-010
        {
            testcase << "xrpl::loanComputePaymentParts : valid total interest";

            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            PrettyAsset const iouAsset = createFundedIouAsset(env, issuer, lender, borrower);

            BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender)};

            using namespace loan;

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const principalRequest{1, 3};

            auto createJson = env.json(
                set(borrower, broker.brokerID, principalRequest),
                Fee(loanSetFee),
                Json(sfCounterpartySignature, json::ValueType::Object));

            createJson["CloseInterestRate"] = 47299;
            createJson["ClosePaymentFee"] = "3985819770";
            createJson["InterestRate"] = 92;
            createJson["LatePaymentFee"] = "3866894865";
            createJson["LoanOriginationFee"] = "0";
            createJson["LoanServiceFee"] = "2348810240";
            createJson["OverpaymentFee"] = 58545;
            createJson["PaymentInterval"] = 60;
            createJson["PaymentTotal"] = 1;
            createJson["PrincipalRequested"] = "0.000763058";

            auto const keylet = nextLoanKeylet(env, broker);

            createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
            env(createJson);
            env.close();

            auto loanPayTx = env.json(pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
            loanPayTx["Amount"]["value"] = "0.000281284125490196";
            env(loanPayTx, Ter(tecINSUFFICIENT_PAYMENT));
            env.close();
        }

        // From FIND-009
        {
            testcase << "xrpl::loanComputePaymentParts : totalPrincipalPaid "
                        "rounded";

            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            PrettyAsset const iouAsset = createFundedIouAsset(env, issuer, lender, borrower);

            BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender)};

            using namespace loan;

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const principalRequest{1, 3};

            auto createJson = env.json(
                set(borrower, broker.brokerID, principalRequest),
                Fee(loanSetFee),
                Json(sfCounterpartySignature, json::ValueType::Object));

            createJson["ClosePaymentFee"] = "0";
            createJson["InterestRate"] = 24346;
            createJson["LateInterestRate"] = 65535;
            createJson["LatePaymentFee"] = "0";
            createJson["LoanOriginationFee"] = "218";
            createJson["LoanServiceFee"] = "0";
            createJson["PaymentInterval"] = 60;
            createJson["PaymentTotal"] = 5678;
            createJson["PrincipalRequested"] = "9924.81";

            auto const keylet = nextLoanKeylet(env, broker);

            createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
            env(createJson, Ter(tesSUCCESS));
            env.close();

            auto const baseFee = env.current()->fees().base;

            auto const stateBefore = getCurrentState(env, broker, keylet);

            {
                auto loanPayTx =
                    env.json(pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
                Number const amount{3074'745'058'823'529, -12};
                BEAST_EXPECT(to_string(amount) == "3074.745058823529");
                XRPAmount const payFee{
                    baseFee *
                    (amount / stateBefore.periodicPayment / kLoanPaymentsPerFeeIncrement + 1)};
                loanPayTx["Amount"]["value"] = to_string(amount);
                env(loanPayTx, Fee(payFee), Ter(tesSUCCESS));
                env.close();
            }

            {
                auto loanPayTx =
                    env.json(pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
                Number const amount{6732'118'170'944'051, -12};
                BEAST_EXPECT(to_string(amount) == "6732.118170944051");
                XRPAmount const payFee{
                    baseFee *
                    (amount / stateBefore.periodicPayment / kLoanPaymentsPerFeeIncrement + 1)};
                loanPayTx["Amount"]["value"] = to_string(amount);
                env(loanPayTx, Fee(payFee), Ter(tesSUCCESS));
                env.close();
            }

            auto const stateAfter = getCurrentState(env, broker, keylet);
            // Total interest outstanding is non-negative
            BEAST_EXPECT(stateAfter.totalValue >= stateAfter.principalOutstanding);
            // Principal paid is non-negative
            BEAST_EXPECT(stateBefore.principalOutstanding >= stateAfter.principalOutstanding);
            // Total value change is non-negative
            BEAST_EXPECT(stateBefore.totalValue >= stateAfter.totalValue);
            // Value delta is larger or same as principal delta (meaning
            // non-negative interest paid)
            BEAST_EXPECT(
                (stateBefore.totalValue - stateAfter.totalValue) >=
                (stateBefore.principalOutstanding - stateAfter.principalOutstanding));
        }

        // From FIND-008
        {
            testcase << "xrpl::loanComputePaymentParts : loanValueChange rounded";

            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            PrettyAsset const iouAsset =
                createFundedIouAsset(env, issuer, lender, borrower, 100'000'000, 10'000'000);

            BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender)};
            {
                auto const coverDepositValue =
                    broker.asset(broker.params.coverDeposit * 10).value();
                env(loanBroker::coverDeposit(lender, broker.brokerID, coverDepositValue));
                env.close();
            }

            using namespace loan;

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const principalRequest{1, 3};

            auto createJson = env.json(
                set(borrower, broker.brokerID, principalRequest),
                Fee(loanSetFee),
                Json(sfCounterpartySignature, json::ValueType::Object));

            createJson["ClosePaymentFee"] = "0";
            createJson["InterestRate"] = 12833;
            createJson["LateInterestRate"] = 77048;
            createJson["LatePaymentFee"] = "0";
            createJson["LoanOriginationFee"] = "218";
            createJson["LoanServiceFee"] = "0";
            createJson["PaymentInterval"] = 752;
            createJson["PaymentTotal"] = 5678;
            createJson["PrincipalRequested"] = "9924.81";

            auto const keylet = nextLoanKeylet(env, broker);

            createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
            env(createJson, Ter(tesSUCCESS));
            env.close();

            auto const baseFee = env.current()->fees().base;

            auto const stateBefore = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(stateBefore.paymentRemaining == 5678);
            BEAST_EXPECT(stateBefore.paymentRemaining > kLoanMaximumPaymentsPerTransaction);

            auto loanPayTx = env.json(pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
            Number const amount{9924'81, -2};
            BEAST_EXPECT(to_string(amount) == "9924.81");
            XRPAmount const payFee{
                baseFee *
                (amount / stateBefore.periodicPayment / kLoanPaymentsPerFeeIncrement + 1)};
            loanPayTx["Amount"]["value"] = to_string(amount);
            env(loanPayTx, Fee(payFee), Ter(tesSUCCESS));
            env.close();

            auto const stateAfter = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(
                stateAfter.paymentRemaining ==
                stateBefore.paymentRemaining - kLoanMaximumPaymentsPerTransaction);
        }
    }

    void
    testAccountSendMptMinAmountInvariant(FeatureBitset features)
    {
        // (From FIND-006)
        testcase << "LoanSet trigger xrpl::accountSendMPT : minimum amount "
                    "and MPT";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = lender});
        mptt.authorize({.account = borrower});
        env(pay(issuer, lender, mptAsset(2'000'000)));
        env(pay(issuer, borrower, mptAsset(1'000)));
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, mptAsset, lender)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            Fee(loanSetFee),
            Json(sfCounterpartySignature, json::ValueType::Object));

        createJson["CloseInterestRate"] = 76671;
        createJson["ClosePaymentFee"] = "2061925410";
        createJson["GracePeriod"] = 434;
        createJson["InterestRate"] = 50302;
        createJson["LateInterestRate"] = 30322;
        createJson["LatePaymentFee"] = "294427911";
        createJson["LoanOriginationFee"] = "3250635102";
        createJson["LoanServiceFee"] = "9557386";
        createJson["OverpaymentFee"] = 51249;
        createJson["OverpaymentInterestRate"] = 14304;
        createJson["PaymentInterval"] = 434;
        createJson["PaymentTotal"] = "2891743748";
        createJson["PrincipalRequested"] = "8516.98";

        auto const brokerStateBefore = env.le(keylet::loanBroker(broker.brokerID));

        createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
        env(createJson, Ter(temINVALID));
        env.close();
    }

    void
    testLoanPayDebtDecreaseInvariant(FeatureBitset features)
    {
        // From FIND-007
        testcase << "LoanPay xrpl::LoanPay::doApply : debtDecrease "
                    "rounding good";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        PrettyAsset const iouAsset = createFundedIouAsset(env, issuer, lender, borrower);

        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender)};

        using namespace loan;

        auto const baseFee = env.current()->fees().base;
        auto const loanSetFee = Fee(baseFee * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            Fee(loanSetFee),
            Json(sfCounterpartySignature, json::ValueType::Object));

        createJson["ClosePaymentFee"] = "0";
        createJson["GracePeriod"] = 60;
        createJson["InterestRate"] = 24346;
        createJson["LateInterestRate"] = 65535;
        createJson["LatePaymentFee"] = "0";
        createJson["LoanOriginationFee"] = "218";
        createJson["LoanServiceFee"] = "0";
        createJson["PaymentInterval"] = 60;
        createJson["PaymentTotal"] = 5678;
        createJson["PrincipalRequested"] = "9924.81";

        auto const keylet = nextLoanKeylet(env, broker);

        createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
        env(createJson, Ter(tesSUCCESS));
        env.close();

        auto const pseudoAcct = brokerPseudoAccount(env, broker, lender);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, keylet);
        auto const originalState = getCurrentState(env, broker, keylet);
        verifyLoanStatus(originalState);

        Number const payment{3'269'349'176'470'588, -12};
        XRPAmount const payFee{
            baseFee *
            ((payment / originalState.periodicPayment) / kLoanPaymentsPerFeeIncrement + 1)};
        auto loanPayTx =
            env.json(pay(borrower, keylet.key, STAmount{broker.asset, payment}), Fee(payFee));
        BEAST_EXPECT(to_string(payment) == "3269.349176470588");
        env(loanPayTx, Ter(tesSUCCESS));
        env.close();

        auto const newState = getCurrentState(env, broker, keylet);
        BEAST_EXPECT(
            isRounded(broker.asset, newState.managementFeeOutstanding, originalState.loanScale));
        BEAST_EXPECT(newState.managementFeeOutstanding < originalState.managementFeeOutstanding);
        BEAST_EXPECT(isRounded(broker.asset, newState.totalValue, originalState.loanScale));
        BEAST_EXPECT(
            isRounded(broker.asset, newState.principalOutstanding, originalState.loanScale));
    }

    void
    testDosLoanPay(FeatureBitset features)
    {
        bool const feeCapped = features[fixCleanup3_1_3];

        // From FIND-005
        testcase << "DoS LoanPay: fee calculation " << (feeCapped ? "capped" : "uncapped");

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
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

    void
    testLoanNextPaymentDueDateOverflow(FeatureBitset features)
    {
        // For FIND-013
        testcase << "Prevent nextPaymentDueDate overflow";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
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
            // Intentionally shadow the outer values
            auto const loanSequence = brokerState->at(sfLoanSequence);
            auto const keylet = keylet::loan(broker.brokerID, loanSequence);

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
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

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

    void
    testLoanCoverMinimumRoundingExploit(FeatureBitset features)
    {
        auto testLoanCoverMinimumRoundingExploit = [&, this](Number const& principalRequest) {
            testcase << "LoanBrokerCoverClawback drains cover via rounding"
                     << " principalRequested=" << to_string(principalRequest);

            using namespace jtx;
            using namespace loan;
            using namespace loanBroker;

            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            env.fund(XRP(1'000'000'000), issuer, lender, borrower);
            env.close();

            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const asset = issuer[iouCurrency];
            env(trust(lender, asset(2'000'0000)));
            env(trust(borrower, asset(2'000'0000)));
            env.close();

            env(pay(issuer, lender, asset(2'000'0000)));
            env.close();

            BrokerParameters brokerParams{.debtMax = 0, .coverRateMin = TenthBips32{10'000}};
            BrokerInfo broker{createVaultAndBroker(env, asset, lender, brokerParams)};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            auto createTx = env.jt(
                set(borrower, broker.brokerID, principalRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                kPaymentInterval(600),
                kPaymentTotal(1),
                kGracePeriod(60));
            env(createTx);
            env.close();

            auto const brokerBefore = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerBefore);
            if (!brokerBefore)
                return;

            Number const debtOutstanding = brokerBefore->at(sfDebtTotal);
            Number const coverAvailableBefore = brokerBefore->at(sfCoverAvailable);

            BEAST_EXPECT(debtOutstanding > Number{});
            BEAST_EXPECT(coverAvailableBefore > Number{});

            log << "debt=" << to_string(debtOutstanding)
                << " cover_available=" << to_string(coverAvailableBefore);

            env(coverClawback(issuer, 0), loanBrokerID(broker.brokerID));
            env.close();

            auto const brokerAfter = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerAfter);
            if (!brokerAfter)
                return;

            Number const debtAfter = brokerAfter->at(sfDebtTotal);
            // the debt has not changed
            BEAST_EXPECT(debtAfter == debtOutstanding);

            Number const coverAvailableAfter = brokerAfter->at(sfCoverAvailable);

            // since the cover rate min != 0, the cover available should not
            // be zero
            BEAST_EXPECT(coverAvailableAfter != Number{});
        };

        // Call the lambda with different principal values
        testLoanCoverMinimumRoundingExploit(Number{1, -30});  // 1e-30 units
        testLoanCoverMinimumRoundingExploit(Number{1, -20});  // 1e-20 units
        testLoanCoverMinimumRoundingExploit(Number{1, -10});  // 1e-10 units
        testLoanCoverMinimumRoundingExploit(Number{1, 1});    // 1e-10 units
    }
#endif

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
    testDustManipulation(FeatureBitset features)
    {
        testcase("Dust manipulation");

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env{*this, features};

        // Setup: Create accounts
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        Account const victim{"victim"};

        env.fund(XRP(1'000'000'00), issuer, lender, borrower, victim);
        env.close();

        // Step 1: Create vault with IOU asset
        auto asset = issuer["USD"];
        env(trust(lender, asset(100000)));
        env(trust(borrower, asset(100000)));
        env(trust(victim, asset(100000)));
        env(pay(issuer, lender, asset(50000)));
        env(pay(issuer, borrower, asset(50000)));
        env(pay(issuer, victim, asset(50000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 10000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{1000},
            .coverRateLiquidation = TenthBips32{2500}};

        auto broker = createVaultAndBroker(env, asset, lender, brokerParams);

        auto const loanKeyletOpt = [&]() -> std::optional<Keylet> {
            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return std::nullopt;

            // Broker has no loans
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);

            // The loan keylet is based on the LoanSequence of the
            // _LOAN_BROKER_ object.
            auto const loanSequence = brokerSle->at(sfLoanSequence);
            return keylet::loan(broker.brokerID, loanSequence);
        }();
        if (!loanKeyletOpt)
            return;

        auto const& vaultKeylet = broker.vaultKeylet();

        {
            auto const vaultSle = env.le(vaultKeylet);
            Number const assetsTotal = vaultSle->at(sfAssetsTotal);
            Number const assetsAvail = vaultSle->at(sfAssetsAvailable);

            log << "Before loan creation:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal << std::endl;
            log << "  AssetsAvailable: " << assetsAvail << std::endl;
            log << "  Difference: " << (assetsTotal - assetsAvail) << std::endl;

            // before the loan the assets total and available should be equal
            BEAST_EXPECT(assetsAvail == assetsTotal);
            BEAST_EXPECT(assetsAvail == broker.asset(brokerParams.vaultDeposit).number());
        }

        Keylet const& loanKeylet = *loanKeyletOpt;

        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{100},
            .interest = TenthBips32{1922},
            .payTotal = 5816,
            .payInterval = 86400 * 6,
            .gracePd = 86400 * 5,
        };

        env(loanParams(env, broker));
        env.close();

        // Wait for loan to be late enough to default
        env.close(std::chrono::seconds(86400 * 40));  // 40 days

        {
            auto const vaultSle = env.le(vaultKeylet);
            Number const assetsTotal = vaultSle->at(sfAssetsTotal);
            Number const assetsAvail = vaultSle->at(sfAssetsAvailable);

            log << "After loan creation:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal << std::endl;
            log << "  AssetsAvailable: " << assetsAvail << std::endl;
            log << "  Difference: " << (assetsTotal - assetsAvail) << std::endl;

            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            auto const state = constructLoanState(loanSle);

            log << "Loan state:" << std::endl;
            log << "  ValueOutstanding: " << state.valueOutstanding << std::endl;
            log << "  PrincipalOutstanding: " << state.principalOutstanding << std::endl;
            log << "  InterestOutstanding: " << state.interestOutstanding() << std::endl;
            log << "  InterestDue: " << state.interestDue << std::endl;
            log << "  FeeDue: " << state.managementFeeDue << std::endl;

            // after loan creation the assets total and available should
            // reflect the value of the loan
            BEAST_EXPECT(assetsAvail < assetsTotal);
            BEAST_EXPECT(
                assetsAvail ==
                broker.asset(brokerParams.vaultDeposit - loanParams.principalRequest).number());
            BEAST_EXPECT(
                assetsTotal ==
                broker.asset(brokerParams.vaultDeposit + state.interestDue).number());
        }

        // Step 7: Trigger default (dust adjustment will occur)
        env(jtx::loan::manage(lender, loanKeylet.key, tfLoanDefault));
        env.close();

        // Step 8: Verify phantom assets created
        {
            auto const vaultSle2 = env.le(vaultKeylet);
            Number const assetsTotal2 = vaultSle2->at(sfAssetsTotal);
            Number const assetsAvail2 = vaultSle2->at(sfAssetsAvailable);

            log << "After default:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal2 << std::endl;
            log << "  AssetsAvailable: " << assetsAvail2 << std::endl;
            log << "  Difference: " << (assetsTotal2 - assetsAvail2) << std::endl;

            // after a default the assets total and available should be equal
            BEAST_EXPECT(assetsAvail2 == assetsTotal2);
        }
    }

    void
    testRoundingAllowsUndercoverage(FeatureBitset features)
    {
        testcase("Minimum cover rounding allows undercoverage (XRP)");

        using namespace jtx;
        using namespace loanBroker;

        Env env{*this, features};

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(200'000), lender, borrower);
        env.close();

        // Vault with XRP asset
        Vault const vault{env};
        auto [vaultCreate, vaultKeylet] = vault.create({.owner = lender, .asset = xrpIssue()});
        env(vaultCreate);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        // Seed the vault with XRP so it can fund the loan principal
        PrettyAsset const xrpAsset{xrpIssue(), 1};

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{10'000},
            .coverDeposit = 82,
        };

        auto const brokerInfo = createVaultAndBroker(env, xrpAsset, lender, brokerParams);
        // Create a loan with principal 804 XRP and 0% interest (so
        // DebtTotal increases by exactly 804)
        env(loan::set(borrower, brokerInfo.brokerID, xrpAsset(804).value()),
            loan::kInterestRate(TenthBips32(0)),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2));
        BEAST_EXPECT(env.ter() == tesSUCCESS);
        env.close();

        // Verify DebtTotal is exactly 804
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            log << *brokerSle << std::endl;
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == Number(804));
        }

        // Attempt to withdraw 2 XRP to self, leaving 80 XRP CoverAvailable.
        // The minimum is 80.4 XRP, which rounds up to 81 XRP, so this fails.
        env(coverWithdraw(lender, brokerInfo.brokerID, xrpAsset(2).value()),
            Ter(tecINSUFFICIENT_FUNDS));
        BEAST_EXPECT(env.ter() == tecINSUFFICIENT_FUNDS);
        env.close();

        // Attempt to withdraw 1 XRP to self, leaving 81 XRP CoverAvailable.
        // because that leaves sufficient cover, this succeeds
        env(coverWithdraw(lender, brokerInfo.brokerID, xrpAsset(1).value()));
        BEAST_EXPECT(env.ter() == tesSUCCESS);
        env.close();

        // Validate CoverAvailable == 80 XRP and DebtTotal remains 804
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            log << *brokerSle << std::endl;
            BEAST_EXPECT(brokerSle->at(sfCoverAvailable) == xrpAsset(81).value());
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == Number(804));

            // Also demonstrate that the true minimum (804 * 10%) exceeds 80
            auto const theoreticalMin = tenthBipsOfValue(Number(804), TenthBips32(10'000));
            log << "Theoretical min cover: " << theoreticalMin << std::endl;
            BEAST_EXPECT(Number(804, -1) == theoreticalMin);
        }
    }

    void
    testSequentialFLCDepletion(FeatureBitset features)
    {
        testcase << "First-Loss Capital Depletion on Sequential Defaults";

        using namespace jtx;
        using namespace loan;
        using namespace loanBroker;

        Env env{*this, features};

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrowerA{"borrowerA"};
        Account const borrowerB{"borrowerB"};

        env.fund(XRP(1'000'000), issuer, lender, borrowerA, borrowerB);
        env.close();

        PrettyAsset const asset = xrpIssue();
        auto const vaultDepositAmount =
            asset(200'000);  // Enough for 2 x 50k loans plus interest/fees

        auto const brokerInfo = createVaultAndBroker(
            env,
            asset,
            lender,
            {
                .vaultDeposit = vaultDepositAmount.value(),
                .debtMax = 0,
                .coverRateMin = TenthBips32(20000),  // 20%
                .coverDeposit = 21'000,
                .managementFeeRate = TenthBips16(100),  // 0.1%
                .coverRateLiquidation = TenthBips32(100000),
            });
        auto const brokerKeylet = brokerInfo.brokerKeylet();

        // Create two identical loans: each 50,000 XRP principal (scaled down to
        // avoid funding issues) Total DebtTotal will be ~100,000 XRP (principal
        // + interest) Formula will calculate cover as: 100% × (20% × 100,000) =
        // 20,000 XRP So we need FLC = 20,000 XRP to be fully consumed by first
        // default
        auto const principalAmount = Number(50'000);
        auto const loanPaymentInterval = 2592000;  // 30 days
        auto const loanGracePeriod = 604800;       // 7 days

        // Create Loan A
        auto loanATx = env.jt(
            set(borrowerA, brokerKeylet.key, principalAmount),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32(500)),  // 5%
            kPaymentTotal(12),
            loan::kPaymentInterval(loanPaymentInterval),
            loan::kGracePeriod(loanGracePeriod),
            Fee(XRP(10)));  // Sufficient fee for multi-sig transaction
        env(loanATx);
        env.close();

        auto const loanAKeylet = keylet::loan(brokerKeylet.key, 1);

        // Create Loan B
        auto loanBTx = env.jt(
            set(borrowerB, brokerKeylet.key, principalAmount),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32(500)),  // 5%
            kPaymentTotal(12),
            loan::kPaymentInterval(loanPaymentInterval),
            loan::kGracePeriod(loanGracePeriod),
            Fee(XRP(10)));  // Sufficient fee for multi-sig transaction
        env(loanBTx);
        env.close();

        auto const loanBKeylet = keylet::loan(brokerKeylet.key, 2);

        auto loanASle = env.le(loanAKeylet);
        if (!BEAST_EXPECT(loanASle))
            return;

        // Advance time past grace period for both loans to be defaultable
        auto const loanANextDue = loanASle->at(sfNextPaymentDueDate);
        auto const loanAGrace = loanASle->at(sfGracePeriod);
        env.close(std::chrono::seconds{loanANextDue + loanAGrace + 60});

        env(manage(lender, loanAKeylet.key, tfLoanDefault), Ter(tesSUCCESS));
        env.close();

        // Verify Loan A is defaulted
        loanASle = env.le(loanAKeylet);
        if (!BEAST_EXPECT(loanASle))
            return;
        BEAST_EXPECT(loanASle->isFlag(lsfLoanDefault));
        BEAST_EXPECT(loanASle->at(sfPaymentRemaining) == 0);

        // Check broker state after first default (from committed ledger)
        auto brokerSle = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return;
        auto const afterFirstDebtTotal = brokerSle->at(sfDebtTotal);
        auto const afterFirstCoverAvailable = brokerSle->at(sfCoverAvailable);

        // DebtTotal should have decreased by Loan A's debt
        BEAST_EXPECT(afterFirstDebtTotal == 50'134);

        // CoverAvailable should have decreased significantly
        BEAST_EXPECT(afterFirstCoverAvailable == 946);

        env(manage(lender, loanBKeylet.key, tfLoanDefault), Ter(tesSUCCESS));

        brokerSle = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return;
        auto const afterSecondDebtTotal = brokerSle->at(sfDebtTotal);
        auto const afterSecondCoverAvailable = brokerSle->at(sfCoverAvailable);

        BEAST_EXPECT(afterSecondDebtTotal == 0);

        BEAST_EXPECT(afterSecondCoverAvailable == 0);
    }

    void
    testYieldTheftRounding(std::uint32_t flags)
    {
        testcase("Rounding manipulation does not permit yield theft");
        using namespace jtx;
        using namespace loan;

        // 1. Setup Environment
        Env env(*this, all_);
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1000), issuer, lender, borrower);
        env.close();

        // 2. Asset Selection
        PrettyAsset const iou = issuer["USD"];
        env(trust(lender, iou(100'000'000)));
        env(trust(borrower, iou(100'000'000)));
        env(pay(issuer, lender, iou(100'000'000)));
        env(pay(issuer, borrower, iou(100'000'000)));
        env.close();

        // 3. Create Vault and Broker with High Debt Limit (100M)
        auto const brokerInfo = createVaultAndBroker(
            env,
            iou,
            lender,
            {
                .vaultDeposit = 5'000'000,
                .debtMax = Number{100'000'000},
                .coverDeposit = 500'000,
            });
        auto const [currentSeq, vaultKeylet] = [&]() {
            auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return std::make_tuple(0u, keylet::unchecked(beast::kZero));
            auto const currentSeq = brokerSle->at(sfLoanSequence);
            auto const vaultKeylet = keylet::vault(brokerSle->at(sfVaultID));
            return std::make_tuple(currentSeq, vaultKeylet);
        }();

        // 4. Loan Parameters (Attack Vector)
        Number const principal = 1'000'000;
        TenthBips32 const interestRate = TenthBips32{1};  // 0.001%
        std::uint32_t const paymentInterval = 86400;
        std::uint32_t const paymentTotal = 3650;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        env(set(borrower, brokerInfo.brokerID, iou(principal).value(), flags),
            Sig(sfCounterpartySignature, lender),
            loan::kInterestRate(interestRate),
            loan::kPaymentInterval(paymentInterval),
            loan::kPaymentTotal(paymentTotal),
            Fee(loanSetFee));
        env.close();

        // --- RETRIEVE OBJECTS & SETUP ATTACK ---

        auto borrowerBalance = [&]() { return env.balance(borrower, iou); };
        auto const borrowerScale = static_cast<STAmount const&>(borrowerBalance()).exponent();

        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, currentSeq);
        auto const maybePeriodicPayment = [&]() -> std::optional<STAmount> {
            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return std::nullopt;
            // Construct Payment
            return STAmount{iou, loanSle->at(sfPeriodicPayment)};
        }();
        if (!maybePeriodicPayment)
            return;
        auto const periodicPayment = *maybePeriodicPayment;
        auto const roundedPayment =
            roundToScale(periodicPayment, borrowerScale, Number::RoundingMode::Upward);

        // ATTACK: Add dust buffer (1e-9) to force 'excess' logic execution
        STAmount const paymentBuffer{iou, Number(1, -9)};
        STAmount const attackPayment = periodicPayment + paymentBuffer;

        auto const maybeInitialVaultAssets = [&]() -> std::optional<Number> {
            auto const vault = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vault))
                return std::nullopt;
            return vault->at(sfAssetsTotal);
        }();
        if (!maybeInitialVaultAssets)
            return;
        auto const initialVaultAssets = *maybeInitialVaultAssets;

        // 5. Execution Loop
        int yieldTheftCount = 0;
        auto previousAssetsTotal = initialVaultAssets;

        for (int i = 0; i < 100; ++i)
        {
            auto const balanceBefore = borrowerBalance();
            env(pay(borrower, loanKeylet.key, attackPayment, flags));
            env.close();
            auto const borrowerDelta = balanceBefore - borrowerBalance();
            BEAST_EXPECT(borrowerDelta.signum() == roundedPayment.signum());

            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                break;
            auto const updatedPayment = STAmount{iou, loanSle->at(sfPeriodicPayment)};
            BEAST_EXPECT(
                (roundToScale(updatedPayment, borrowerScale, Number::RoundingMode::Upward) ==
                 roundedPayment));
            BEAST_EXPECT(
                (updatedPayment == periodicPayment) ||
                (flags == tfLoanOverpayment && i >= 2 && updatedPayment < periodicPayment));

            auto const currentVaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(currentVaultSle))
                break;

            auto const currentAssetsTotal = currentVaultSle->at(sfAssetsTotal);
            auto const delta = currentAssetsTotal - previousAssetsTotal;

            BEAST_EXPECT(
                (delta == beast::kZero && borrowerDelta <= roundedPayment) ||
                (delta > beast::kZero && borrowerDelta > roundedPayment));

            // If tx succeeded but Assets Total didn't change, interest was
            // stolen.
            if (delta == beast::kZero && borrowerDelta > roundedPayment)
            {
                yieldTheftCount++;
            }

            previousAssetsTotal = currentAssetsTotal;
        }

        BEAST_EXPECTS(yieldTheftCount == 0, std::to_string(yieldTheftCount));
    }

    // Tests that vault withdrawals work correctly when the vault has unrealized
    // loss from an impaired loan, ensuring the invariant check properly
    // accounts for the loss.
    void
    testWithdrawReflectsUnrealizedLoss(FeatureBitset features)
    {
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        testcase("Vault withdraw reflects sfLossUnrealized");

        // Test constants
        static constexpr std::int64_t kInitialFunding = 1'000'000;
        static constexpr std::int64_t kLenderInitialIou = 5'000'000;
        static constexpr std::int64_t kDepositorInitialIou = 1'000'000;
        static constexpr std::int64_t kBorrowerInitialIou = 100'000;
        static constexpr std::int64_t kDepositAmount = 5'000;
        static constexpr std::int64_t kPrincipalAmount = 99;
        static constexpr std::uint64_t kExpectedSharesPerDepositor = 5'000'000'000;
        static constexpr std::uint32_t kLocalPaymentInterval = 600;
        static constexpr std::uint32_t kLocalPaymentTotal = 2;

        Env env{*this, features};

        // Setup accounts
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const depositorA{"lpA"};
        Account const depositorB{"lpB"};
        Account const borrower{"borrowerA"};

        env.fund(XRP(kInitialFunding), issuer, lender, depositorA, depositorB, borrower);
        env.close();

        // Setup trust lines
        PrettyAsset const iouAsset = issuer[iouCurrency_];
        env(trust(lender, iouAsset(10'000'000)));
        env(trust(depositorA, iouAsset(10'000'000)));
        env(trust(depositorB, iouAsset(10'000'000)));
        env(trust(borrower, iouAsset(10'000'000)));
        env.close();

        // Fund accounts with IOUs
        env(pay(issuer, lender, iouAsset(kLenderInitialIou)));
        env(pay(issuer, depositorA, iouAsset(kDepositorInitialIou)));
        env(pay(issuer, depositorB, iouAsset(kDepositorInitialIou)));
        env(pay(issuer, borrower, iouAsset(kBorrowerInitialIou)));
        env.close();

        // Create vault and broker, then add deposits from two depositors
        auto const broker = createVaultAndBroker(env, iouAsset, lender);
        Vault v{env};

        env(v.deposit({
                .depositor = depositorA,
                .id = broker.vaultKeylet().key,
                .amount = iouAsset(kDepositAmount),
            }),
            Ter(tesSUCCESS));
        env(v.deposit({
                .depositor = depositorB,
                .id = broker.vaultKeylet().key,
                .amount = iouAsset(kDepositAmount),
            }),
            Ter(tesSUCCESS));
        env.close();

        // Create a loan
        auto const sleBroker = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(sleBroker))
            return;

        auto const loanKeylet = keylet::loan(broker.brokerID, sleBroker->at(sfLoanSequence));

        env(set(borrower, broker.brokerID, kPrincipalAmount),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(kLocalPaymentTotal),
            kPaymentInterval(kLocalPaymentInterval),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        // Impair the loan to create unrealized loss
        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tesSUCCESS));
        env.close();

        // Verify unrealized loss is recorded in the vault
        auto const vaultAfterImpair = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultAfterImpair))
            return;

        BEAST_EXPECT(
            vaultAfterImpair->at(sfLossUnrealized) == broker.asset(kPrincipalAmount).value());

        // Helper to get share balance for a depositor
        auto const shareAsset = vaultAfterImpair->at(sfShareMPTID);
        auto const getShareBalance = [&](Account const& depositor) -> std::uint64_t {
            auto const token = env.le(keylet::mptoken(shareAsset, depositor.id()));
            return token ? token->getFieldU64(sfMPTAmount) : 0;
        };

        // Verify both depositors have equal shares
        auto const sharesLpA = getShareBalance(depositorA);
        auto const sharesLpB = getShareBalance(depositorB);
        BEAST_EXPECT(sharesLpA == kExpectedSharesPerDepositor);
        BEAST_EXPECT(sharesLpB == kExpectedSharesPerDepositor);
        BEAST_EXPECT(sharesLpA == sharesLpB);

        // Helper to attempt withdrawal
        auto const attemptWithdrawShares = [&](Account const& depositor,
                                               std::uint64_t shareAmount,
                                               TER expected) {
            STAmount const shareAmt{MPTIssue{shareAsset}, Number(shareAmount)};
            env(v.withdraw(
                    {.depositor = depositor, .id = broker.vaultKeylet().key, .amount = shareAmt}),
                Ter(expected));
            env.close();
        };

        // Regression test: Both depositors should successfully withdraw despite
        // unrealized loss. Previously failed with invariant violation:
        // "withdrawal must change vault and destination balance by equal
        // amount". This was caused by sharesToAssetsWithdraw rounding down,
        // creating a mismatch where vaultDeltaAssets * -1 != destinationDelta
        // when unrealized loss exists.
        attemptWithdrawShares(depositorA, sharesLpA, tesSUCCESS);
        attemptWithdrawShares(depositorB, sharesLpB, tesSUCCESS);
    }

    void
    runAmendmentIndependent()
    {
        for (auto const flags : {0u, tfLoanOverpayment})
            testYieldTheftRounding(flags);
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
#if LOAN_TODO
        testLoanPayLateFullPaymentBypassesPenalties(features);
        testLoanCoverMinimumRoundingExploit(features);
#endif
        testDosLoanPay(features);
        testWithdrawReflectsUnrealizedLoss(features);
        testPoCUnsignedUnderflowOnFullPayAfterEarlyPeriodic(features);
        testLoanNextPaymentDueDateOverflow(features);
        testSequentialFLCDepletion(features);
        testLoanPayComputePeriodicPaymentInvariants(features);
        testAccountSendMptMinAmountInvariant(features);
        testLoanPayDebtDecreaseInvariant(features);
        testDustManipulation(features);
        testRoundingAllowsUndercoverage(features);
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

BEAST_DEFINE_TESTSUITE(LoanInvariant, tx, xrpl);

}  // namespace xrpl::test
