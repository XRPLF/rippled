#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace xrpl::test {

class LoanLifecycle_test : public LoanTestBase
{
private:
    void
    testLifecycle(FeatureBitset features)
    {
        testcase("Lifecycle");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for
        // an MPT. That'll require three corresponding SAVs.
        Env env(*this, features);

        Account const issuer{"issuer"};
        // For simplicity, lender will be the sole actor for the vault &
        // brokers.
        Account const lender{"lender"};
        // Borrower only wants to borrow
        Account const borrower{"borrower"};
        // Evan will attempt to be naughty
        Account const evan{"evan"};
        // Do not fund alice
        Account const alice{"alice"};

        // Fund the accounts and trust lines with the same amount so that
        // tests can use the same values regardless of the asset.
        env.fund(XRP(100'000'000), issuer, noripple(lender, borrower, evan));
        env.close();

        // Create assets
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        PrettyAsset const iouAsset = issuer[iouCurrency_];
        env(trust(lender, iouAsset(10'000'000)));
        env(trust(borrower, iouAsset(10'000'000)));
        env(trust(evan, iouAsset(10'000'000)));
        env(pay(issuer, evan, iouAsset(1'000'000)));
        env(pay(issuer, lender, iouAsset(10'000'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, iouAsset(10'000)));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        // Scale the MPT asset a little bit so we can get some interest
        PrettyAsset const mptAsset{mptt.issuanceID(), 100};
        mptt.authorize({.account = lender});
        mptt.authorize({.account = borrower});
        mptt.authorize({.account = evan});
        env(pay(issuer, lender, mptAsset(10'000'000)));
        env(pay(issuer, evan, mptAsset(1'000'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, mptAsset(10'000)));
        env.close();

        std::array const assets{iouAsset, xrpAsset, mptAsset};

        // Create vaults and loan brokers
        std::vector<BrokerInfo> brokers;
        brokers.reserve(assets.size());
        for (auto const& asset : assets)
        {
            brokers.emplace_back(createVaultAndBroker(
                env, asset, lender, BrokerParameters{.data = "spam spam spam spam"}));
        }

        // Create and update Loans
        for (auto const& broker : brokers)
        {
            for (int amountExponent = 3; amountExponent >= 3; --amountExponent)
            {
                Number const loanAmount{1, amountExponent};
                for (int interestExponent = 0; interestExponent >= 0; --interestExponent)
                {
                    testCaseWrapper(
                        env, mptt, assets, broker, loanAmount, interestExponent, LoanFlow::OneStep);
                    if (features[featureLendingProtocolV1_1])
                    {
                        testCaseWrapper(
                            env,
                            mptt,
                            assets,
                            broker,
                            loanAmount,
                            interestExponent,
                            LoanFlow::TwoStep);
                    }
                }
            }

            if (auto brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);

                auto const coverAvailable = brokerSle->at(sfCoverAvailable);
                env(loan_broker::coverWithdraw(
                    lender, broker.brokerID, STAmount(broker.asset, coverAvailable)));
                env.close();

                brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                BEAST_EXPECT(brokerSle && brokerSle->at(sfCoverAvailable) == 0);
            }
            // Verify we can delete the loan broker
            env(loan_broker::del(lender, broker.brokerID));
            env.close();
        }
    }

    void
    testSelfLoan(FeatureBitset features)
    {
        testcase << "Self Loan";

        using namespace jtx;
        using namespace std::chrono_literals;
        // Create 3 loan brokers: one for XRP, one for an IOU, and one for
        // an MPT. That'll require three corresponding SAVs.
        Env env(*this, features);

        Account const issuer{"issuer"};
        // For simplicity, lender will be the sole actor for the vault &
        // brokers.
        Account const lender{"lender"};

        // Fund the accounts and trust lines with the same amount so that
        // tests can use the same values regardless of the asset.
        env.fund(XRP(100'000'000), issuer, noripple(lender));
        env.close();

        // Use an XRP asset for simplicity
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        // Create vaults and loan brokers
        BrokerInfo broker{createVaultAndBroker(env, xrpAsset, lender)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        // The LoanSet json can be created without a counterparty signature,
        // but it will not pass preflight
        auto createJson = env.json(
            set(lender, broker.brokerID, broker.asset(principalRequest).value()), Fee(loanSetFee));
        env(createJson,
            env.enabled(featureLendingProtocolV1_1) ? Ter(temINVALID) : Ter(temBAD_SIGNER));

        // Adding an empty counterparty signature object also fails, but
        // at the RPC level.
        createJson = env.json(createJson, Json(sfCounterpartySignature, json::ValueType::Object));
        env(createJson, Ter(telENV_RPC_FAILED));

        if (auto const jt = env.jt(createJson); BEAST_EXPECT(jt.stx))
        {
            Serializer s;
            jt.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()));

            BEAST_EXPECT(jr.isMember(jss::result));
            auto const jResult = jr[jss::result];
            BEAST_EXPECT(jResult[jss::error] == "invalidTransaction");
            BEAST_EXPECT(
                jResult[jss::error_exception] ==
                "fails local checks: Transaction has bad signature.");
        }

        // Copy the transaction signature into the counterparty signature.
        json::Value counterpartyJson{json::ValueType::Object};
        counterpartyJson[sfTxnSignature] = createJson[sfTxnSignature];
        counterpartyJson[sfSigningPubKey] = createJson[sfSigningPubKey];
        if (!BEAST_EXPECT(!createJson.isMember(jss::Signers)))
            counterpartyJson[sfSigners] = createJson[sfSigners];

        // The duplicated signature works
        createJson = env.json(createJson, Json(sfCounterpartySignature, counterpartyJson));
        env(createJson);

        env.close();

        auto const startDate = env.current()->header().parentCloseTime;

        // Loan is successfully created
        {
            auto const res = env.rpc("account_objects", lender.human());
            auto const objects = res[jss::result][jss::account_objects];

            std::map<std::string, std::size_t> types;
            BEAST_EXPECT(objects.size() == 4);
            for (auto const& object : objects)
            {
                ++types[object[sfLedgerEntryType].asString()];
            }
            BEAST_EXPECT(types.size() == 4);
            for (std::string const type : {"MPToken", "Vault", "LoanBroker", "Loan"})
            {
                BEAST_EXPECT(types[type] == 1);
            }
        }
        auto const loanID = [&]() {
            json::Value params(json::ValueType::Object);
            params[jss::account] = lender.human();
            params[jss::type] = "Loan";
            auto const res = env.rpc("json", "account_objects", to_string(params));
            auto const objects = res[jss::result][jss::account_objects];

            BEAST_EXPECT(objects.size() == 1);

            auto const loan = objects[0u];
            BEAST_EXPECT(loan[sfBorrower] == lender.human());
            // soeDEFAULT fields are not returned if they're in the default
            // state
            BEAST_EXPECT(!loan.isMember(sfCloseInterestRate));
            BEAST_EXPECT(!loan.isMember(sfClosePaymentFee));
            BEAST_EXPECT(loan[sfFlags] == 0);
            BEAST_EXPECT(loan[sfGracePeriod] == 60);
            BEAST_EXPECT(!loan.isMember(sfInterestRate));
            BEAST_EXPECT(!loan.isMember(sfLateInterestRate));
            BEAST_EXPECT(!loan.isMember(sfLatePaymentFee));
            BEAST_EXPECT(loan[sfLoanBrokerID] == to_string(broker.brokerID));
            BEAST_EXPECT(!loan.isMember(sfLoanOriginationFee));
            BEAST_EXPECT(loan[sfLoanSequence] == 1);
            BEAST_EXPECT(!loan.isMember(sfLoanServiceFee));
            BEAST_EXPECT(loan[sfNextPaymentDueDate] == loan[sfStartDate].asUInt() + 60);
            BEAST_EXPECT(!loan.isMember(sfOverpaymentFee));
            BEAST_EXPECT(!loan.isMember(sfOverpaymentInterestRate));
            BEAST_EXPECT(loan[sfPaymentInterval] == 60);
            BEAST_EXPECT(loan[sfPeriodicPayment] == "1000000000");
            BEAST_EXPECT(loan[sfPaymentRemaining] == 1);
            BEAST_EXPECT(!loan.isMember(sfPreviousPaymentDueDate));
            BEAST_EXPECT(loan[sfPrincipalOutstanding] == "1000000000");
            BEAST_EXPECT(loan[sfTotalValueOutstanding] == "1000000000");
            BEAST_EXPECT(!loan.isMember(sfLoanScale));
            BEAST_EXPECT(loan[sfStartDate].asUInt() == startDate.time_since_epoch().count());

            return loan["index"].asString();
        }();
        auto const loanKeylet{keylet::loan(uint256{std::string_view(loanID)})};

        env.close(startDate);

        // Make a payment
        env(pay(lender, loanKeylet.key, broker.asset(1000)));
    }

    void
    testIssuerLoan()
    {
        testcase << "Issuer Loan";

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;
        Account const issuer("issuer");
        Account const borrower = issuer;
        Account const lender("lender");

        // Exercise both creation flows where supported. In the two-step flow
        // the broker owner (lender) proposes the loan naming the issuer as the
        // borrower, who then accepts it.
        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            bool const twoStep = flow == LoanFlow::TwoStep;

            Env env(*this);
            if (twoStep && !env.enabled(featureLendingProtocolV1_1))
                continue;

            env.fund(XRP(1'000), issuer, lender);

            static constexpr std::int64_t kIssuerBalance = 10'000'000;
            MPTTester const asset(
                {.env = env, .issuer = issuer, .holders = {lender}, .pay = kIssuerBalance});

            BrokerParameters const brokerParams{
                .debtMax = 200,
            };
            auto const broker = createVaultAndBroker(env, asset, lender, brokerParams);
            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            auto const loanKeylet = keylet::loan(broker.brokerID, SeqProxy::rawSequence(1));
            // Create Loan
            if (twoStep)
            {
                env(set(lender, broker.brokerID, 200),
                    kBorrower(borrower),
                    kStartDate((env.now() + 1h).time_since_epoch().count()),
                    loanSetFee);
                env.close();
                env(accept(borrower, loanKeylet.key));
                env.close();
            }
            else
            {
                env(set(borrower, broker.brokerID, 200),
                    Sig(sfCounterpartySignature, lender),
                    loanSetFee);
                env.close();
            }
            // Issuer should not create MPToken
            BEAST_EXPECT(!env.le(keylet::mptoken(asset.issuanceID(), issuer)));
            // Issuer "borrowed" 200, OutstandingAmount decreased by 200
            BEAST_EXPECT(env.balance(issuer, asset) == asset(-kIssuerBalance + 200));
            // Pay Loan
            env(pay(borrower, loanKeylet.key, asset(200)));
            env.close();
            // Issuer "re-payed" 200, OutstandingAmount increased by 200
            BEAST_EXPECT(env.balance(issuer, asset) == asset(-kIssuerBalance));
        }
    }

    void
    testBorrowerIsBroker()
    {
        testcase("Test Borrower is Broker");
        using namespace jtx;
        using namespace loan;
        Account const broker{"broker"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Account const depositor{"depositor"};

        auto testLoanAsset = [&](auto&& getMaxDebt, auto const& borrower) {
            Env env(*this);
            Vault const vault(env);

            if (borrower == broker)
            {
                env.fund(XRP(10'000), broker, issuer, depositor);
            }
            else
            {
                env.fund(XRP(10'000), broker, borrower, issuer, depositor);
            }
            env.close();

            auto const xrpFee = XRP(100);
            auto const txFee = Fee(xrpFee);

            STAmount const debtMaximumRequest = getMaxDebt(env);

            auto const& asset = debtMaximumRequest.asset();
            auto const initialVault = asset(debtMaximumRequest * 100);

            auto [tx, vaultKeylet] = vault.create({.owner = broker, .asset = asset});
            env(tx, txFee);
            env.close();

            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = initialVault}),
                txFee);
            env.close();

            auto const brokerKeylet =
                keylet::loanBroker(broker.id(), SeqProxy::rawSequence(env.seq(broker)));

            env(loan_broker::set(broker, vaultKeylet.key), txFee);
            env.close();

            auto const serviceFee = 101;

            env(set(broker, brokerKeylet.key, debtMaximumRequest),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                kLoanServiceFee(serviceFee),
                kPaymentTotal(10),
                txFee);
            env.close();

            std::uint32_t const loanSequence = 1;
            auto const loanKeylet =
                keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(loanSequence));

            auto const brokerBalanceBefore = env.balance(broker, asset);

            if (auto const loanSle = env.le(loanKeylet); env.test.BEAST_EXPECT(loanSle))
            {
                auto const payment = loanSle->at(sfPeriodicPayment);
                auto const totalPayment = payment + serviceFee;
                env(loan::pay(borrower, loanKeylet.key, asset(totalPayment)), txFee);
                env.close();
                if (auto const vaultSle = env.le(vaultKeylet); BEAST_EXPECT(vaultSle))
                {
                    auto const expected = [&]() {
                        // The service fee is transferred to the broker if
                        // a borrower is not the broker
                        if (borrower != broker)
                            return brokerBalanceBefore.number() + serviceFee;
                        // Since a borrower is the broker, the payment is
                        // transferred to the Vault from the broker but not
                        // the service fee.
                        // If the asset is XRP then the broker pays the txFee.
                        if (asset.native())
                            return brokerBalanceBefore.number() - payment - xrpFee.number();
                        return brokerBalanceBefore.number() - payment;
                    }();
                    BEAST_EXPECT(env.balance(broker, asset).value() == asset(expected).value());
                }
            }
        };
        // Test when a borrower is the broker and is not to verify correct
        // service fee transfer in both cases.
        for (auto const& borrowerAcct : {broker, borrower})
        {
            testLoanAsset(
                [&](Env&) -> STAmount { return STAmount{XRPAmount{200'000}}; }, borrowerAcct);
            testLoanAsset(
                [&](Env& env) -> STAmount {
                    auto const iou = issuer["USD"];
                    env(trust(broker, iou(1'000'000'000)));
                    env(trust(depositor, iou(1'000'000'000)));
                    env(pay(issuer, broker, iou(100'000'000)));
                    env(pay(issuer, depositor, iou(100'000'000)));
                    env.close();
                    return iou(200'000);
                },
                borrowerAcct);
            testLoanAsset(
                [&](Env& env) -> STAmount {
                    MPTTester const mpt(
                        {.env = env,
                         .issuer = issuer,
                         .holders = {broker, depositor},
                         .pay = 100'000'000});
                    return mpt(200'000);
                },
                borrowerAcct);
        }
    }

    void
    testIssuerIsBorrower(FeatureBitset features)
    {
        testcase("RIPD-4096 - Issuer as borrower");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender, .counter = issuer, .principalRequest = Number{10000}};

        auto const assetType = AssetType::IOU;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, issuer);

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
            issuer,
            PaymentParameters{.showStepBalances = true});
    }

    void
    testBatchBypassCounterparty(FeatureBitset features)
    {
        // From FIND-001
        testcase << "Batch Bypass Counterparty";

        bool const lendingBatchEnabled = !std::ranges::any_of(
            Batch::kDisabledTxTypes, [](auto const& disabled) { return disabled == ttLOAN_SET; });

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, features);

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        BrokerParameters const brokerParams;
        env.fund(XRP(brokerParams.vaultDeposit * 100), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto forgedLoanSet = set(borrower, broker.brokerID, principalRequest, 0);

        json::Value randomData{json::ValueType::Object};
        randomData[jss::SigningPubKey] = json::StaticString{"2600"};
        json::Value sigObject{json::ValueType::Object};
        sigObject[jss::SigningPubKey] = strHex(lender.pk().slice());
        Serializer ss;
        ss.add32(HashPrefix::TxSign);
        parse(randomData).addWithoutSigningFields(ss);
        auto const sig = xrpl::sign(borrower.pk(), borrower.sk(), ss.slice());
        sigObject[jss::TxnSignature] = strHex(Slice{sig.data(), sig.size()});

        forgedLoanSet[json::StaticString{"CounterpartySignature"}] = sigObject;

        // ? Fails because the lender hasn't signed the tx
        env(env.json(forgedLoanSet, Fee(loanSetFee)), Ter(telENV_RPC_FAILED));

        auto const seq = env.seq(borrower);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        // ! Should fail because the lender hasn't signed the tx
        env(batch::outer(borrower, seq, batchFee, tfAllOrNothing),
            batch::Inner(forgedLoanSet, seq + 1),
            batch::Inner(pay(borrower, lender, XRP(1)), seq + 2),
            Ter(lendingBatchEnabled ? temBAD_SIGNATURE : temINVALID_INNER_BATCH));
        env.close();

        // ? Check that the loan was NOT created
        {
            json::Value params(json::ValueType::Object);
            params[jss::account] = borrower.human();
            params[jss::type] = "Loan";
            auto const res = env.rpc("json", "account_objects", to_string(params));
            auto const objects = res[jss::result][jss::account_objects];
            BEAST_EXPECT(objects.size() == 0);
        }

        // XLS-66 spec 3.8.5.2.1 (Batch-inner refinement): a Batch inner
        // LoanSet with no Counterparty and no Borrower is rejected with
        // temBAD_SIGNER in preflight. Inside a Batch, the immediate flow
        // still applies but the inner transaction cannot carry a
        // CounterpartySignature, so the Counterparty must be named
        // explicitly on the inner transaction.
        {
            auto const jtx =
                env.jt(set(lender, broker.brokerID, principalRequest), Txflags(tfInnerBatchTxn));
            if (BEAST_EXPECT(jtx.stx))
            {
                PreflightContext const pfCtx(
                    env.app(), *jtx.stx, uint256{1}, env.current()->rules(), TapBatch, env.journal);
                BEAST_EXPECT(Transactor::invokePreflight<LoanSet>(pfCtx) == temBAD_SIGNER);
            }
        }

        // XLS-66 flow (Batch + V1.1): a Batch inner LoanSet may name a
        // Borrower (with a StartDate) instead of a Counterparty: the
        // borrower is identified explicitly on the inner tx and no
        // CounterpartySignature is required. Preflight must accept it.
        if (features[featureLendingProtocolV1_1])
        {
            auto const jtx = env.jt(
                set(lender, broker.brokerID, principalRequest),
                Txflags(tfInnerBatchTxn),
                kBorrower(borrower),
                kStartDate((env.now() + 1h).time_since_epoch().count()));
            if (BEAST_EXPECT(jtx.stx))
            {
                PreflightContext const pfCtx(
                    env.app(), *jtx.stx, uint256{1}, env.current()->rules(), TapBatch, env.journal);
                BEAST_EXPECT(Transactor::invokePreflight<LoanSet>(pfCtx) == tesSUCCESS);
            }

            // XLS-66 flow (Batch + V1.1): a Batch inner LoanSet with
            // Borrower but no StartDate is not a valid two-step proposal
            // and no longer masquerades as a missing-Counterparty error:
            // it is rejected as temINVALID by getLoanFlow, past the
            // Batch-specific check.
            auto const jtxNoStart = env.jt(
                set(lender, broker.brokerID, principalRequest),
                Txflags(tfInnerBatchTxn),
                kBorrower(borrower));
            if (BEAST_EXPECT(jtxNoStart.stx))
            {
                PreflightContext const pfCtx(
                    env.app(),
                    *jtxNoStart.stx,
                    uint256{1},
                    env.current()->rules(),
                    TapBatch,
                    env.journal);
                BEAST_EXPECT(Transactor::invokePreflight<LoanSet>(pfCtx) == temINVALID);
            }
        }

        // XLS-66 flow (Batch + V1.1) success: a Batch containing an inner
        // LoanSet that names a Counterparty (but carries no
        // CounterpartySignature) is accepted when the counterparty signs
        // the outer Batch. The immediate flow's counterparty consent is
        // satisfied by the batch signature rather than an inner
        // CounterpartySignature. Requires both the Batch and
        // LendingProtocolV1_1 amendments.
        if (features[featureLendingProtocolV1_1] && lendingBatchEnabled)
        {
            auto const lenderSeq = env.seq(lender);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const loanKeylet = keylet::loan(broker.brokerID, SeqProxy::rawSequence(1));

            env(batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                batch::Inner(
                    env.json(
                        set(lender, broker.brokerID, principalRequest),
                        kCounterparty(borrower.id()),
                        Sig(kNone),
                        Fee(kNone),
                        Seq(kNone)),
                    lenderSeq + 1),
                batch::Inner(pay(lender, borrower, XRP(1)), lenderSeq + 2),
                batch::Sig(borrower));
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
        }
    }

    // Integration test: full lifecycle of a $1B loan in the bug regime.
    // Verifies that the vault collects the economically-correct interest
    // income and that conservation holds at the trust-line level.
    //
    // Pre-fix (closed-form `power(1+r, n) - 1`): vault collected only
    // ~$0.058 per $1B due to cancellation of `(1+r)^n - 1` at r*n ~ 5.7e-10.
    // Post-fix (hybrid binomial path): vault collects ~$0.38 per $1B,
    // matching the value computed independently with arbitrary-precision
    // Decimal arithmetic.
    void
    testFullLifecycleVaultPnLNearZeroRate()
    {
        testcase("integration: full loan lifecycle, vault interest at near-zero rate");

        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;
        Env env(*this, all_);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const iouAsset = issuer["USD"];
        STAmount const trustLimit{iouAsset.raw(), Number{1, 17}};
        env(trust(lender, trustLimit));
        env(trust(borrower, trustLimit));
        env.close();
        env(pay(issuer, lender, iouAsset(5'000'000'000LL)));
        env(pay(issuer, borrower, iouAsset(5'000'000'000LL)));
        env.close();

        auto usdBalance = [&](Account const& a) {
            return env.balance(a, iouAsset.raw().get<Issue>()).value();
        };
        STAmount const borrowerStartBal = usdBalance(borrower);

        BrokerParameters const brokerParams{
            .vaultDeposit = Number{2, 9},
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender, brokerParams)};

        auto const vaultBefore = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultBefore))
            return;
        Number const vaultAvailableBefore = vaultBefore->at(sfAssetsAvailable);

        // Loan: $1B principal, 3 payments, 600s interval, rate=1 TenthBips32.
        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 9};
        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            Fee(loanSetFee),
            Json(sfCounterpartySignature, json::ValueType::Object));
        createJson["InterestRate"] = 1;
        createJson["PaymentTotal"] = 3;
        createJson["PaymentInterval"] = 600;

        auto const loanKeylet = nextLoanKeylet(env, broker);
        createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
        env(createJson, Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        Number const expectedTotalInterest =
            loanSle->at(sfTotalValueOutstanding) - loanSle->at(sfPrincipalOutstanding);

        env(pay(borrower, loanKeylet.key, iouAsset(1'500'000'000LL)), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfter = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(vaultAfter))
            return;
        Number const vaultAvailableAfter = vaultAfter->at(sfAssetsAvailable);
        Number const vaultGain = vaultAvailableAfter - vaultAvailableBefore;

        STAmount const borrowerEndBal = usdBalance(borrower);
        STAmount const borrowerNetOut = borrowerStartBal - borrowerEndBal;

        // Self-consistency: vault gained exactly the expected interest
        // computed at LoanSet, and the borrower's outflow matches.
        BEAST_EXPECT(vaultGain == expectedTotalInterest);
        BEAST_EXPECT(Number(borrowerNetOut) == expectedTotalInterest);

        // Mathematical correctness: the total interest for this loan
        // configuration is 0.38051750382930729983, calculated
        // independently using 50-digit Decimal arithmetic (no
        // cancellation possible at that precision). At Number's 19-digit
        // mantissa this rounds to 0.38051750382930729 — the literal
        // below. The vault's actual gain must agree to within
        // sub-microcent precision.
        Number const decimalReference{38051750382930729LL, -17};
        Number const tolerance{1, -6};  // 1e-6 USD = sub-microcent
        Number const error = abs(vaultGain - decimalReference);
        BEAST_EXPECTS(
            error < tolerance,
            "vault gain " + to_string(vaultGain) + " differs from Decimal reference " +
                to_string(decimalReference) + " by " + to_string(error) + " — exceeds tolerance " +
                to_string(tolerance));
    }

    void
    runAmendmentIndependent()
    {
        testIssuerLoan();
        testBorrowerIsBroker();
        testFullLifecycleVaultPnLNearZeroRate();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testLifecycle(features);
        testSelfLoan(features);
        testIssuerIsBorrower(features);
        testBatchBypassCounterparty(features);
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

BEAST_DEFINE_TESTSUITE(LoanLifecycle, tx, xrpl);

}  // namespace xrpl::test
