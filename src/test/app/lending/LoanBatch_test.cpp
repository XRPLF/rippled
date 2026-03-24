#include <xrpl/beast/unit_test/suite.h>
//
#include <test/app/lending/LoanBase.h>
#include <test/jtx.h>

#include <xrpl/protocol/SField.h>

#include <chrono>

namespace xrpl {
namespace test {

class LoanBatch_test : public LoanBase
{
protected:
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
            using namespace loan;
            using namespace std::chrono_literals;

            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const keylet = keylet::loanbroker(alice, env.seq(alice));
            auto const loanKeylet = keylet::loan(keylet.key, env.seq(alice));

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);

            auto const batchFee = batch::calcBatchFee(env, 1, 4);

            auto loanSet = set(alice, keylet.key, Number(10000));
            loanSet[sfCounterparty] = bob.human();
            auto const batchTxn = env.jt(
                batch::outer(bob, bobSeq, batchFee, tfAllOrNothing),
                batch::inner(loanSet, aliceSeq),
                batch::inner(del(alice, loanKeylet.key), aliceSeq + 1),
                batch::inner(manage(alice, loanKeylet.key, tfLoanImpair), aliceSeq + 2),
                batch::inner(pay(alice, loanKeylet.key, XRP(500)), aliceSeq + 3),
                batch::sig(alice));
            env(batchTxn, ter(temINVALID_INNER_BATCH));
        };
        failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol);
    }

    void
    testCreateAsset()
    {
        testcase("CreateAsset");
        // Checks if a single asset vault can be created in a batch.

        using namespace jtx;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const broker{"broker"};
        Account const borrower{"borrower"};

        auto const IOU = issuer["IOU"];

        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        env(trust(broker, IOU(20'000'000)));
        env(pay(issuer, broker, IOU(10'000'000)));
        env.close();

        auto const brokerSeq = env.seq(broker);
        // The starting sequence should be brokerSeq + 1 because the batch
        // outer transaction will consume the first sequence.
        auto const txns = createVaultAndBrokerTransactions(
            env, IOU, broker, BrokerParameters::defaults(), brokerSeq + 1);

        auto const batchFee = batch::calcBatchFee(env, 0, 4);

        auto const batchTxn = env.jt(
            batch::outer(broker, brokerSeq, batchFee, tfAllOrNothing),
            batch::inner(txns.vaultCreateTx, brokerSeq + 1),
            batch::inner(txns.vaultDepositTx, brokerSeq + 2),
            batch::inner(txns.brokerSetTx, brokerSeq + 3),
            batch::inner(*txns.coverDepositTx, brokerSeq + 4));

        env(batchTxn);
        env.close();

        checkVaultAndBroker(env, txns);
    }

    void
    testLoanSetAndDelete()
    {
        testcase("LoanSetAndDelete");
        // Checks if LoanSet works in a batch.

        using namespace jtx;
        using namespace jtx::loan;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const broker{"broker"};
        Account const borrower{"borrower"};

        auto const IOU = issuer["IOU"];

        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        env(trust(broker, IOU(20'000'000)));
        env(pay(issuer, broker, IOU(10'000'000)));
        env.close();
        auto const brokerInfo = createVaultAndBroker(env, IOU, broker);

        LoanParameters const loanParams{
            .account = broker,
            .counter = borrower,
            .principalRequest = Number{100},
            .interest = TenthBips32{1922},
            .payTotal = 5816,
            .payInterval = 86400 * 6,
            .gracePd = 86400 * 5,
        };

        auto const loanSetTx = loanParams.getTransaction(env, brokerInfo);

        // Get the loan keylet that will be created by the LoanSet
        auto const brokerSeq = env.seq(broker);

        // The loan keylet is based on the broker's LoanSequence
        auto const brokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const loanSequence = brokerSle->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        // Create the loan delete transaction
        auto const loanDelTx = del(broker, loanKeylet.key);

        // Calculate batch fee: 1 signer (borrower) + 2 transactions
        auto const batchFee = batch::calcBatchFee(env, 1, 2);

        // Create the batch transaction with both LoanSet and LoanDelete
        auto const batchTxn = env.jt(
            batch::outer(broker, brokerSeq, batchFee, tfAllOrNothing),
            batch::inner(loanSetTx, brokerSeq + 1),
            batch::inner(loanDelTx, brokerSeq + 2),
            batch::sig(borrower));

        env(batchTxn);
        env.close();

        // Verify the loan is not there.
        BEAST_EXPECT(!env.le(loanKeylet));
    }

    void
    testLoanSetAndImpair()
    {
        testcase("LoanSetAndManage");
        // Creates a loan and impairs it in a single batch.
        // When a loan is impaired, NextPaymentDueDate is set to currentTime.

        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const broker{"broker"};
        Account const borrower{"borrower"};

        auto const IOU = issuer["IOU"];

        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        env(trust(broker, IOU(20'000'000)));
        env(pay(issuer, broker, IOU(10'000'000)));
        env.close();
        auto const brokerInfo = createVaultAndBroker(env, IOU, broker);

        LoanParameters const loanParams{
            .account = broker,
            .counter = borrower,
            .principalRequest = Number{100},
            .interest = TenthBips32{1922},
            .payTotal = 5816,
            .payInterval = 86400 * 6,  // 6 days
            .gracePd = 86400 * 5,      // 5 days grace period
        };

        auto const loanSetTx = loanParams.getTransaction(env, brokerInfo);

        // Get the loan keylet that will be created by the LoanSet
        auto const brokerSeq = env.seq(broker);

        // The loan keylet is based on the broker's LoanSequence
        auto const brokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const loanSequence = brokerSle->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        // Create the manage transaction to impair the loan
        auto const impairTx = manage(broker, loanKeylet.key, tfLoanImpair);

        // Calculate batch fee: 1 signer (borrower) + 2 transactions
        auto const batchFee = batch::calcBatchFee(env, 1, 2);

        // Create the batch transaction with LoanSet and impair
        auto const batchTxn = env.jt(
            batch::outer(broker, brokerSeq, batchFee, tfAllOrNothing),
            batch::inner(loanSetTx, brokerSeq + 1),
            batch::inner(impairTx, brokerSeq + 2),
            batch::sig(borrower));

        auto currentTime = env.now().time_since_epoch().count();
        env(batchTxn);
        env.close();

        // Verify the loan was created and impaired
        auto const finalLoanSle = env.le(loanKeylet);
        BEAST_EXPECT(finalLoanSle);
        BEAST_EXPECT(finalLoanSle->isFlag(lsfLoanImpaired));
        // When impaired, NextPaymentDueDate should be set to current time
        BEAST_EXPECT(finalLoanSle->at(sfNextPaymentDueDate) == currentTime);

        // Verify Vault.LossUnrealized was increased
        auto const finalBrokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        BEAST_EXPECT(finalBrokerSle);
        if (finalBrokerSle)
        {
            auto const vaultKeylet = keylet::vault(finalBrokerSle->at(sfVaultID));
            auto const vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle);
            if (vaultSle)
            {
                // LossUnrealized = TotalValueOutstanding - ManagementFeeOutstanding
                auto const expectedLoss = finalLoanSle->at(sfTotalValueOutstanding) -
                    finalLoanSle->at(sfManagementFeeOutstanding);
                BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == expectedLoss);
            }
        }
    }

    void
    testLoanDefaultWithdrawAndPay()
    {
        testcase("LoanDefaultWithdrawAndPay");
        // Creates a loan, advances time to make it defaultable, then in a batch:
        // defaults the loan, withdraws the DefaultCovered amount, and makes a payment.

        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const broker{"broker"};
        Account const borrower{"borrower"};
        Account const recipient{"recipient"};

        auto const IOU = issuer[iouCurrency];

        env.fund(XRP(20'000), issuer, broker, borrower, recipient);
        env.close();

        env(trust(broker, IOU(20'000'000)));
        env(trust(recipient, IOU(20'000'000)));
        env(pay(issuer, broker, IOU(10'000'000)));
        env.close();

        // Create vault and broker with specific parameters to ensure DefaultCovered > vault
        // available
        BrokerParameters brokerParams = BrokerParameters::defaults();
        brokerParams.vaultDeposit = 100;   // Small vault deposit
        brokerParams.coverDeposit = 1000;  // Large cover deposit
        auto const brokerInfo = createVaultAndBroker(env, IOU, broker, brokerParams);

        LoanParameters const loanParams{
            .account = broker,
            .counter = borrower,
            .principalRequest = Number{100},
            .interest = TenthBips32{1922},
            .payTotal = 5816,
            .payInterval = 86400 * 6,  // 6 days
            .gracePd = 86400 * 5,      // 5 days grace period
        };

        // Create the loan first (not in batch)
        env(loanParams(env, brokerInfo));
        env.close();

        // Get the loan keylet
        auto const brokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const loanSequence = brokerSle->at(sfLoanSequence) - 1;
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        // Verify loan exists
        BEAST_EXPECT(env.le(loanKeylet));

        // Advance time past the payment due date + grace period to make the loan defaultable
        auto const loanSle = env.le(loanKeylet);
        auto const nextPaymentDue = loanSle->at(sfNextPaymentDueDate);
        auto const gracePeriod = loanSle->at(sfGracePeriod);
        env.close(std::chrono::seconds{nextPaymentDue + gracePeriod + 60});

        // Calculate DefaultCovered before defaulting
        auto const totalValue = loanSle->at(sfTotalValueOutstanding);
        auto const managementFee = loanSle->at(sfManagementFeeOutstanding);
        auto const defaultAmount = totalValue - managementFee;

        auto const debtTotal = brokerSle->at(sfDebtTotal);
        auto const coverRateMin = TenthBips32{brokerSle->at(sfCoverRateMinimum)};
        auto const coverRateLiq = TenthBips32{brokerSle->at(sfCoverRateLiquidation)};
        auto const coverAvailable = brokerSle->at(sfCoverAvailable);

        // MinimumCover = DebtTotal x CoverRateMinimum
        Number const minimumCover = debtTotal * coverRateMin.value() / tenthBipsPerUnity.value();
        // DefaultCovered = min(MinimumCover x CoverRateLiquidation, DefaultAmount, CoverAvailable)
        Number const defaultCovered = std::min(
            {minimumCover * coverRateLiq.value() / tenthBipsPerUnity.value(),
             defaultAmount,
             coverAvailable});

        // Get vault available before default
        auto const vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
        auto const vaultAvailableBefore = vaultSle->at(sfAssetsAvailable);

        // Verify DefaultCovered will be greater than vault available
        BEAST_EXPECT(defaultCovered > vaultAvailableBefore);

        // Now batch: default, withdraw DefaultCovered, and make a payment
        auto const brokerSeq = env.seq(broker);

        auto const defaultTx = manage(broker, loanKeylet.key, tfLoanDefault);

        // Withdraw the DefaultCovered amount from vault
        Vault vault{env};
        auto const withdrawTx = vault.withdraw(
            {.depositor = broker,
             .id = brokerInfo.vaultID,
             .amount = brokerInfo.asset(defaultCovered)});

        // Make a payment to recipient
        auto const paymentTx = pay(broker, recipient, brokerInfo.asset(defaultCovered / 2));

        // Calculate batch fee: 0 signers (broker signs outer) + 3 transactions
        auto const batchFee = batch::calcBatchFee(env, 0, 3);

        // Create the batch transaction
        auto const batchTxn = env.jt(
            batch::outer(broker, brokerSeq, batchFee, tfAllOrNothing),
            batch::inner(defaultTx, brokerSeq + 1),
            batch::inner(withdrawTx, brokerSeq + 2),
            batch::inner(paymentTx, brokerSeq + 3));

        env(batchTxn);
        env.close();

        // Verify the loan is defaulted
        auto const finalLoanSle = env.le(loanKeylet);
        BEAST_EXPECT(finalLoanSle);
        if (finalLoanSle)
        {
            BEAST_EXPECT(finalLoanSle->isFlag(lsfLoanDefault));
            BEAST_EXPECT(finalLoanSle->at(sfPaymentRemaining) == 0);
            BEAST_EXPECT(finalLoanSle->at(sfTotalValueOutstanding) == 0);
            BEAST_EXPECT(finalLoanSle->at(sfPrincipalOutstanding) == 0);
            BEAST_EXPECT(finalLoanSle->at(sfManagementFeeOutstanding) == 0);
            BEAST_EXPECT(finalLoanSle->at(sfNextPaymentDueDate) == 0);
        }

        // Verify vault state after default and withdrawal
        auto const finalVaultSle = env.le(keylet::vault(brokerInfo.vaultID));
        BEAST_EXPECT(finalVaultSle);
        if (finalVaultSle)
        {
            // Vault should have received DefaultCovered, then withdrawn it
            auto const expectedAvailable = vaultAvailableBefore + defaultCovered - defaultCovered;
            BEAST_EXPECT(finalVaultSle->at(sfAssetsAvailable) == expectedAvailable);
        }

        // Verify recipient received payment
        BEAST_EXPECT(env.balance(recipient, IOU) == brokerInfo.asset(defaultCovered / 2));
    }

    void
    testRiskFreeArbitrage()
    {
        testcase("RiskFreeArbitrage");
        // Demonstrates risk-free arbitrage using batch transactions:
        // 1. Borrow funds from a loan
        // 2. Buy XRP at one price
        // 3. Sell XRP at a higher price
        // 4. Repay the loan with interest
        // All in a single atomic batch - if any step fails, everything reverts.

        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const broker{"broker"};
        Account const borrower{"borrower"};
        Account const marketMaker1{"mm1"};  // Sells XRP at $2.50
        Account const marketMaker2{"mm2"};  // Buys XRP at $2.52

        auto const IOU = issuer[iouCurrency];

        env.fund(XRP(50'000), issuer, broker, borrower, marketMaker1, marketMaker2);
        env.close();

        // Set up trust lines
        env(trust(broker, IOU(20'000'000)));
        env(trust(borrower, IOU(20'000'000)));
        env(trust(marketMaker1, IOU(20'000'000)));
        env(trust(marketMaker2, IOU(20'000'000)));
        env.close();

        // Fund accounts
        env(pay(issuer, broker, IOU(15'000'000)));
        env(pay(issuer, marketMaker1, IOU(11'000'000)));
        env(pay(issuer, marketMaker2, IOU(11'000'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, IOU, broker);

        // Get the loan keylet BEFORE creating the loan
        auto const brokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const loanSequence = brokerSle->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        // Market makers create offers:
        // MM1: Sells 400 XRP for $1000 (price: $2.50 per XRP)
        // MM2: Buys 400 XRP for $1008 (price: $2.52 per XRP)
        env(offer(marketMaker1, IOU(1000), XRP(400)));
        env(offer(marketMaker2, XRP(400), IOU(1008)));
        env.close();

        // Record vault state before batch
        auto const initialVaultSle = env.le(keylet::vault(brokerInfo.vaultID));
        auto const initialVaultPseudoAccountID = initialVaultSle->at(sfAccount);
        Account const initialVaultPseudoAccount("VaultPseudo", initialVaultPseudoAccountID);
        auto const initialVaultBalance = env.balance(initialVaultPseudoAccount, IOU);
        auto const initialAssetsAvailable = initialVaultSle->at(sfAssetsAvailable);

        // Now create the arbitrage batch - ALL IN ONE ATOMIC TRANSACTION:
        // 1. Create loan (borrow $1000)
        // 2. Buy 400 XRP at $2.50 (cost: $1000)
        // 3. Sell 400 XRP at $2.52 (revenue: $1008)
        // 4. Repay loan principal + interest ($1000 + $1 = $1001)
        // Profit: $1008 - $1001 = $7

        auto const borrowerSeq = env.seq(borrower);

        // Transaction 1: Create the loan (borrower receives $1000)
        LoanParameters const loanParams{
            .account = borrower,
            .counter = broker,
            .principalRequest = Number{1000},
            .interest = TenthBips32{100},  // 0.1% interest
            .payTotal = 1,
            .payInterval = 86400,  // 1 day
            .gracePd = 86400,      // 1 day grace period
        };
        auto const loanSetTx = loanParams.getTransaction(env, brokerInfo);

        // Transaction 2: Buy 400 XRP for $1000 from MM1
        auto const buyTx = offer(borrower, XRP(400), IOU(1000));

        // Transaction 3: Sell 400 XRP for $1008 to MM2
        auto const sellTx = offer(borrower, IOU(1008), XRP(400));

        // Transaction 4: Repay the loan
        // We know the total will be $1001 (principal $1000 + interest $1)
        auto const payTx = pay(borrower, loanKeylet.key, IOU(1001));

        // Calculate batch fee: 1 signer (broker as counterparty) + 4 transactions
        auto const batchFee = batch::calcBatchFee(env, 1, 4);

        // Create the batch transaction with tfAllOrNothing
        auto const batchTxn = env.jt(
            batch::outer(borrower, borrowerSeq, batchFee, tfAllOrNothing),
            batch::inner(loanSetTx, borrowerSeq + 1),
            batch::inner(buyTx, borrowerSeq + 2),
            batch::inner(sellTx, borrowerSeq + 3),
            batch::inner(payTx, borrowerSeq + 4),
            batch::sig(broker));

        env(batchTxn);
        env.close();

        // Verify the arbitrage was successful:
        // 1. Borrower should have profit (~$7)
        // 2. Loan should be fully paid
        // 3. Market makers' offers should be consumed
        // 4. Vault should have received the interest payment

        auto const finalLoanSle = env.le(loanKeylet);
        BEAST_EXPECT(finalLoanSle);
        if (finalLoanSle)
        {
            // Loan should be fully paid (or nearly so, depending on rounding)
            BEAST_EXPECT(finalLoanSle->at(sfTotalValueOutstanding) < IOU(1).value());
        }

        // Borrower should have profit:
        // Started with $1000 from loan, spent $1000 on XRP, earned $1008 from selling XRP, paid
        // $1001 to repay loan Net: $1000 - $1000 + $1008 - $1001 = $7 profit
        auto const borrowerBalance = env.balance(borrower, IOU);
        BEAST_EXPECT(borrowerBalance > IOU(6) && borrowerBalance < IOU(8));

        // Verify offers were consumed
        env.require(offers(marketMaker1, 0));
        env.require(offers(marketMaker2, 0));

        // Verify vault received the interest payment
        // The vault should have received $1001 (principal $1000 + interest $1)
        auto const finalVaultSle = env.le(keylet::vault(brokerInfo.vaultID));
        BEAST_EXPECT(finalVaultSle);
        if (finalVaultSle)
        {
            auto const finalVaultPseudoAccountID = finalVaultSle->at(sfAccount);
            BEAST_EXPECT(finalVaultPseudoAccountID == initialVaultPseudoAccountID);

            // Check vault pseudo-account balance increased
            auto const finalVaultBalance = env.balance(initialVaultPseudoAccount, IOU);
            auto const vaultBalanceIncrease = finalVaultBalance - initialVaultBalance;
            BEAST_EXPECT(vaultBalanceIncrease > IOU(0) && vaultBalanceIncrease < IOU(3));

            // Check AssetsAvailable increased
            auto const finalAssetsAvailable = finalVaultSle->at(sfAssetsAvailable);
            auto const assetsAvailableIncrease = finalAssetsAvailable - initialAssetsAvailable;
            BEAST_EXPECT(
                assetsAvailableIncrease > Number{0} && assetsAvailableIncrease < Number{2});
        }
    }

    void
    testRiskFreeArbitrageFails()
    {
        testcase("RiskFreeArbitrageFails");
        // Demonstrates that batch transactions with tfAllOrNothing revert completely
        // when any transaction fails. In this case, we make it impossible to sell
        // the XRP at the higher price, which causes the entire batch to fail,
        // including the loan creation.

        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const broker{"broker"};
        Account const borrower{"borrower"};
        Account const marketMaker1{"mm1"};  // Sells XRP at $2.50

        auto const IOU = issuer[iouCurrency];

        env.fund(XRP(50'000), issuer, broker, borrower, marketMaker1);
        env.close();

        // Set up trust lines
        env(trust(broker, IOU(20'000'000)));
        env(trust(borrower, IOU(20'000'000)));
        env(trust(marketMaker1, IOU(20'000'000)));
        env.close();

        // Fund accounts
        env(pay(issuer, broker, IOU(15'000'000)));
        env(pay(issuer, marketMaker1, IOU(11'000'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, IOU, broker);

        // Get the loan keylet BEFORE creating the loan
        auto const brokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const loanSequence = brokerSle->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        // Market maker creates only ONE offer:
        // MM1: Sells 400 XRP for $1000 (price: $2.50 per XRP)
        // NOTE: No MM2 offer to buy XRP at higher price!
        env(offer(marketMaker1, IOU(1000), XRP(400)));
        env.close();

        // Record initial state
        auto const initialBorrowerBalance = env.balance(borrower, IOU);
        auto const initialBrokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const initialLoanSequence = initialBrokerSle->at(sfLoanSequence);

        // Record vault state before batch
        auto const initialVaultSle = env.le(keylet::vault(brokerInfo.vaultID));
        auto const initialVaultPseudoAccountID = initialVaultSle->at(sfAccount);
        Account const initialVaultPseudoAccount("VaultPseudo", initialVaultPseudoAccountID);
        auto const initialVaultBalance = env.balance(initialVaultPseudoAccount, IOU);
        auto const initialAssetsAvailable = initialVaultSle->at(sfAssetsAvailable);

        // Try to create the arbitrage batch - this should FAIL:
        // 1. Create loan (borrow $1000)
        // 2. Buy 400 XRP at $2.50 (cost: $1000) - succeeds
        // 3. Sell 400 XRP at $2.52 (revenue: $1008) - FAILS (no offer available)
        // 4. Repay loan principal + interest ($1000 + $1 = $1001)
        // Because of tfAllOrNothing, the entire batch should revert!

        auto const borrowerSeq = env.seq(borrower);

        // Transaction 1: Create the loan (borrower receives $1000)
        LoanParameters const loanParams{
            .account = borrower,
            .counter = broker,
            .principalRequest = Number{1000},
            .interest = TenthBips32{100},  // 0.1% interest
            .payTotal = 1,
            .payInterval = 86400,  // 1 day
            .gracePd = 86400,      // 1 day grace period
        };
        auto const loanSetTx = loanParams.getTransaction(env, brokerInfo);

        // Transaction 2: Buy 400 XRP for $1000 from MM1
        auto const buyTx = offer(borrower, XRP(400), IOU(1000));
        buyTx[jss::Flags] = tfFillOrKill;

        // Transaction 3: Try to sell 400 XRP for $1008 - this will FAIL
        auto const sellTx = offer(borrower, XRP(400), IOU(1008));
        sellTx[jss::Flags] = tfFillOrKill;

        // Transaction 4: Repay the loan
        auto const payTx = pay(borrower, loanKeylet.key, IOU(1001));

        // Calculate batch fee: 1 signer (broker as counterparty) + 4 transactions
        auto const batchFee = batch::calcBatchFee(env, 1, 4);

        // Create the batch transaction with tfAllOrNothing
        auto const batchTxn = env.jt(
            batch::outer(borrower, borrowerSeq, batchFee, tfAllOrNothing),
            batch::inner(loanSetTx, borrowerSeq + 1),
            batch::inner(buyTx, borrowerSeq + 2),
            batch::inner(sellTx, borrowerSeq + 3),
            batch::inner(payTx, borrowerSeq + 4),
            batch::sig(broker));

        env(batchTxn);
        env.close();

        // Verify that EVERYTHING was reverted:
        // 1. Loan was NOT created
        // 2. Borrower balance unchanged (except for fee)
        // 3. LoanSequence unchanged
        // 4. Market maker's offer still exists
        // 5. Vault received NOTHING

        // Loan should NOT exist
        auto const finalLoanSle = env.le(loanKeylet);
        BEAST_EXPECT(!finalLoanSle);

        // Borrower balance should be unchanged (except for batch fee)
        auto const finalBorrowerBalance = env.balance(borrower, IOU);
        BEAST_EXPECT(finalBorrowerBalance == initialBorrowerBalance);

        // LoanSequence should be unchanged (loan was never created)
        auto const finalBrokerSle = env.le(keylet::loanbroker(brokerInfo.brokerID));
        auto const finalLoanSequence = finalBrokerSle->at(sfLoanSequence);
        BEAST_EXPECT(finalLoanSequence == initialLoanSequence);

        // Market maker's offer should still exist (not consumed)
        env.require(offers(marketMaker1, 1));

        // Verify vault received NOTHING (no loan was created, so no repayment)
        auto const finalVaultSle = env.le(keylet::vault(brokerInfo.vaultID));
        BEAST_EXPECT(finalVaultSle);
        if (finalVaultSle)
        {
            auto const finalVaultPseudoAccountID = finalVaultSle->at(sfAccount);
            BEAST_EXPECT(finalVaultPseudoAccountID == initialVaultPseudoAccountID);

            // Vault pseudo-account balance should be unchanged
            auto const finalVaultBalance = env.balance(initialVaultPseudoAccount, IOU);
            BEAST_EXPECT(finalVaultBalance == initialVaultBalance);

            // AssetsAvailable should be unchanged
            auto const finalAssetsAvailable = finalVaultSle->at(sfAssetsAvailable);
            BEAST_EXPECT(finalAssetsAvailable == initialAssetsAvailable);
        }
    }

public:
    void
    run() override
    {
        testDisabled();
        testCreateAsset();
        testLoanSetAndDelete();
        testLoanSetAndImpair();
        testLoanDefaultWithdrawAndPay();
        testRiskFreeArbitrage();
        testRiskFreeArbitrageFails();
    }
};

BEAST_DEFINE_TESTSUITE(LoanBatch, tx, xrpl);

}  // namespace test
}  // namespace xrpl
