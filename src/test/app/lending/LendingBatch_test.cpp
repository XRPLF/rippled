#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
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

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace xrpl::test {

// Vault and Lending transactions used as Batch inners under LendingProtocolV1_2.
class LendingBatch_test : public LoanTestBase
{
private:
    // Asserts a loan has been paid off in full: no principal, value, or
    // remaining payments left on the loan; no debt left on the broker; and the
    // vault's available assets back to `depositValue`.
    void
    assertLoanFullyRepaid(
        jtx::Env const& env,
        Keylet const& brokerKeylet,
        Keylet const& vaultKeylet,
        Keylet const& loanKeylet,
        STAmount const& depositValue)
    {
        if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
        {
            BEAST_EXPECT(loanSle->at(sfPrincipalOutstanding) == 0);
            BEAST_EXPECT(loanSle->at(sfTotalValueOutstanding) == 0);
            BEAST_EXPECT(loanSle->at(sfPaymentRemaining) == 0);
        }
        if (auto const brokerSle = env.le(brokerKeylet); BEAST_EXPECT(brokerSle))
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);
        if (auto const vaultSle = env.le(vaultKeylet); BEAST_EXPECT(vaultSle))
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == depositValue);
    }

    // LoanSet as a batch inner: the counterparty signs the outer batch, so
    // this inner carries no signature of its own. Pass paymentTotal >= 2
    // wherever the same batch also submits an early tfLoanFullPayment:
    // computeFullPayment refuses it with tecKILLED when only the final
    // scheduled payment is left.
    static json::Value
    loanSetInner(
        jtx::Env& env,
        jtx::Account const& account,
        uint256 const& brokerID,
        jtx::Account const& counterparty,
        STAmount const& principal,
        std::uint32_t paymentTotal)
    {
        using namespace jtx;
        using namespace jtx::loan;

        return env.json(
            set(account, brokerID, principal.value()),
            kCounterparty(counterparty.id()),
            kPaymentTotal(paymentTotal),
            Sig(kNone),
            Fee(kNone),
            Seq(kNone));
    }

    // Runs VaultCreate -> Deposit -> Withdraw -> Delete in one tfAllOrNothing
    // batch and checks the vault is gone, the owner paid only the batch fee,
    // and both owner count and sequence are back where they belong.
    void
    runVaultLifecycleBatch(jtx::Env& env, jtx::Account const& owner, json::Value const& createTx)
    {
        using namespace jtx;

        Vault const vault{env};

        auto const seq = env.seq(owner);
        auto const balanceBefore = env.balance(owner);
        auto const ownerCountBefore = env.ownerCount(owner);

        // The outer Batch consumes owner's current sequence, so the first
        // inner (VaultCreate) lands on seq + 1.
        auto const vaultKeylet = keylet::vault(owner.id(), SeqProxy::rawSequence(seq + 1));
        auto const depositAmount = XRP(1'000);

        auto const batchFee = batch::calcBatchFee(env, 0, 4);
        env(batch::outer(owner, seq, batchFee, tfAllOrNothing),
            batch::Inner(createTx, seq + 1),
            batch::Inner(
                vault.deposit({.depositor = owner, .id = vaultKeylet.key, .amount = depositAmount}),
                seq + 2),
            batch::Inner(
                vault.withdraw(
                    {.depositor = owner, .id = vaultKeylet.key, .amount = depositAmount}),
                seq + 3),
            batch::Inner(vault.del({.owner = owner, .id = vaultKeylet.key}), seq + 4),
            Ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(!env.le(vaultKeylet));
        BEAST_EXPECT(env.balance(owner) == balanceBefore - batchFee);
        BEAST_EXPECT(env.ownerCount(owner) == ownerCountBefore);
        BEAST_EXPECT(env.seq(owner) == seq + 5);
    }

    void
    testVaultLifecycle(FeatureBitset features)
    {
        // A single tfAllOrNothing batch runs an open-ended XRP vault through
        // its whole lifecycle: create, deposit, withdraw everything back out,
        // and delete.
        testcase("vault lifecycle in a batch");

        using namespace jtx;

        Env env(*this, features);

        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        auto const createTx = std::get<0>(Vault{env}.create({.owner = owner, .asset = xrpIssue()}));
        runVaultLifecycleBatch(env, owner, createTx);
    }

    void
    testClosedEndedVaultLifecycle(FeatureBitset features)
    {
        // Same lifecycle as testVaultLifecycle, but on a closed-ended vault
        // submitted while it is still in the Subscription phase, where
        // LendingProtocolV1_1 allows both deposit and withdraw.
        testcase("vault lifecycle in a batch, closed-ended vault, subscription phase");

        using namespace jtx;

        Env env(*this, features);

        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        auto const createTx = std::get<0>(Vault{env}.createClosedEnded(
            {.owner = owner, .asset = xrpIssue(), .subscriptionOffset = std::chrono::seconds{60}}));
        runVaultLifecycleBatch(env, owner, createTx);
    }

    void
    testLoanLifecycleOpenEndedVault(FeatureBitset features)
    {
        // With LendingProtocolV1_1 off, an open-ended vault can host a broker,
        // so vault creation, deposit, broker setup, loan origination, and an
        // early full payoff all fit in one atomic batch. The borrower signs the
        // outer as counterparty to the LoanSet inner and as the account behind
        // the LoanPay inner.
        testcase("loan lifecycle in a batch, open-ended vault");

        using namespace jtx;

        Env env(*this, features);

        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1'000'000};
        Vault const vault{env};

        auto const lenderSeq = env.seq(lender);
        auto const borrowerSeq = env.seq(borrower);
        auto const lenderBalanceBefore = env.balance(lender);
        auto const borrowerBalanceBefore = env.balance(borrower);

        auto const depositAmount = asset(50'000);
        auto const principal = asset(1'000);

        auto const vaultKeylet = keylet::vault(lender.id(), SeqProxy::rawSequence(lenderSeq + 1));
        auto const brokerKeylet =
            keylet::loanBroker(lender.id(), SeqProxy::rawSequence(lenderSeq + 3));
        auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));

        auto const createTx = std::get<0>(vault.create({.owner = lender, .asset = xrpIssue()}));

        // CoverRateMinimum/CoverRateLiquidation are left unset on the
        // LoanBrokerSet inner below, which defaults both to zero, so this
        // loan needs no first-loss cover deposit from the broker.
        auto const loanSetTx = loanSetInner(env, lender, brokerKeylet.key, borrower, principal, 2);
        auto const loanPayTx =
            loan::pay(borrower, loanKeylet.key, principal.value(), tfLoanFullPayment);

        auto const batchFee = batch::calcBatchFee(env, 1, 5);
        env(batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
            batch::Inner(createTx, lenderSeq + 1),
            batch::Inner(
                vault.deposit(
                    {.depositor = lender, .id = vaultKeylet.key, .amount = depositAmount.value()}),
                lenderSeq + 2),
            batch::Inner(loan_broker::set(lender, vaultKeylet.key), lenderSeq + 3),
            batch::Inner(loanSetTx, lenderSeq + 4),
            batch::Inner(loanPayTx, borrowerSeq),
            batch::Sig(borrower),
            Ter(tesSUCCESS));
        env.close();

        assertLoanFullyRepaid(env, brokerKeylet, vaultKeylet, loanKeylet, depositAmount.value());

        // The borrower borrowed the principal and repaid it in full at zero
        // interest and fees, so their balance nets to zero; the batch fee is
        // paid entirely by the lender, who also funded the vault deposit.
        BEAST_EXPECT(env.balance(borrower) == borrowerBalanceBefore);
        BEAST_EXPECT(env.balance(lender) == lenderBalanceBefore - batchFee - depositAmount.value());
    }

    void
    testLoanLifecycleClosedEndedVault(FeatureBitset features)
    {
        // Under LendingProtocolV1_1 a broker can attach only to a closed-ended
        // vault, and LoanSet is refused outside the Investment phase, so the
        // vault/broker setup and the loan/payoff run as two batches split at
        // the Subscription -> Investment boundary.
        testcase("loan lifecycle in a batch, closed-ended vault");

        using namespace jtx;

        Env env(*this, features);

        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1'000'000};
        Vault const vault{env};

        auto const lenderSeq1 = env.seq(lender);
        auto const depositAmount = asset(50'000);

        auto const vaultKeylet = keylet::vault(lender.id(), SeqProxy::rawSequence(lenderSeq1 + 1));
        auto const brokerKeylet =
            keylet::loanBroker(lender.id(), SeqProxy::rawSequence(lenderSeq1 + 3));

        auto const created = vault.createClosedEnded(
            {.owner = lender, .asset = xrpIssue(), .subscriptionOffset = std::chrono::seconds{60}});
        auto const createTx = std::get<0>(created);
        auto const subscriptionDate = std::get<2>(created);

        // LoanBrokerSet checks only the vault's kind, not its phase, so setting
        // up the broker is allowed here in the same batch, while the vault is
        // still in the Subscription phase.
        auto const batch1Fee = batch::calcBatchFee(env, 0, 3);
        env(batch::outer(lender, lenderSeq1, batch1Fee, tfAllOrNothing),
            batch::Inner(createTx, lenderSeq1 + 1),
            batch::Inner(
                vault.deposit(
                    {.depositor = lender, .id = vaultKeylet.key, .amount = depositAmount.value()}),
                lenderSeq1 + 2),
            batch::Inner(loan_broker::set(lender, vaultKeylet.key), lenderSeq1 + 3),
            Ter(tesSUCCESS));
        env.close();

        if (auto const vaultSle = env.le(vaultKeylet); BEAST_EXPECT(vaultSle))
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == depositAmount.value());
        BEAST_EXPECT(env.le(brokerKeylet));

        vault.closePastSubscription(subscriptionDate);

        auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));
        auto const principal = asset(1'000);

        auto const lenderSeq2 = env.seq(lender);
        auto const borrowerSeq = env.seq(borrower);
        auto const lenderBalanceBefore = env.balance(lender);
        auto const borrowerBalanceBefore = env.balance(borrower);

        auto const loanSetTx = loanSetInner(env, lender, brokerKeylet.key, borrower, principal, 2);
        auto const loanPayTx =
            loan::pay(borrower, loanKeylet.key, principal.value(), tfLoanFullPayment);

        auto const batch2Fee = batch::calcBatchFee(env, 1, 2);
        env(batch::outer(lender, lenderSeq2, batch2Fee, tfAllOrNothing),
            batch::Inner(loanSetTx, lenderSeq2 + 1),
            batch::Inner(loanPayTx, borrowerSeq),
            batch::Sig(borrower),
            Ter(tesSUCCESS));
        env.close();

        assertLoanFullyRepaid(env, brokerKeylet, vaultKeylet, loanKeylet, depositAmount.value());

        BEAST_EXPECT(env.balance(borrower) == borrowerBalanceBefore);
        // The deposit already left the lender's balance in batch 1; batch 2
        // only costs the batch fee, since LoanSet/LoanPay net to zero XRP.
        BEAST_EXPECT(env.balance(lender) == lenderBalanceBefore - batch2Fee);
    }

    // Handles shared by testArbitrage and testArbitrageRollback.
    struct ArbitrageSetup
    {
        jtx::PrettyAsset iou;
        jtx::Account issuer;
        jtx::Account lender;
        jtx::Account borrower;
        jtx::Account mm1;
        jtx::Account mm2;
        BrokerInfo broker;
    };

    // Broker with zero cover rates and no debt cap, so LoanSet needs no cover
    // deposit. The borrower holds a trust line but no IOU, so the profit is
    // exact. mm1 rests an offer selling 400 XRP for 1000 IOU; mm2 is funded
    // but places its offer only inside testArbitrage's batch.
    ArbitrageSetup
    setupArbitrage(jtx::Env& env)
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        Account const mm1{"mm1"};
        Account const mm2{"mm2"};
        env.fund(XRP(1'000'000), issuer, lender, borrower, mm1, mm2);
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const iou = issuer[iouCurrency_];
        STAmount const iouLimit = iou(1'000'000'000);
        env(trust(lender, iouLimit));
        env(trust(borrower, iouLimit));
        env(trust(mm1, iouLimit));
        env(trust(mm2, iouLimit));
        env.close();

        BrokerParameters params = BrokerParameters::defaults();
        params.vaultDeposit = 100'000;
        params.coverRateMin = TenthBips32{0};
        params.coverDeposit = 0;
        params.debtMax = 0;
        params.coverRateLiquidation = TenthBips32{0};

        env(pay(issuer, lender, iou(params.vaultDeposit)));
        env(pay(issuer, mm2, iou(20'000)));
        env.close();

        BrokerInfo const broker{createVaultAndBroker(env, iou, lender, params)};

        env(offer(mm1, iou(1'000), XRP(400)));
        env.close();

        return {
            .iou = iou,
            .issuer = issuer,
            .lender = lender,
            .borrower = borrower,
            .mm1 = mm1,
            .mm2 = mm2,
            .broker = broker};
    }

    void
    testArbitrage(FeatureBitset features)
    {
        // A borrower profits risk-free from a loan and two crossing DEX
        // offers in one atomic batch. Crossed offers execute the moment both
        // rest on the book, so mm2's offer is itself a batch inner, placed
        // right after the borrower's buy leg consumes mm1.
        testcase("arbitrage across a loan and two crossing offers in a batch");

        using namespace jtx;

        Env env(*this, features);
        auto const setup = setupArbitrage(env);
        auto const& iou = setup.iou;
        auto const& lender = setup.lender;
        auto const& borrower = setup.borrower;
        auto const& broker = setup.broker;
        auto const& mm2 = setup.mm2;

        auto const loanKeylet = nextLoanKeylet(env, broker);
        auto const principal = iou(1'000);

        BEAST_EXPECT(env.balance(borrower, iou) == iou(0));

        auto const borrowerSeq = env.seq(borrower);
        auto const mm2Seq = env.seq(mm2);

        auto const loanSetTx = loanSetInner(env, borrower, broker.brokerID, lender, principal, 2);
        auto const loanPayTx =
            loan::pay(borrower, loanKeylet.key, principal.value(), tfLoanFullPayment);

        auto const batchFee = batch::calcBatchFee(env, 2, 5);
        env(batch::outer(borrower, borrowerSeq, batchFee, tfAllOrNothing),
            batch::Inner(loanSetTx, borrowerSeq + 1),
            batch::Inner(
                offer(borrower, XRP(400), principal.value(), tfFillOrKill), borrowerSeq + 2),
            batch::Inner(offer(mm2, XRP(400), iou(1'008)), mm2Seq),
            batch::Inner(
                offer(borrower, iou(1'008).value(), XRP(400), tfFillOrKill), borrowerSeq + 3),
            batch::Inner(loanPayTx, borrowerSeq + 4),
            batch::Sig(lender, mm2),
            Ter(tesSUCCESS));
        env.close();

        if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            BEAST_EXPECT(loanSle->at(sfPrincipalOutstanding) == 0);
        BEAST_EXPECT(env.balance(borrower, iou) == iou(8));
        env.require(offers(setup.mm1, 0));
        env.require(offers(mm2, 0));
        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);
        if (auto const vaultSle = env.le(broker.vaultKeylet()); BEAST_EXPECT(vaultSle))
        {
            BEAST_EXPECT(
                vaultSle->at(sfAssetsAvailable) == iou(broker.params.vaultDeposit).value());
        }
    }

    void
    testArbitrageRollback(FeatureBitset features)
    {
        // Without a second market maker willing to buy XRP back at the
        // higher price, the sell leg's tfFillOrKill offer cannot fill and
        // returns tecKILLED. tfAllOrNothing then rolls back every inner,
        // including the loan origination; the outer batch itself still
        // returns tesSUCCESS.
        testcase("arbitrage batch rolls back when the sell offer cannot fill");

        using namespace jtx;

        Env env(*this, features);
        auto const setup = setupArbitrage(env);
        auto const& iou = setup.iou;
        auto const& lender = setup.lender;
        auto const& borrower = setup.borrower;
        auto const& broker = setup.broker;

        auto const brokerSleBefore = env.le(broker.brokerKeylet());
        auto const vaultSleBefore = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(brokerSleBefore) || !BEAST_EXPECT(vaultSleBefore))
            return;
        auto const loanSequenceBefore = brokerSleBefore->at(sfLoanSequence);
        auto const assetsAvailableBefore = vaultSleBefore->at(sfAssetsAvailable);

        auto const loanKeylet = nextLoanKeylet(env, broker);
        auto const principal = iou(1'000);

        auto const borrowerSeq = env.seq(borrower);
        auto const borrowerBalanceBefore = env.balance(borrower);

        auto const loanSetTx = loanSetInner(env, borrower, broker.brokerID, lender, principal, 2);
        auto const loanPayTx =
            loan::pay(borrower, loanKeylet.key, principal.value(), tfLoanFullPayment);

        auto const batchFee = batch::calcBatchFee(env, 1, 4);
        env(batch::outer(borrower, borrowerSeq, batchFee, tfAllOrNothing),
            batch::Inner(loanSetTx, borrowerSeq + 1),
            batch::Inner(
                offer(borrower, XRP(400), principal.value(), tfFillOrKill), borrowerSeq + 2),
            batch::Inner(
                offer(borrower, iou(1'008).value(), XRP(400), tfFillOrKill), borrowerSeq + 3),
            batch::Inner(loanPayTx, borrowerSeq + 4),
            batch::Sig(lender),
            Ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(!env.le(loanKeylet));
        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfLoanSequence) == loanSequenceBefore);
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);
        }
        BEAST_EXPECT(env.balance(borrower, iou) == iou(0));
        BEAST_EXPECT(env.balance(borrower) == borrowerBalanceBefore - batchFee);
        env.require(offers(setup.mm1, 1));
        if (auto const vaultSle = env.le(broker.vaultKeylet()); BEAST_EXPECT(vaultSle))
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == assetsAvailableBefore);
    }

    void
    testDefaultAndCoverWithdraw(FeatureBitset features)
    {
        // A defaulted loan burns first-loss cover per CoverRateLiquidation;
        // the remainder can be withdrawn once DebtTotal drops to zero. The
        // default runs in its own batch: LoanManage refuses tfLoanDefault
        // with tecTOO_SOON until the grace period elapses, so it cannot share
        // a batch with the LoanSet that creates the loan.
        testcase("loan default and cover withdraw in a batch");

        using namespace jtx;

        Env env(*this, features);

        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(100'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1'000'000};
        auto const broker = createVaultAndBroker(env, asset, lender);

        auto const loanKeylet = nextLoanKeylet(env, broker);
        auto const principal = asset(1'000);

        {
            using namespace loan;
            // PaymentTotal 1 is fine: the loan is only ever defaulted here,
            // never paid off early, so the tfLoanFullPayment restriction on
            // the last remaining payment does not apply.
            env(set(lender, broker.brokerID, principal.value()),
                kCounterparty(borrower.id()),
                kPaymentTotal(1),
                kPaymentInterval(86400),
                kGracePeriod(86400),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 2));
        }
        env.close();

        auto const loanSleBefore = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;

        // Advance past NextPaymentDueDate + GracePeriod so the default is no
        // longer refused with tecTOO_SOON.
        auto const dueDate = loanSleBefore->at(sfNextPaymentDueDate);
        auto const gracePeriod = loanSleBefore->at(sfGracePeriod);
        using d = NetClock::duration;
        using tp = NetClock::time_point;
        env.close(tp{d{dueDate + gracePeriod + 1}});

        auto const brokerSleBefore = env.le(broker.brokerKeylet());
        auto const vaultSleBefore = env.le(broker.vaultKeylet());
        if (!BEAST_EXPECT(brokerSleBefore) || !BEAST_EXPECT(vaultSleBefore))
            return;

        auto const coverAvailableBefore = brokerSleBefore->at(sfCoverAvailable);
        auto const assetsAvailableBefore = vaultSleBefore->at(sfAssetsAvailable);
        auto const assetsTotalBefore = vaultSleBefore->at(sfAssetsTotal);

        TenthBips32 const coverRateMin{brokerSleBefore->at(sfCoverRateMinimum)};
        TenthBips32 const coverRateLiquidation{brokerSleBefore->at(sfCoverRateLiquidation)};
        Number const totalDefaultAmount{loanSleBefore->at(sfPrincipalOutstanding)};

        // DefaultCovered = min(MinimumCover x CoverRateLiquidation,
        // DefaultAmount, CoverAvailable), matching LoanManage::defaultLoan.
        // Zero interest and fees make DefaultAmount equal the outstanding
        // principal under both accounting modes, so no
        // featureLendingProtocolV1_1 branch is needed here.
        Number defaultCovered;
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
            auto const minimumCover =
                tenthBipsOfValue(Number{brokerSleBefore->at(sfDebtTotal)}, coverRateMin);
            defaultCovered = std::min(
                roundToAsset(
                    asset.raw(),
                    std::min(
                        tenthBipsOfValue(minimumCover, coverRateLiquidation), totalDefaultAmount),
                    loanSleBefore->at(sfLoanScale)),
                Number{coverAvailableBefore});
        }
        auto const vaultDefaultAmount = totalDefaultAmount - defaultCovered;

        auto const lenderSeq = env.seq(lender);
        auto const lenderBalanceBefore = env.balance(lender);
        auto const withdrawAmount = STAmount{asset.raw(), coverAvailableBefore - defaultCovered};

        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
            batch::Inner(loan::manage(lender, loanKeylet.key, tfLoanDefault), lenderSeq + 1),
            batch::Inner(
                loan_broker::coverWithdraw(lender, broker.brokerID, withdrawAmount), lenderSeq + 2),
            Ter(tesSUCCESS));
        env.close();

        if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
        {
            BEAST_EXPECT(loanSle->isFlag(lsfLoanDefault));
            BEAST_EXPECT(loanSle->at(sfPrincipalOutstanding) == 0);
        }
        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfCoverAvailable) == 0);
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);
        }
        BEAST_EXPECT(env.balance(lender) == lenderBalanceBefore - batchFee + withdrawAmount);
        // The default moves defaultCovered from the broker's cover to the
        // vault; the cover withdraw pays out of the broker's own
        // pseudo-account and does not touch the vault again.
        if (auto const vaultSle = env.le(broker.vaultKeylet()); BEAST_EXPECT(vaultSle))
        {
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == assetsAvailableBefore + defaultCovered);
            BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == assetsTotalBefore - vaultDefaultAmount);
        }
    }

    void
    testIndependentVaultChain(FeatureBitset features)
    {
        // tfIndependent runs every inner regardless of earlier failures.
        // VaultWithdraw fails before anything is deposited (AssetsTotal is
        // zero, so there are no shares to redeem yet) and VaultDelete fails
        // once the vault holds a deposit (tecHAS_OBLIGATIONS); both are
        // tec-class results, so neither stops the inners that follow.
        testcase("independent batch runs every inner despite failing inners");

        using namespace jtx;

        Env env(*this, features);

        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        Vault const vault{env};

        auto const seq = env.seq(owner);
        auto const vaultKeylet = keylet::vault(owner.id(), SeqProxy::rawSequence(seq + 1));
        auto const createTx = std::get<0>(vault.create({.owner = owner, .asset = xrpIssue()}));
        auto const amount = XRP(1'000);

        auto const batchFee = batch::calcBatchFee(env, 0, 4);
        env(batch::outer(owner, seq, batchFee, tfIndependent),
            batch::Inner(createTx, seq + 1),
            batch::Inner(
                vault.withdraw({.depositor = owner, .id = vaultKeylet.key, .amount = amount}),
                seq + 2),
            batch::Inner(
                vault.deposit({.depositor = owner, .id = vaultKeylet.key, .amount = amount}),
                seq + 3),
            batch::Inner(vault.del({.owner = owner, .id = vaultKeylet.key}), seq + 4),
            Ter(tesSUCCESS));
        env.close();

        if (auto const vaultSle = env.le(vaultKeylet); BEAST_EXPECT(vaultSle))
            BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == amount.value());
        BEAST_EXPECT(env.seq(owner) == seq + 5);
    }

    void
    testIndependentLoanChain(FeatureBitset features)
    {
        // Same claim as testIndependentVaultChain, on a loan chain. The
        // borrower starts with no IOU balance, so a LoanPay for twice the
        // loan's principal fails preclaim's balance check
        // (tecINSUFFICIENT_FUNDS); the cover deposit submitted after it
        // still lands.
        testcase("independent batch runs every loan inner despite a failing payment");

        using namespace jtx;

        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const iou = issuer[iouCurrency_];
        STAmount const iouLimit = iou(1'000'000'000);
        env(trust(lender, iouLimit));
        env(trust(borrower, iouLimit));
        env.close();

        env(pay(issuer, lender, iou(2'000'000)));
        env.close();

        auto const broker = createVaultAndBroker(env, iou, lender);

        auto const loanKeylet = nextLoanKeylet(env, broker);
        auto const principal = iou(1'000);
        auto const brokerSleBefore = env.le(broker.brokerKeylet());
        if (!BEAST_EXPECT(brokerSleBefore))
            return;
        auto const coverAvailableBefore = brokerSleBefore->at(sfCoverAvailable);

        auto const lenderSeq = env.seq(lender);
        auto const borrowerSeq = env.seq(borrower);

        auto const loanSetTx = loanSetInner(env, lender, broker.brokerID, borrower, principal, 2);
        auto const loanPayTx =
            loan::pay(borrower, loanKeylet.key, iou(2'000).value(), tfLoanFullPayment);
        auto const additionalCover = iou(500);

        auto const batchFee = batch::calcBatchFee(env, 1, 3);
        env(batch::outer(lender, lenderSeq, batchFee, tfIndependent),
            batch::Inner(loanSetTx, lenderSeq + 1),
            batch::Inner(loanPayTx, borrowerSeq),
            batch::Inner(
                loan_broker::coverDeposit(lender, broker.brokerID, additionalCover), lenderSeq + 2),
            batch::Sig(borrower),
            Ter(tesSUCCESS));
        env.close();

        if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            BEAST_EXPECT(loanSle->at(sfPrincipalOutstanding) == principal.value());
        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(
                brokerSle->at(sfCoverAvailable) == coverAvailableBefore + additionalCover.value());
        }
    }

public:
    void
    run() override
    {
        for (auto const& features : {all_, all_ | featureLendingProtocolV1_1})
        {
            testVaultLifecycle(features);
            testArbitrage(features);
            testArbitrageRollback(features);
            testDefaultAndCoverWithdraw(features);
            testIndependentVaultChain(features);
            testIndependentLoanChain(features);
        }
        testClosedEndedVaultLifecycle(all_ | featureLendingProtocolV1_1);
        testLoanLifecycleOpenEndedVault(all_);
        testLoanLifecycleClosedEndedVault(all_ | featureLendingProtocolV1_1);
    }
};

BEAST_DEFINE_TESTSUITE(LendingBatch, tx, xrpl);

}  // namespace xrpl::test
