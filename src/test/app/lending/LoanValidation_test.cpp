#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <cstdint>
#include <optional>

namespace xrpl::test {

class LoanValidation_test : public LoanTestBase
{
private:
    void
    testDisabled()
    {
        testcase("Disabled");
        // Lending Protocol depends on Single Asset Vault (SAV). Test
        // combinations of the two amendments.
        // Single Asset Vault depends on MPTokensV1, but don't test every combo
        // of that.
        using namespace jtx;
        auto failAll = [this](FeatureBitset features) {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);

            auto const keylet = keylet::loanBroker(alice, SeqProxy::rawSequence(env.seq(alice)));

            using namespace std::chrono_literals;
            using namespace loan;

            // counter party signature is optional on LoanSet. Confirm that by
            // sending transaction without one.
            auto setTx = env.jt(set(alice, keylet.key, Number(10000)), Ter(temDISABLED));
            env(setTx);

            // All loan transactions are disabled.
            // 1. LoanSet
            setTx = env.jt(setTx, Sig(sfCounterpartySignature, bob), Ter(temDISABLED));
            env(setTx);
            // Actual sequence will be based off the loan broker, but we
            // obviously don't have one of those if the amendment is disabled
            auto const loanKeylet = keylet::loan(keylet.key, SeqProxy::rawSequence(env.seq(alice)));
            // Other Loan transactions are disabled, too.
            // 2. LoanDelete
            env(del(alice, loanKeylet.key), Ter(temDISABLED));
            // 3. LoanManage
            env(manage(alice, loanKeylet.key, tfLoanImpair), Ter(temDISABLED));
            // 4. LoanPay
            env(pay(alice, loanKeylet.key, XRP(500)), Ter(temDISABLED));
        };
        failAll(all_ - featureMPTokensV1);
        failAll(all_ - featureSingleAssetVault - featureLendingProtocol);
        failAll(all_ - featureSingleAssetVault);
        failAll(all_ - featureLendingProtocol);
    }

    void
    testInvalidLoanSet()
    {
        testcase("Invalid LoanSet");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Account const sponsor{"sponsor"};
        auto const iou = issuer["IOU"];

        auto testWrapper = [&](auto&& test) {
            Env env(*this);
            env.fund(XRP(1'000), lender, issuer, borrower, sponsor);
            env(trust(lender, iou(10'000'000)));
            env(pay(issuer, lender, iou(5'000'000)));
            BrokerInfo const brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const debtMaximumRequest = brokerInfo.asset(1'000).value();
            test(env, brokerInfo, loanSetFee, debtMaximumRequest);
        };

        // preflight:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            for (auto const sponsorFlags : {spfSponsorReserve, spfSponsorReserve | spfSponsorFee})
            {
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    sponsor::As(sponsor, sponsorFlags),
                    Sig(sfCounterpartySignature, lender),
                    loanSetFee,
                    Ter(temINVALID_FLAG));
            }

            // first temBAD_SIGNER: TODO
            // invalid grace period
            {
                // zero grace period
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kGracePeriod(0),
                    loanSetFee,
                    Ter(temINVALID));

                // grace period less than default minimum
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kGracePeriod(LoanSet::kDefaultGracePeriod - 1),
                    loanSetFee,
                    Ter(temINVALID));

                // grace period greater than payment interval
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kPaymentInterval(120),
                    kGracePeriod(121),
                    loanSetFee,
                    Ter(temINVALID));
            }
            // empty/zero broker ID
            {
                auto jv = set(borrower, uint256{}, debtMaximumRequest);

                auto testZeroBrokerID = [&](std::string const& id, std::uint32_t flags = 0) {
                    // empty broker ID
                    jv[sfLoanBrokerID] = id;
                    env(jv,
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Txflags(flags),
                        Ter(temINVALID));
                };
                // empty broker ID
                testZeroBrokerID(std::string(""));
                // zero broker ID
                // needs a flag to distinguish the parsed STTx from the prior
                // test
                testZeroBrokerID(to_string(uint256{}), tfFullyCanonicalSig);
            }

            // preflightCheckSigningKey() failure:
            // can it happen? the signature is checked before transactor
            // executes

            JTx const tx = env.jt(
                set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee);
            STTx local = *(tx.stx);
            auto counterpartySig = local.getFieldObject(sfCounterpartySignature);
            auto badPubKey = counterpartySig.getFieldVL(sfSigningPubKey);
            badPubKey[20] ^= 0xAA;
            counterpartySig.setFieldVL(sfSigningPubKey, badPubKey);
            local.setFieldObject(sfCounterpartySignature, counterpartySig);
            json::Value jvResult;
            jvResult[jss::tx_blob] = strHex(local.getSerializer().slice());
            auto res = env.rpc("json", "submit", to_string(jvResult))["result"];
            BEAST_EXPECT(
                res[jss::error] == "invalidTransaction" &&
                res[jss::error_exception] ==
                    "fails local checks: Counterparty: Invalid signature.");
        });

        // preclaim:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            // canAddHoldingFailure (IOU only, if MPT doesn't have
            // MPTCanTransfer set, then can't create Vault/LoanBroker,
            // and LoanSet will fail with different error
            env(fclear(issuer, asfDefaultRipple));
            env.close();
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(terNO_RIPPLE));
        });

        // doApply:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            auto const amt =
                env.balance(borrower) - accountReserve(*env.current(), borrower.id(), env.journal);
            env(pay(borrower, issuer, amt));

            // tecINSUFFICIENT_RESERVE
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecINSUFFICIENT_RESERVE));

            // addEmptyHolding failure
            env(pay(issuer, borrower, amt));
            env(fset(issuer, asfGlobalFreeze));
            env.close();

            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecFROZEN));
        });
    }

    void
    testInvalidLoanDelete()
    {
        testcase("Invalid LoanDelete");
        using namespace jtx;
        using namespace loan;

        // preflight: temINVALID, LoanID == zero
        {
            Account const alice{"alice"};
            Env env(*this);
            env.fund(XRP(1'000), alice);
            env.close();
            env(del(alice, beast::kZero), Ter(temINVALID));
        }
    }

    void
    testInvalidLoanManage()
    {
        testcase("Invalid LoanManage");
        using namespace jtx;
        using namespace loan;

        // preflight: temINVALID, LoanID == zero
        {
            Account const alice{"alice"};
            Env env(*this);
            env.fund(XRP(1'000), alice);
            env.close();
            env(manage(alice, beast::kZero, tfLoanDefault), Ter(temINVALID));
        }
    }

    void
    testInvalidLoanPay()
    {
        testcase("Invalid LoanPay");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        auto const iou = issuer["IOU"];

        // preclaim
        Env env(*this);
        env.fund(XRP(1'000), lender, issuer, borrower);
        env(trust(lender, iou(10'000'000)));
        env(pay(issuer, lender, iou(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
            Sig(sfCounterpartySignature, lender),
            loanSetFee);

        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet =
            keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(loanSequence));

        env(fset(issuer, asfGlobalFreeze));
        env.close();

        // preclaim: tecFROZEN
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecFROZEN));
        env.close();

        env(fclear(issuer, asfGlobalFreeze));
        env.close();

        auto const pseudoBroker = [&]() -> std::optional<Account> {
            if (auto brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                return Account{"pseudo", brokerSle->at(sfAccount)};
            }

            return std::nullopt;
        }();
        if (!pseudoBroker)
            return;

        // Lender and pseudoaccount must both be frozen
        env(trust(issuer, lender["IOU"](1'000), lender, tfSetFreeze | tfSetDeepFreeze));
        env(trust(
            issuer, (*pseudoBroker)["IOU"](1'000), *pseudoBroker, tfSetFreeze | tfSetDeepFreeze));
        env.close();

        // preclaim: tecFROZEN due to deep frozen
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecFROZEN));
        env.close();

        // Only one needs to be unfrozen
        env(trust(issuer, lender["IOU"](1'000), tfClearFreeze | tfClearDeepFreeze));
        env.close();

        // The payment is late by this point
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecEXPIRED));
        env.close();
        env(pay(borrower, loanKeylet.key, debtMaximumRequest, tfLoanLatePayment));
        env.close();

        // preclaim: tecKILLED
        // note that tecKILLED in loanMakePayment()
        // doesn't happen because of the preclaim check.
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecKILLED));
    }

    void
    testRequireAuth()
    {
        testcase("Require Auth - Implicit Pseudo-account authorization");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Env env(*this);

        env.fund(XRP(100'000), issuer, lender, borrower);
        env.close();

        auto asset = MPTTester({
            .env = env,
            .issuer = issuer,
            .holders = {lender, borrower},
            .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
            .authHolder = true,
        });

        env(pay(issuer, lender, asset(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, asset, lender)};

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        auto forUnauthAuth = [&](auto&& doTx) {
            for (auto const flag : {tfMPTUnauthorize, 0u})
            {
                asset.authorize({.account = issuer, .holder = borrower, .flags = flag});
                env.close();
                doTx(flag == 0);
                env.close();
            }
        };

        // Can't create a loan if the borrower is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                err);
        });

        static constexpr std::uint32_t kLoanSequence = 1;
        auto const loanKeylet =
            keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(kLoanSequence));

        // Can't loan pay if the borrower is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(pay(borrower, loanKeylet.key, debtMaximumRequest), err);
        });
    }

    void
    testLimitExceeded()
    {
        testcase("RIPD-4125 - overpayment");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{200000, -6},
            .interest = TenthBips32{50000},
            .payTotal = 3,
            .payInterval = 200,
            .gracePd = 60,
            .flags = tfLoanOverpayment,
        };

        auto const assetType = AssetType::XRP;

        Env env(*this, makeConfig(), all_, nullptr, beast::Severity::Warning);

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        auto const state = getCurrentState(env, broker, loanKeylet);

        env(loan::pay(
            borrower,
            loanKeylet.key,
            STAmount{broker.asset, state.periodicPayment * 3 / 2 + 1},
            tfLoanOverpayment));
        env.close();

        PaymentParameters const paymentParams{
            .showStepBalances = false,
            .validateBalances = true,
        };

        makeLoanPayments(
            env,
            broker,
            loanParams,
            loanKeylet,
            verifyLoanStatus,
            issuer,
            lender,
            borrower,
            paymentParams);
    }

    void
    testWrongMaxDebtBehavior(FeatureBitset features)
    {
        // From FIND-003
        testcase << "Wrong Max Debt Behavior";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        BrokerParameters const brokerParams{.debtMax = 0};
        env.fund(XRP(brokerParams.vaultDeposit * 100), issuer, noripple(lender));
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfDebtMaximum) == 0);
        }

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(set(lender, broker.brokerID, principalRequest), Fee(loanSetFee));

        json::Value counterpartyJson{json::ValueType::Object};
        counterpartyJson[sfTxnSignature] = createJson[sfTxnSignature];
        counterpartyJson[sfSigningPubKey] = createJson[sfSigningPubKey];
        if (!BEAST_EXPECT(!createJson.isMember(jss::Signers)))
            counterpartyJson[sfSigners] = createJson[sfSigners];

        createJson = env.json(createJson, Json(sfCounterpartySignature, counterpartyJson));
        env(createJson);

        env.close();
    }

    void
    runAmendmentIndependent()
    {
        testDisabled();
        testInvalidLoanSet();
        testInvalidLoanDelete();
        testInvalidLoanManage();
        testInvalidLoanPay();
        testRequireAuth();
        testLimitExceeded();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testWrongMaxDebtBehavior(features);
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

BEAST_DEFINE_TESTSUITE(LoanValidation, tx, xrpl);

}  // namespace xrpl::test
