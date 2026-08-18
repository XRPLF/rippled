#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace xrpl::test {

class LoanMisc_test : public LoanTestBase
{
private:
    void
    testRPC(FeatureBitset features)
    {
        // This will expand as more test cases are added. Some functionality
        // is tested in other test functions.
        testcase("RPC");

        using namespace jtx;

        Env env(*this, features);

        auto lowerFee = [&]() {
            // Run the local fee back down.
            while (env.app().getFeeTrack().lowerLocalFee())
                ;
        };

        auto const baseFee = env.current()->fees().base;

        Account const alice{"alice"};
        std::string const borrowerPass = "borrower";
        Account const borrower{borrowerPass, KeyType::Ed25519};
        auto const lenderPass = "lender";
        Account const lender{lenderPass, KeyType::Ed25519};

        env.fund(XRP(1'000'000), alice, lender, borrower);
        env.close();
        env(noop(lender));
        env(noop(lender));
        env(noop(lender));
        env(noop(lender));
        env(noop(lender));
        env.close();

        {
            testcase("RPC AccountSet");
            json::Value txJson{json::ValueType::Object};
            txJson[sfTransactionType] = "AccountSet";
            txJson[sfAccount] = borrower.human();

            auto const signParams = [&]() {
                json::Value signParams{json::ValueType::Object};
                signParams[jss::passphrase] = borrowerPass;
                signParams[jss::key_type] = "ed25519";
                signParams[jss::tx_json] = txJson;
                return signParams;
            }();
            auto const jSign = env.rpc("json", "sign", to_string(signParams));
            BEAST_EXPECT(jSign.isMember(jss::result) && jSign[jss::result].isMember(jss::tx_json));
            auto txSignResult = jSign[jss::result][jss::tx_json];
            auto txSignBlob = jSign[jss::result][jss::tx_blob].asString();
            txSignResult.removeMember(jss::hash);

            auto const jtx = env.jt(txJson, Sig(borrower));
            BEAST_EXPECT(txSignResult == jtx.jv);

            lowerFee();
            auto const jSubmit = env.rpc("submit", txSignBlob);
            BEAST_EXPECT(
                jSubmit.isMember(jss::result) &&
                jSubmit[jss::result].isMember(jss::engine_result) &&
                jSubmit[jss::result][jss::engine_result].asString() == "tesSUCCESS");

            lowerFee();
            env(jtx.jv, Sig(kNone), Seq(kNone), Fee(kNone), Ter(tefPAST_SEQ));
        }

        {
            testcase("RPC LoanSet - illegal signature_target");

            json::Value txJson{json::ValueType::Object};
            txJson[sfTransactionType] = "AccountSet";
            txJson[sfAccount] = borrower.human();

            auto const borrowerSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "Destination";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignBorrower = env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::error) &&
                jSignBorrower[jss::result][jss::error] == "invalidParams" &&
                jSignBorrower[jss::result].isMember(jss::error_message) &&
                jSignBorrower[jss::result][jss::error_message] == "Destination");
        }
        {
            testcase("RPC LoanSet - sign and submit borrower initiated");
            // 1. Borrower creates the transaction
            json::Value txJson{json::ValueType::Object};
            txJson[sfTransactionType] = "LoanSet";
            txJson[sfAccount] = borrower.human();
            txJson[sfCounterparty] = lender.human();
            txJson[sfLoanBrokerID] =
                "FF924CD18A236C2B49CF8E80A351CEAC6A10171DC9F110025646894FEC"
                "F83F"
                "5C";
            txJson[sfPrincipalRequested] = "100000000";
            txJson[sfPaymentTotal] = 10000;
            txJson[sfPaymentInterval] = 3600;
            txJson[sfGracePeriod] = 300;
            txJson[sfFlags] = 65536;  // tfLoanOverpayment
            txJson[sfFee] = to_string(24 * baseFee / 10);

            // 2. Borrower signs the transaction
            auto const borrowerSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignBorrower = env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECTS(
                jSignBorrower.isMember(jss::result) &&
                    jSignBorrower[jss::result].isMember(jss::tx_json),
                to_string(jSignBorrower));
            auto const txBorrowerSignResult = jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob = jSignBorrower[jss::result][jss::tx_blob].asString();

            // 2a. Borrower attempts to submit the transaction. It doesn't
            // work
            {
                lowerFee();
                auto const jSubmitBlob = env.rpc("submit", txBorrowerSignBlob);
                BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
                auto const jSubmitBlobResult = jSubmitBlob[jss::result];
                BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
                // Transaction fails because the CounterpartySignature is
                // missing
                BEAST_EXPECT(
                    jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() ==
                        (env.enabled(featureLendingProtocolV1_1) ? "temINVALID" : "temBAD_SIGNER"));
            }

            // 3. Borrower sends the signed transaction to the lender
            // 4. Lender signs the transaction
            auto const lenderSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = lenderPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "CounterpartySignature";
                params[jss::tx_json] = txBorrowerSignResult;
                return params;
            }();
            auto const jSignLender = env.rpc("json", "sign", to_string(lenderSignParams));
            BEAST_EXPECT(
                jSignLender.isMember(jss::result) &&
                jSignLender[jss::result].isMember(jss::tx_json));
            auto const txLenderSignResult = jSignLender[jss::result][jss::tx_json];
            auto const txLenderSignBlob = jSignLender[jss::result][jss::tx_blob].asString();

            // 5. Lender submits the signed transaction blob
            lowerFee();
            auto const jSubmitBlob = env.rpc("submit", txLenderSignBlob);
            BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
            auto const jSubmitBlobResult = jSubmitBlob[jss::result];
            BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
            auto const jSubmitBlobTx = jSubmitBlobResult[jss::tx_json];
            // To get far enough to return tecNO_ENTRY means that the
            // signatures all validated. Of course the transaction won't
            // succeed because no Vault or Broker were created.
            BEAST_EXPECTS(
                jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() == "tecNO_ENTRY",
                to_string(jSubmitBlobResult));

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) && !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Lender submits the transaction json originally
            // received from the Borrower. It gets signed, but is now a
            // duplicate, so fails. Borrower could done this instead of
            // steps 4 and 5.
            lowerFee();
            auto const jSubmitJson = env.rpc("json", "submit", to_string(lenderSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECTS(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                    jSubmitJsonResult[jss::engine_result].asString() == "tefPAST_SEQ",
                to_string(jSubmitJsonResult));

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) && !jSubmitJsonResult.isMember(jss::error));

            BEAST_EXPECT(jSubmitBlobTx == jSubmitJsonTx);
        }

        {
            testcase("RPC LoanSet - sign and submit lender initiated");
            // 1. Lender creates the transaction
            json::Value txJson{json::ValueType::Object};
            txJson[sfTransactionType] = "LoanSet";
            txJson[sfAccount] = lender.human();
            txJson[sfCounterparty] = borrower.human();
            txJson[sfLoanBrokerID] =
                "FF924CD18A236C2B49CF8E80A351CEAC6A10171DC9F110025646894FEC"
                "F83F"
                "5C";
            txJson[sfPrincipalRequested] = "100000000";
            txJson[sfPaymentTotal] = 10000;
            txJson[sfPaymentInterval] = 3600;
            txJson[sfGracePeriod] = 300;
            txJson[sfFlags] = 65536;  // tfLoanOverpayment
            txJson[sfFee] = to_string(24 * baseFee / 10);

            // 2. Lender signs the transaction
            auto const lenderSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = lenderPass;
                params[jss::key_type] = "ed25519";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignLender = env.rpc("json", "sign", to_string(lenderSignParams));
            BEAST_EXPECT(
                jSignLender.isMember(jss::result) &&
                jSignLender[jss::result].isMember(jss::tx_json));
            auto const txLenderSignResult = jSignLender[jss::result][jss::tx_json];
            auto const txLenderSignBlob = jSignLender[jss::result][jss::tx_blob].asString();

            // 2a. Lender attempts to submit the transaction. It doesn't
            // work
            {
                lowerFee();
                auto const jSubmitBlob = env.rpc("submit", txLenderSignBlob);
                BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
                auto const jSubmitBlobResult = jSubmitBlob[jss::result];
                BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
                // Transaction fails because the CounterpartySignature is
                // missing
                BEAST_EXPECT(
                    jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() ==
                        (env.enabled(featureLendingProtocolV1_1) ? "temINVALID" : "temBAD_SIGNER"));
            }

            // 3. Lender sends the signed transaction to the Borrower
            // 4. Borrower signs the transaction
            auto const borrowerSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "CounterpartySignature";
                params[jss::tx_json] = txLenderSignResult;
                return params;
            }();
            auto const jSignBorrower = env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::tx_json));
            auto const txBorrowerSignResult = jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob = jSignBorrower[jss::result][jss::tx_blob].asString();

            // 5. Borrower submits the signed transaction blob
            lowerFee();
            auto const jSubmitBlob = env.rpc("submit", txBorrowerSignBlob);
            BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
            auto const jSubmitBlobResult = jSubmitBlob[jss::result];
            BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
            auto const jSubmitBlobTx = jSubmitBlobResult[jss::tx_json];
            // To get far enough to return tecNO_ENTRY means that the
            // signatures all validated. Of course the transaction won't
            // succeed because no Vault or Broker were created.
            BEAST_EXPECTS(
                jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() == "tecNO_ENTRY",
                to_string(jSubmitBlobResult));

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) && !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Borrower submits the transaction json originally
            // received from the Lender. It gets signed, but is now a
            // duplicate, so fails. Lender could done this instead of steps
            // 4 and 5.
            lowerFee();
            auto const jSubmitJson = env.rpc("json", "submit", to_string(borrowerSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECTS(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                    jSubmitJsonResult[jss::engine_result].asString() == "tefPAST_SEQ",
                to_string(jSubmitJsonResult));

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) && !jSubmitJsonResult.isMember(jss::error));

            BEAST_EXPECT(jSubmitBlobTx == jSubmitJsonTx);
        }
    }

    void
    testLendingCanTradeDisabledNoImpact()
    {
        testcase("Lending: CanTrade disabled has no impact");
        using namespace jtx;
        using namespace loan;
        using namespace loan_broker;

        Env env(*this, all_);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        MPTTester mpt(
            {.env = env,
             .issuer = issuer,
             .holders = {lender, borrower},
             .flags = tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const asset = mpt.issuanceID();
        env(pay(issuer, lender, asset(10'000'000)));
        env(pay(issuer, borrower, asset(100'000)));
        env.close();

        auto const broker = createVaultAndBroker(env, asset, lender);

        // CanTrade is not set
        env(offer(lender, XRP(1), asset(10)), Ter{tecNO_PERMISSION});
        env.close();

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        // New cover deposits still work.
        env(coverDeposit(lender, broker.brokerID, asset(100)));
        env.close();

        // New loan issuance still works.
        env(loan::set(borrower, broker.brokerID, 1'000),
            Sig(sfCounterpartySignature, lender),
            loanSetFee);
        env.close();
        auto const loanKeylet = keylet::loan(broker.brokerID, SeqProxy::rawSequence(1));
        BEAST_EXPECT(env.le(loanKeylet));

        // Repayment still works.
        env(pay(borrower, loanKeylet.key, asset(1'000)));
        env.close();

        // Cover withdrawal still works.
        env(coverWithdraw(lender, broker.brokerID, asset(100)));
        env.close();

        // Enable CanTrade and verify the DEX path is restored.
        mpt.set({.flags = tfMPTSetCanTrade});
        env.close();

        env(offer(lender, XRP(1), asset(10)));
        env.close();
    }

    void
    runAmendmentIndependent()
    {
        testLendingCanTradeDisabledNoImpact();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testRPC(features);
    }

public:
    void
    run() override
    {
        runAmendmentIndependent();
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2, featureLendingProtocolV1_1},
                 all_))
            runAmendmentSensitive(features);
    }
};

class LoanBatch_test : public LoanTestBase
{
protected:
    beast::xor_shift_engine engine_;

    std::uniform_int_distribution<> assetDist_{0, 2};
    std::uniform_int_distribution<std::int64_t> principalDist_{100'000, 1'000'000'000};
    std::uniform_int_distribution<std::uint32_t> interestRateDist_{0, 10000};
    std::uniform_int_distribution<> paymentTotalDist_{12, 10000};
    std::uniform_int_distribution<> paymentIntervalDist_{60, 3600 * 24 * 30};
    std::uniform_int_distribution<std::uint16_t> managementFeeRateDist_{0, 10'000};
    std::uniform_int_distribution<> serviceFeeDist_{0, 20};
    /*
        # Generate parameters that are more likely to be valid
    principal = Decimal(str(rand.randint(100000,
   100'000'000))).quantize(ROUND_TARGET)

    interest_rate = Decimal(rand.randint(1, 10000)) /
   Decimal(100000)

    payment_total = rand.randint(12, 10000)

    payment_interval = Decimal(str(rand.randint(60, 2629746)))

    interest_fee = Decimal(rand.randint(0, 100000)) /
   Decimal(100000)
*/

    void
    testRandomLoan()
    {
        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        // Determine all the random parameters at once
        auto const assetType = static_cast<AssetType>(assetDist_(engine_));
        auto const principalRequest = principalDist_(engine_);
        TenthBips16 const managementFeeRate{managementFeeRateDist_(engine_)};
        auto const serviceFee = serviceFeeDist_(engine_);
        TenthBips32 interest{interestRateDist_(engine_)};
        auto payTotal = paymentTotalDist_(engine_);
        auto const payInterval = paymentIntervalDist_(engine_);
        // The end of the last payment's grace period must fit in a 32-bit
        // ripple-epoch timestamp, or LoanSet fails with tecKILLED. Cap the
        // schedule well below that horizon (2e9 seconds is roughly 63 years,
        // leaving ample headroom over the ledger start date).
        constexpr std::uint32_t kMaxScheduleSeconds = 2'000'000'000;
        payTotal = std::min(payTotal, static_cast<int>(kMaxScheduleSeconds / payInterval));

        BrokerParameters const brokerParams{
            .vaultDeposit = principalRequest * 10,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = managementFeeRate,
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = principalRequest,
            .serviceFee = serviceFee,
            .interest = interest,
            .payTotal = payTotal,
            .payInterval = payInterval,
        };

        runLoan(assetType, brokerParams, loanParams, all_);
    }

public:
    void
    run() override
    {
        auto const numIterations = [s = arg()]() -> int {
            int const defaultNum = 5;
            if (s.empty())
                return defaultNum;
            try
            {
                std::size_t pos = 0;
                auto const r = stoi(s, &pos);
                if (pos != s.size())
                    return defaultNum;
                return r;
            }
            catch (...)
            {
                return defaultNum;
            }
        }();

        using namespace jtx;

        auto const updateInterval = std::max(std::min(numIterations / 5, 100), 1);

        for (int i = 0; i < numIterations; ++i)
        {
            if (i % updateInterval == 0)
                testcase << "Random Loan Test iteration " << (i + 1) << "/" << numIterations;
            testRandomLoan();
        }
    }
};

class LoanArbitrary_test : public LoanBatch_test
{
    void
    run() override
    {
        using namespace jtx;

        BrokerParameters const brokerParams{
            .vaultDeposit = 10000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = Account("lender"),
            .counter = Account("borrower"),
            .principalRequest = Number{200000, -6},
            .interest = TenthBips32{50000},
            .payTotal = 2,
            .payInterval = 200};

        runLoan(AssetType::XRP, brokerParams, loanParams, all_);
    }
};

BEAST_DEFINE_TESTSUITE(LoanMisc, tx, xrpl);
BEAST_DEFINE_TESTSUITE_MANUAL(LoanBatch, tx, xrpl);
BEAST_DEFINE_TESTSUITE_MANUAL(LoanArbitrary, tx, xrpl);

}  // namespace xrpl::test
