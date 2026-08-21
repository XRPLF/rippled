#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <optional>

namespace xrpl::test {

class LoanInvariants_test : public LoanTestBase
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
        using namespace lending;

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
                env(loan_broker::coverDeposit(lender, broker.brokerID, coverDepositValue));
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
    testLoanPayDebtDecreaseInvariant(FeatureBitset features)
    {
        // From FIND-007
        testcase << "LoanPay xrpl::LoanPay::doApply : debtDecrease "
                    "rounding good";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace lending;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        PrettyAsset const iouAsset = createFundedIouAsset(env, issuer, lender, borrower);

        BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender)};

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

    // Reproduces the scenario raised in PR #7732 review: the §3.11.5
    // non-full-payment invariant asserts that a successful LoanPay strictly
    // decreases PaymentRemaining and advances NextPaymentDueDate. doPayment
    // deliberately leaves those schedule fields unchanged for
    // PaymentSpecialCase::Extra (an overpayment), so the concern is that an
    // "extra-only" overpayment - one that does not also cover a scheduled
    // payment - would reach that branch and trip the invariant.
    //
    // The invariant is gated behind featureLendingProtocolV1_1, which
    // LoanTestBase::all_ excludes, so it is opted back in here.
    void
    testLoanPayOverpaymentScheduleInvariant(FeatureBitset features)
    {
        testcase("LoanPay overpayment vs non-full-payment invariant");

        using namespace jtx;
        using namespace loan;

        Env env{*this, features | featureLendingProtocolV1_1};

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(10'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1000};

        BrokerInfo const broker = createVaultAndBroker(
            env,
            asset,
            lender,
            {
                .vaultDeposit = asset(100'000).value(),
                .managementFeeRate = TenthBips16(10'000),
            });

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        // Principal 10,000 over 3 payments, overpayment enabled. One scheduled
        // payment is ~3,333, so an amount well below that cannot cover one.
        auto const loanKeylet = nextLoanKeylet(env, broker);
        env(loan::set(borrower, broker.brokerID, asset(10'000).value(), tfLoanOverpayment),
            Sig(sfCounterpartySignature, lender),
            loan::kPaymentInterval(86400 * 30),
            loan::kPaymentTotal(3),
            loan::kOverpaymentInterestRate(TenthBips32(percentageToTenthBips(20))),
            loanSetFee);
        env.close();

        auto const before = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(before.paymentRemaining == 3);

        STAmount const belowOnePayment = asset(1'000).value();
        BEAST_EXPECT((belowOnePayment < STAmount{asset, before.periodicPayment}));

        auto const payFee = Fee(env.current()->fees().base * 2);

        // Scenario A - the reviewer's "extra-only" overpayment. The amount does
        // not cover a scheduled payment, so makeRegularPayment makes zero
        // scheduled payments and returns tecINSUFFICIENT_PAYMENT before the
        // Extra branch runs. The invariant is therefore never reached. Were the
        // payment to succeed while touching only principal, the invariant would
        // fire instead.
        env(pay(borrower, loanKeylet.key, belowOnePayment, tfLoanOverpayment),
            payFee,
            Ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        auto const afterReject = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(afterReject.paymentRemaining == before.paymentRemaining);
        BEAST_EXPECT(afterReject.principalOutstanding == before.principalOutstanding);
        BEAST_EXPECT(afterReject.nextPaymentDate == before.nextPaymentDate);

        // Scenario B - a valid overpayment that also covers one scheduled
        // payment. This reaches the §3.11.5 invariant with tesSUCCESS:
        // PaymentRemaining drops by one, NextPaymentDueDate advances by one
        // interval, and PrincipalOutstanding strictly decreases (by more than a
        // plain payment thanks to the extra). The invariant must accept it.
        STAmount const onePaymentPlusExtra = asset(5'000).value();
        env(pay(borrower, loanKeylet.key, onePaymentPlusExtra, tfLoanOverpayment), payFee);
        env.close();

        auto const afterPay = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(afterPay.paymentRemaining == before.paymentRemaining - 1);
        BEAST_EXPECT(afterPay.principalOutstanding < before.principalOutstanding);
        BEAST_EXPECT(afterPay.nextPaymentDate == before.nextPaymentDate + before.paymentInterval);
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

        createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
        env(createJson, Ter(temINVALID));
        env.close();
    }

    // Verify that LoanPay, LoanBrokerCoverWithdraw, and LoanSet all use the
    // same vault-scale minimum cover when fixCleanup3_2_0 is enabled.
    // Before the amendment, each transactor computed its minimum cover at a
    // different precision (loanScale, debtScale, or the raw unrounded
    // tenthBipsOfValue), which could lead to inconsistent decisions for the
    // same broker state.  After the amendment all three use
    // minimumBrokerCover at vaultScale.
    void
    testMinimumBrokerCoverConsistency(FeatureBitset features)
    {
        using namespace jtx;
        using namespace loan;
        using namespace loan_broker;

        bool const withAmendment = features[fixCleanup3_2_0];

        struct Ctx
        {
            jtx::Account issuer;
            jtx::Account lender;
            jtx::Account borrower;
            jtx::PrettyAsset iou;
            BrokerInfo broker;
            BrokerParameters brokerParams;
        };

        // Shared setup, parametrized by vaultDeposit (the only varying setup
        // field across the three scenarios).  Each call runs in its own Env
        // so multiple invocations within one scenario cannot interfere.
        // The caller is responsible for invoking testcase(...) before the
        // first runTest call of each scenario.
        auto runTest = [&](Number vaultDeposit, auto&& body) {
            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            env.fund(XRP(1'000'000'000), issuer, lender, borrower);
            env.close();

            // Enable clawback on the issuer *before* any trust lines exist
            // (asfAllowTrustLineClawback requires an empty owner directory).
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const iou = issuer[iouCurrency_];
            env(trust(lender, iou(1'000'000'000)));
            env(trust(borrower, iou(1'000'000'000)));
            env.close();
            env(pay(issuer, lender, iou(100'000'000)));
            env(pay(issuer, borrower, iou(100'000'000)));
            env.close();

            // 13.37% — non-round rate produces a messier minimum.
            BrokerParameters const brokerParams{
                .vaultDeposit = vaultDeposit,
                .debtMax = 0,
                .coverRateMin = TenthBips32{13'370},
                .coverDeposit = 5'000,
                .managementFeeRate = TenthBips16{500}};

            BrokerInfo const broker = createVaultAndBroker(env, iou, lender, brokerParams);

            body(
                env,
                Ctx{.issuer = issuer,
                    .lender = lender,
                    .borrower = borrower,
                    .iou = iou,
                    .broker = broker,
                    .brokerParams = brokerParams});
        };

        // Scenario 1 — LoanPay
        //
        // Verify that LoanPay's minimum cover check uses vault scale (not
        // loan scale).  Before the amendment, different loans could produce
        // different fee routing decisions for the same broker-level state.
        // Small vault deposit => vaultScale = -12.
        testcase("LoanPay minimum cover scale consistency");
        {
            struct LoanKeylets
            {
                Keylet tiny;
                Keylet big;
            };

            // Create the tiny + big loans and reduce cover via clawback so
            // that subsequent LoanPay calls hit the minimum-cover boundary.
            // Used by the two pay-and-check sub-tests below so each can run
            // in its own Env.
            auto setupLoansAndClawback = [&](Env& env, Ctx const& c) -> std::optional<LoanKeylets> {
                Asset const asset{c.iou};

                // Create the TINY loan first (while vaultScale is still
                // small).  principal 0.01, 0% interest, 1 payment =>
                // loanScale = vaultScale.
                auto const brokerSle1 = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle1))
                    return std::nullopt;
                auto const tinyLoanSeq = brokerSle1->at(sfLoanSequence);
                auto const tinyLoanKeylet =
                    keylet::loan(c.broker.brokerID, SeqProxy::rawSequence(tinyLoanSeq));

                env(set(c.borrower, c.broker.brokerID, Number{1, -2}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{0}),
                    kPaymentTotal(1),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // Create the BIG loan second.  100% annual interest over 20
                // payments pushes totalValueOutstanding high enough that
                // loanScale > vaultScale.
                auto const brokerSle2 = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle2))
                    return std::nullopt;
                auto const bigLoanSeq = brokerSle2->at(sfLoanSequence);
                auto const bigLoanKeylet =
                    keylet::loan(c.broker.brokerID, SeqProxy::rawSequence(bigLoanSeq));

                env(set(c.borrower, c.broker.brokerID, Number{500}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{100'000}),
                    kPaymentTotal(20),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // The tiny loan's scale is frozen at the vault's pre-big-loan
                // scale, so it is strictly smaller than the big loan's.
                // After the big loan is created the vault absorbs its value,
                // pushing vaultScale up to match bigLoanScale.
                auto const tinyLoanSle = env.le(tinyLoanKeylet);
                auto const bigLoanSle = env.le(bigLoanKeylet);
                auto const vaultSle = env.le(keylet::vault(c.broker.vaultID));
                if (!BEAST_EXPECT(tinyLoanSle) || !BEAST_EXPECT(bigLoanSle) ||
                    !BEAST_EXPECT(vaultSle))
                    return std::nullopt;
                if (!BEAST_EXPECT(tinyLoanSle->at(sfLoanScale) == -12) ||
                    !BEAST_EXPECT(bigLoanSle->at(sfLoanScale) == -11) ||
                    !BEAST_EXPECT(getAssetsTotalScale(vaultSle) == -11))
                    return std::nullopt;

                // Use issuer clawback to reduce cover to the minimum the
                // clawback transactor allows.  Compute the amount as
                // initialCover - expectedCoverAfter so we exercise the exact
                // clawback rather than relying on the transactor to clip
                // down.
                //
                // Before the amendment the clawback minimum is the
                // *unrounded* tenthBipsOfValue — strictly less than the
                // rounded-at-vaultScale minimum LoanPay uses for the big
                // loan.  After the amendment both clawback and LoanPay use
                // the same rounded minimum (via minimumBrokerCover), so
                // cover lands exactly at that threshold.
                Number const expectedCoverAfter = withAmendment ? Number{1330651855688460000, -15}
                                                                : Number{1330651855688458000, -15};
                Number const clawbackAmount =
                    Number{c.brokerParams.coverDeposit} - expectedCoverAfter;

                env(coverClawback(c.issuer),
                    kLoanBrokerId(c.broker.brokerID),
                    kAmount(STAmount{asset, clawbackAmount}));
                env.close();

                auto const brokerSle = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle) ||
                    !BEAST_EXPECT(brokerSle->at(sfCoverAvailable) == expectedCoverAfter))
                    return std::nullopt;

                return LoanKeylets{.tiny = tinyLoanKeylet, .big = bigLoanKeylet};
            };

            // Pay one loan and report whether the fee went to the broker's
            // pseudo account (the fallback when cover < minimum) rather
            // than to the owner.
            auto feeGoesToPseudo = [&](Env& env, Ctx const& c, Keylet const& loanKeylet) -> bool {
                Asset const asset{c.iou};
                auto const brokerSle = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle))
                    return false;
                auto const pseudoAcct = Account("pseudo", brokerSle->at(sfAccount));
                auto const pseudoBefore = env.balance(pseudoAcct, c.iou);

                auto const payLoan = env.le(loanKeylet);
                if (!BEAST_EXPECT(payLoan))
                    return false;
                auto const periodicPayment = payLoan->at(sfPeriodicPayment);
                auto const serviceFee = payLoan->at(sfLoanServiceFee);
                std::int32_t const loanScale = payLoan->at(sfLoanScale);

                auto const payment = roundPeriodicPayment(asset, periodicPayment, loanScale);
                auto const payAmt = STAmount{asset, payment + serviceFee};

                env(loan::pay(c.borrower, loanKeylet.key, payAmt), Fee(XRP(10)));
                env.close();

                auto const pseudoAfter = env.balance(pseudoAcct, c.iou);
                return pseudoAfter.number() > pseudoBefore.number();
            };

            // Pay the BIG loan in its own Env so its outcome cannot affect
            // the TINY-loan check.  With the fix, LoanPay and clawback use
            // the same vaultScale minimum (cover == minAtVaultScale =>
            // fee to owner).  Without the fix, LoanPay uses bigLoanScale=-11,
            // rounds up to a larger minimum than what clawback used =>
            // cover < min => fee to pseudo.
            runTest(/*vaultDeposit=*/1'000, [&](Env& env, Ctx const& c) {
                auto const loans = setupLoansAndClawback(env, c);
                if (!loans)
                    return;
                BEAST_EXPECT(feeGoesToPseudo(env, c, loans->big) == !withAmendment);
            });

            // Pay the TINY loan in its own Env.  Fee goes to the owner
            // either way:
            //  - With the fix: LoanPay uses vaultScale=-11 (same as
            //    clawback) => owner.
            //  - Without the fix: LoanPay uses tinyLoanScale=-12, rounds
            //    up at -12 (a no-op) => min == cover => owner.
            runTest(/*vaultDeposit=*/1'000, [&](Env& env, Ctx const& c) {
                auto const loans = setupLoansAndClawback(env, c);
                if (!loans)
                    return;
                BEAST_EXPECT(!feeGoesToPseudo(env, c, loans->tiny));
            });
        }

        // Scenario 2 — LoanBrokerCoverWithdraw
        //
        // Verify that CoverWithdraw's minimum cover check uses vault scale
        // (not scale(debtTotal, asset)).  Before the amendment, CoverWithdraw
        // used:
        //   roundToAsset(asset, tenthBipsOfValue(debt, rate), scale(debt, asset))
        // which could disagree with LoanPay's minimum (which used loanScale).
        //
        // Use a large vault deposit so that vaultScale (from AssetsTotal) is
        // strictly larger than debtScale (from DebtTotal).  With
        // vaultDeposit = 100,000: after the big loan
        //   AssetsTotal ≈ 109,500 → vaultScale = -10
        //   DebtTotal   ≈  10,000 → debtScale  = -11
        // The one-order-of-magnitude gap makes roundToAsset at -10 truncate
        // more aggressively than at -11, exposing the bug.
        testcase("CoverWithdraw minimum cover scale consistency");
        runTest(
            /*vaultDeposit=*/100'000, [&](Env& env, Ctx const& c) {
                Asset const asset{c.iou};

                // Create only the big loan to push DebtTotal up to ~10,000
                // while AssetsTotal stays around 109,500 (dominated by the
                // large vault deposit).
                env(set(c.borrower, c.broker.brokerID, Number{500}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{100'000}),
                    kPaymentTotal(20),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // Read broker state and compute both old and new minimums.
                auto const brokerSle = env.le(keylet::loanBroker(c.broker.brokerID));
                auto const vaultSle = env.le(keylet::vault(c.broker.vaultID));
                if (!BEAST_EXPECT(brokerSle) || !BEAST_EXPECT(vaultSle))
                    return;

                auto const coverAvail = brokerSle->at(sfCoverAvailable);
                auto const debtTotal = brokerSle->at(sfDebtTotal);
                auto const vaultScale = getAssetsTotalScale(vaultSle);
                auto const debtScale = scale(debtTotal, asset);

                // Sanity: debt scale differs from vault scale for this setup.
                BEAST_EXPECT(debtScale < vaultScale);

                auto const oldMin = [&]() {
                    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
                    return roundToAsset(
                        asset,
                        tenthBipsOfValue(debtTotal, TenthBips32{c.brokerParams.coverRateMin}),
                        debtScale);
                }();
                auto const newMin = minimumBrokerCover(
                    debtTotal, TenthBips32{c.brokerParams.coverRateMin}, vaultSle);

                // The new (vaultScale) minimum must be strictly larger than
                // the old (debtScale) minimum — that is the gap the amendment
                // closes.
                Number const expectedNewMin{1330650518688500000, -15};
                Number const expectedOldMin{1330650518688472000, -15};
                BEAST_EXPECT(newMin == expectedNewMin);
                BEAST_EXPECT(oldMin == expectedOldMin);

                // Try to withdraw so that remaining cover lands between the
                // two minimums:  oldMin < target < newMin.
                auto const target = oldMin + (newMin - oldMin) / 2;
                auto const withdrawAmount = STAmount{asset, coverAvail - target};

                if (withAmendment)
                {
                    // CoverWithdraw now uses vaultScale: target < newMin
                    // => FAILS.
                    env(coverWithdraw(c.lender, c.broker.brokerID, withdrawAmount),
                        Ter(tecINSUFFICIENT_FUNDS));
                }
                else
                {
                    // Old CoverWithdraw uses debtScale: target > oldMin
                    // => SUCCEEDS.
                    env(coverWithdraw(c.lender, c.broker.brokerID, withdrawAmount));
                }
                env.close();
            });

        // Scenario 3 — LoanSet
        //
        // Verify that LoanSet's minimum cover check uses vault scale (not the
        // raw unrounded tenthBipsOfValue).  Before the amendment, LoanSet
        // used tenthBipsOfValue(newDebtTotal, coverRateMinimum) (no
        // roundToAsset), while clawback/withdraw used different formulas.
        // After the amendment all use minimumBrokerCover at vaultScale, and
        // rounding at a coarser scale can absorb a tiny debt increase —
        // allowing a loan that would otherwise be rejected.
        testcase("LoanSet minimum cover scale consistency");
        runTest(
            /*vaultDeposit=*/1'000, [&](Env& env, Ctx const& c) {
                // Create the tiny loan (scale -12) AND the big loan (scale
                // -11).  Both loans are needed so that DebtTotal has a full
                // 16-digit mantissa — a "messy" value where roundToAsset at
                // vaultScale actually truncates digits and produces a
                // different result from the raw tenthBipsOfValue.  With only
                // the big loan, DebtTotal has ~4 significant digits and
                // rounding at scale -11 is a no-op, masking the amendment's
                // effect.
                env(set(c.borrower, c.broker.brokerID, Number{1, -2}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{0}),
                    kPaymentTotal(1),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                env(set(c.borrower, c.broker.brokerID, Number{500}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{100'000}),
                    kPaymentTotal(20),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // Clawback to reduce cover to the clawback transactor's
                // minimum.  Pass the exact amount rather than relying on the
                // transactor to clip down; the setup matches Scenario 1 so
                // the same residual-cover values apply.
                Number const expectedCoverAfter = withAmendment ? Number{1330651855688460000, -15}
                                                                : Number{1330651855688458000, -15};
                Number const clawbackAmount =
                    Number{c.brokerParams.coverDeposit} - expectedCoverAfter;
                env(coverClawback(c.issuer),
                    kLoanBrokerId(c.broker.brokerID),
                    kAmount(c.iou(clawbackAmount)));
                env.close();

                // Verify scales.
                auto const vaultSle = env.le(keylet::vault(c.broker.vaultID));
                if (!BEAST_EXPECT(vaultSle))
                    return;
                auto const vaultScale = getAssetsTotalScale(vaultSle);
                BEAST_EXPECT(vaultScale == -11);

                // Now try to create a tiny additional loan.  Principal is
                // 1e-11 (the smallest value that survives the precision
                // check at loanScale = vaultScale = -11), with 0% interest
                // and 1 payment.
                //
                // The tiny debt increase adds ~1.337e-12 to the unrounded
                // minimum.
                // - Without the amendment: the old LoanSet formula rounds
                //   up during tenthBipsOfValue (16-digit Number
                //   normalisation), pushing the minimum past the cover left
                //   by clawback => tecINSUFFICIENT_FUNDS.
                // - With the amendment: minimumBrokerCover rounds at
                //   vaultScale=-11, which absorbs the tiny increase — the
                //   rounded minimum stays the same => tesSUCCESS.
                auto const tinyPrincipal = Number{1, -11};

                if (withAmendment)
                {
                    env(set(c.borrower, c.broker.brokerID, tinyPrincipal),
                        Sig(sfCounterpartySignature, c.lender),
                        kInterestRate(TenthBips32{0}),
                        kPaymentTotal(1),
                        kPaymentInterval(86400 * 365),
                        Fee(XRP(10)));
                }
                else
                {
                    env(set(c.borrower, c.broker.brokerID, tinyPrincipal),
                        Sig(sfCounterpartySignature, c.lender),
                        kInterestRate(TenthBips32{0}),
                        kPaymentTotal(1),
                        kPaymentInterval(86400 * 365),
                        Fee(XRP(10)),
                        Ter(tecINSUFFICIENT_FUNDS));
                }
                env.close();
            });
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testLoanPayComputePeriodicPaymentInvariants(features);
        testLoanPayDebtDecreaseInvariant(features);
        testLoanPayOverpaymentScheduleInvariant(features);
        testAccountSendMptMinAmountInvariant(features);
        testMinimumBrokerCoverConsistency(features);
    }

public:
    void
    run() override
    {
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2}, all_))
            runAmendmentSensitive(features);
    }
};

BEAST_DEFINE_TESTSUITE(LoanInvariants, tx, xrpl);

}  // namespace xrpl::test
