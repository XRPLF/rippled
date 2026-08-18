#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>

namespace xrpl::test {

class LoanCoverFreezeAuth_test : public LoanTestBase
{
private:
    void
    testSequentialFLCDepletion(FeatureBitset features)
    {
        testcase << "First-Loss Capital Depletion on Sequential Defaults";

        using namespace jtx;
        using namespace loan;
        using namespace loan_broker;

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

        auto const loanAKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));

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

        auto const loanBKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(2));

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

        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(sleBroker->at(sfLoanSequence)));

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
    testServiceFeeOnBrokerDeepFreeze()
    {
        testcase << "Service Fee On Broker Deep Freeze";
        using namespace jtx;
        using namespace loan;
        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");
        auto const iou = issuer["IOU"];

        for (bool const deepFreeze : {true, false})
        {
            Env env(*this);

            auto getCoverBalance = [&](BrokerInfo const& brokerInfo, auto const& accountField) {
                if (auto const le = env.le(keylet::loanBroker(brokerInfo.brokerID));
                    BEAST_EXPECT(le))
                {
                    auto const account = le->at(accountField);
                    if (auto const sleLine = env.le(keylet::trustLine(account, iou));
                        BEAST_EXPECT(sleLine))
                    {
                        STAmount balance = sleLine->at(sfBalance);
                        if (account > issuer.id())
                            balance.negate();
                        return balance;
                    }
                }
                return STAmount{iou};
            };

            env.fund(XRP(20'000), issuer, broker, borrower);
            env.close();

            env(trust(broker, iou(20'000'000)));
            env(pay(issuer, broker, iou(10'000'000)));
            env.close();

            auto const brokerInfo = createVaultAndBroker(env, iou, broker);

            BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == iou(1'000));

            auto const keylet = keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(1));

            env(set(borrower, brokerInfo.brokerID, 10'000),
                Sig(sfCounterpartySignature, broker),
                kLoanServiceFee(iou(100).value()),
                kPaymentInterval(100),
                Fee(XRP(100)));
            env.close();

            env(trust(borrower, iou(20'000'000)));
            // The borrower increases their limit and acquires some IOU so
            // they can pay interest
            env(pay(issuer, borrower, iou(500)));
            env.close();

            if (auto const le = env.le(keylet::loan(keylet.key)); BEAST_EXPECT(le))
            {
                if (deepFreeze)
                {
                    env(trust(issuer, broker["IOU"](0), tfSetFreeze | tfSetDeepFreeze));
                    env.close();
                }

                env(pay(borrower, keylet.key, iou(10'100)), Fee(XRP(100)));
                env.close();

                if (deepFreeze)
                {
                    // The fee goes to the broker pseudo-account
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == iou(1'100));
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfOwner) == iou(8'999'000));
                }
                else
                {
                    // The fee goes to the broker account
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfOwner) == iou(8'999'100));
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == iou(1'000));
                }
            }
        };
    }

    void
    testLoanPayBrokerOwnerMissingTrustline(FeatureBitset features)
    {
        testcase << "LoanPay Broker Owner Missing Trustline (PoC)";
        using namespace jtx;
        using namespace loan;
        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");
        auto const iou = issuer["IOU"];
        Env env(*this, features);
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();
        // Set up trustlines and fund accounts
        env(trust(broker, iou(20'000'000)));
        env(trust(borrower, iou(20'000'000)));
        env(pay(issuer, broker, iou(10'000'000)));
        env(pay(issuer, borrower, iou(1'000)));
        env.close();
        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, iou, broker);
        // Create a loan first (this creates debt)
        auto const keylet = keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(1));
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(iou(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)));
        env.close();
        // Ensure broker has sufficient cover so brokerPayee == brokerOwner
        // We need coverAvailable >= (debtTotal * coverRateMinimum)
        // Deposit enough cover to ensure the fee goes to broker owner
        // The default coverRateMinimum is 10%, so for a 10,000 loan we need
        // at least 1,000 cover. Default cover is 1,000, so we add more to be
        // safe.
        auto const additionalCover = iou(50'000).value();
        env(loan_broker::coverDeposit(broker, brokerInfo.brokerID, STAmount{iou, additionalCover}));
        env.close();
        // Verify broker owner has a trustline
        auto const brokerTrustline = keylet::trustLine(broker, iou);
        BEAST_EXPECT(env.le(brokerTrustline) != nullptr);
        // Broker owner deletes their trustline
        // First, pay any positive balance to issuer to zero it out
        auto const brokerBalance = env.balance(broker, iou);
        env(pay(broker, issuer, brokerBalance));
        env.close();
        // Remove the trustline by setting limit to 0
        env(trust(broker, iou(0)));
        env.close();
        // Verify trustline is deleted
        BEAST_EXPECT(env.le(brokerTrustline) == nullptr);
        // Now borrower tries to make a payment
        // We should get a tesSUCCESS instead of a tecNO_LINE.
        env(pay(borrower, keylet.key, iou(10'100)), Fee(XRP(100)), Ter(tesSUCCESS));
        env.close();
        // Verify trustline is still deleted
        BEAST_EXPECT(env.le(brokerTrustline) == nullptr);
        // Verify the service fee went to the broker pseudo-account
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            Account const pseudo("pseudo-account", brokerSle->at(sfAccount));
            auto const balance = env.balance(pseudo, iou);
            // 1,000 default + 50,000 extra + 100 service fee from LoanPay
            BEAST_EXPECTS(balance == iou(51'100), to_string(json::Value(balance)));
        }
    }

    void
    testLoanPayBrokerOwnerUnauthorizedMPT(FeatureBitset features)
    {
        testcase << "LoanPay Broker Owner MPT unauthorized";
        using namespace jtx;
        using namespace loan;

        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");

        Env env{*this, features};
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});

        PrettyAsset const mpt{mptt.issuanceID()};

        // Authorize broker and borrower
        mptt.authorize({.account = broker});
        mptt.authorize({.account = borrower});

        env.close();

        // Fund accounts
        env(pay(issuer, broker, mpt(10'000'000)));
        env(pay(issuer, borrower, mpt(1'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, mpt, broker);
        // Create a loan first (this creates debt)
        auto const keylet = keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(1));
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(mpt(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)));
        env.close();
        // Ensure broker has sufficient cover so brokerPayee == brokerOwner
        // We need coverAvailable >= (debtTotal * coverRateMinimum)
        // Deposit enough cover to ensure the fee goes to broker owner
        // The default coverRateMinimum is 10%, so for a 10,000 loan we need
        // at least 1,000 cover. Default cover is 1,000, so we add more to be
        // safe.
        auto const additionalCover = mpt(50'000).value();
        env(loan_broker::coverDeposit(broker, brokerInfo.brokerID, STAmount{mpt, additionalCover}));
        env.close();
        // Verify broker owner is authorized
        auto const brokerMpt = keylet::mptoken(mptt.issuanceID(), broker);
        BEAST_EXPECT(env.le(brokerMpt) != nullptr);
        // Broker owner unauthorizes.
        // First, pay any positive balance to issuer to zero it out
        auto const brokerBalance = env.balance(broker, mpt);
        env(pay(broker, issuer, brokerBalance));
        env.close();
        // Then, unauthorize the MPT.
        mptt.authorize({.account = broker, .flags = tfMPTUnauthorize});
        env.close();
        // Verify the MPT is unauthorized.
        BEAST_EXPECT(env.le(brokerMpt) == nullptr);
        // Now borrower tries to make a payment
        // We should get a tesSUCCESS instead of a tecNO_AUTH.
        env(pay(borrower, keylet.key, mpt(10'100)), Fee(XRP(100)), Ter(tesSUCCESS));
        env.close();
        // Verify the MPT is still unauthorized.
        BEAST_EXPECT(env.le(brokerMpt) == nullptr);
        // Verify the service fee went to the broker pseudo-account
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            Account const pseudo("pseudo-account", brokerSle->at(sfAccount));
            auto const balance = env.balance(pseudo, mpt);
            // 1,000 default + 50,000 extra + 100 service fee from LoanPay
            BEAST_EXPECTS(balance == mpt(51'100), to_string(json::Value(balance)));
        }
    }

    void
    testLoanPayBrokerOwnerNoPermissionedDomainMPT(FeatureBitset features)
    {
        testcase << "LoanPay Broker Owner without permissioned domain of the MPT";
        using namespace jtx;
        using namespace loan;

        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");

        Env env{*this, features};
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        auto credType = "credential1";

        pdomain::Credentials const credentials1 = {{.issuer = issuer, .credType = credType}};
        env(pdomain::setTx(issuer, credentials1));
        env.close();

        auto domainID = pdomain::getNewDomain(env.meta());

        env(credentials::create(broker, issuer, credType));
        env(credentials::accept(broker, issuer, credType));
        env.close();

        env(credentials::create(borrower, issuer, credType));
        env(credentials::accept(borrower, issuer, credType));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({
            .flags = tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanTransfer | tfMPTCanLock,
            .domainID = domainID,
        });

        PrettyAsset const mpt{mptt.issuanceID()};

        // Authorize broker and borrower
        mptt.authorize({.account = broker});
        mptt.authorize({.account = borrower});

        env.close();

        // Fund accounts
        env(pay(issuer, broker, mpt(10'000'000)));
        env(pay(issuer, borrower, mpt(1'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, mpt, broker);
        // Create a loan first (this creates debt)
        auto const keylet = keylet::loan(brokerInfo.brokerID, SeqProxy::rawSequence(1));
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(mpt(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)));
        env.close();
        // Ensure broker has sufficient cover so brokerPayee == brokerOwner
        // We need coverAvailable >= (debtTotal * coverRateMinimum)
        // Deposit enough cover to ensure the fee goes to broker owner
        // The default coverRateMinimum is 10%, so for a 10,000 loan we need
        // at least 1,000 cover. Default cover is 1,000, so we add more to be
        // safe.
        auto const additionalCover = mpt(50'000).value();
        env(loan_broker::coverDeposit(broker, brokerInfo.brokerID, STAmount{mpt, additionalCover}));
        env.close();
        // Verify broker owner is authorized
        auto const brokerMpt = keylet::mptoken(mptt.issuanceID(), broker);
        BEAST_EXPECT(env.le(brokerMpt) != nullptr);
        // Remove the credentials for the Broker owner.
        // First, pay any positive balance to issuer to zero it out
        auto const brokerBalance = env.balance(broker, mpt);
        env(pay(broker, issuer, brokerBalance));
        env.close();

        env(credentials::deleteCred(broker, broker, issuer, credType));
        env.close();

        // Make sure the broker is not authorized to hold the MPT after we
        // deleted the credentials
        env(pay(issuer, broker, mpt(1'000)), Ter(tecNO_AUTH));

        // Now borrower tries to make a payment
        // We should get a tesSUCCESS instead of a tecNO_AUTH.
        env(pay(borrower, keylet.key, mpt(10'100)), Fee(XRP(100)), Ter(tesSUCCESS));
        env.close();
        // Verify broker is still not authorized
        env(pay(issuer, broker, mpt(1'000)), Ter(tecNO_AUTH));
        // Verify the service fee went to the broker pseudo-account
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            Account const pseudo("pseudo-account", brokerSle->at(sfAccount));
            auto const balance = env.balance(pseudo, mpt);
            // 1,000 default + 50,000 extra + 100 service fee from LoanPay
            BEAST_EXPECTS(balance == mpt(51'100), to_string(json::Value(balance)));
        }
    }

    void
    testLoanSetBrokerOwnerNoPermissionedDomainMPT(FeatureBitset features)
    {
        testcase << "LoanSet Broker Owner without permissioned domain of the MPT";
        using namespace jtx;
        using namespace loan;

        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");

        Env env{*this, features};
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        auto credType = "credential1";

        pdomain::Credentials const credentials1{{.issuer = issuer, .credType = credType}};
        env(pdomain::setTx(issuer, credentials1));
        env.close();

        auto domainID = pdomain::getNewDomain(env.meta());

        // Add credentials for the broker and borrower
        env(credentials::create(broker, issuer, credType));
        env(credentials::accept(broker, issuer, credType));
        env.close();

        env(credentials::create(borrower, issuer, credType));
        env(credentials::accept(borrower, issuer, credType));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({
            .flags = tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanTransfer | tfMPTCanLock,
            .domainID = domainID,
        });

        PrettyAsset const mpt{mptt.issuanceID()};

        // Authorize broker and borrower
        mptt.authorize({.account = broker});
        mptt.authorize({.account = borrower});
        env.close();

        // Fund accounts
        env(pay(issuer, broker, mpt(10'000'000)));
        env(pay(issuer, borrower, mpt(1'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, mpt, broker);

        // Remove the credentials for the Broker owner.
        // Clear the balance first.
        auto const brokerBalance = env.balance(broker, mpt);
        env(pay(broker, issuer, brokerBalance));
        env.close();
        // Delete the credentials
        env(credentials::deleteCred(broker, broker, issuer, credType));
        env.close();

        // Create a loan, this should fail for tecNO_AUTH
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(mpt(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)),
            Ter(tecNO_AUTH));
        env.close();
    }

    void
    runAmendmentIndependent()
    {
        testServiceFeeOnBrokerDeepFreeze();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testSequentialFLCDepletion(features);
        testWithdrawReflectsUnrealizedLoss(features);
        testLoanPayBrokerOwnerMissingTrustline(features);
        testLoanPayBrokerOwnerUnauthorizedMPT(features);
        testLoanPayBrokerOwnerNoPermissionedDomainMPT(features);
        testLoanSetBrokerOwnerNoPermissionedDomainMPT(features);
    }

public:
    void
    run() override
    {
        runAmendmentIndependent();
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2},
                 all_))
            runAmendmentSensitive(features);
    }
};

BEAST_DEFINE_TESTSUITE(LoanCoverFreezeAuth, tx, xrpl);

}  // namespace xrpl::test
