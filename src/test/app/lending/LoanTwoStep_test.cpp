#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace xrpl::test {

class LoanTwoStep_test : public LoanTestBase
{
private:
    // Every balance a proposal, acceptance or deletion can move: the vault's
    // asset accounting, the broker's bookkeeping, and the owner counts of
    // the broker owner and the borrower.
    struct Books
    {
        Number available = 0;
        Number reserved = 0;
        Number total = 0;
        Number debtTotal = 0;
        Number coverAvailable = 0;
        std::uint32_t brokerOwners = 0;
        std::uint32_t lenderOwners = 0;
        std::uint32_t borrowerOwners = 0;

        bool
        operator==(Books const&) const = default;

        // The change a transaction is expected to make. A field left out is
        // expected not to move.
        struct Delta
        {
            Number available = 0;
            Number reserved = 0;
            Number total = 0;
            Number debtTotal = 0;
            Number coverAvailable = 0;
            std::int32_t brokerOwners = 0;
            std::int32_t lenderOwners = 0;
            std::int32_t borrowerOwners = 0;
        };

        [[nodiscard]] Books
        adjusted(Delta const& delta) const
        {
            return {
                .available = available + delta.available,
                .reserved = reserved + delta.reserved,
                .total = total + delta.total,
                .debtTotal = debtTotal + delta.debtTotal,
                .coverAvailable = coverAvailable + delta.coverAvailable,
                .brokerOwners = brokerOwners + delta.brokerOwners,
                .lenderOwners = lenderOwners + delta.lenderOwners,
                .borrowerOwners = borrowerOwners + delta.borrowerOwners};
        }
    };

    // Shared context used by every two-step scenario. The accounts and loan
    // terms are fixed; `features` is set by testTwoStep for each run.
    FeatureBitset features_;
    jtx::Account const issuer_{"issuer"};  // Issues the IOU / MPT assets
    jtx::Account const lender_{"lender"};  // Vault + LoanBroker owner
    jtx::Account const borrower_{"borrower"};
    jtx::Account const evan_{"evan"};  // unrelated third party

    // A funded Vault + LoanBroker owned by lender_ on the requested asset,
    // with borrower_ and evan_ funded as well.
    BrokerInfo
    makeBroker(jtx::Env& env, AssetType assetType, bool enableClawback = false)
    {
        return makeFundedBroker(
            env, assetType, issuer_, lender_, borrower_, {evan_}, enableClawback);
    }

    // Like makeBroker(AssetType::IOU), except the issuer sets asfRequireAuth
    // before any trust line exists and then authorises the lender's and the
    // borrower's lines.
    BrokerInfo
    makeRequireAuthIouBroker(jtx::Env& env)
    {
        using namespace jtx;
        env.fund(XRP(100'000'000), noripple(lender_));
        env.fund(XRP(1'000'000), issuer_, borrower_, evan_);
        env.close();
        // asfRequireAuth must be set before the issuer owns any trust lines.
        env(fset(issuer_, asfRequireAuth));
        env.close();

        BrokerParameters const params{};
        PrettyAsset const asset{issuer_[iouCurrency_]};
        auto const limit = asset(100 * (params.vaultDeposit + params.coverDeposit));
        env(trust(lender_, limit));
        env(trust(borrower_, limit));
        env.close();
        env(trust(issuer_, asset(0), lender_, tfSetfAuth));
        env(trust(issuer_, asset(0), borrower_, tfSetfAuth));
        env(pay(issuer_, lender_, asset(params.vaultDeposit + params.coverDeposit)));
        env.close();
        return createVaultAndBroker(env, asset, lender_, params);
    }

    // Rewrites the vault's LEVersion to Legacy (accrual accounting) to
    // simulate a vault created before LendingProtocolV1_1, and checks the
    // vault now resolves to that version. The rewrite does not survive a
    // ledger close.
    void
    makeVaultInstantRecognition(jtx::Env& env, BrokerInfo const& broker)
    {
        modifyOpenLedgerEntry(env, broker.vaultKeylet(), [](SLE& vault) {
            vault.setFieldU8(sfLEVersion, std::to_underlying(VaultVersion::Legacy));
        });
        if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
            BEAST_EXPECT(getVaultVersion(v) == VaultVersion::Legacy);
    }

    // The accounting model a vault version uses, for test case names.
    // Legacy recognises the full scheduled interest when the loan is
    // accepted; CashBasis and FixedPrecision recognise it as payments arrive.
    // No version recognises interest at proposal: a proposal can be deleted
    // at any time at no cost, so it must not move the share price.
    static char const*
    getVersionName(VaultVersion version)
    {
        return version == VaultVersion::Legacy ? "accrual" : "cash-basis";
    }

    [[nodiscard]] Books
    readBooks(jtx::Env& env, BrokerInfo const& broker)
    {
        auto const v = env.le(broker.vaultKeylet());
        auto const b = env.le(broker.brokerKeylet());
        if (!BEAST_EXPECT(v && b))
            return {};
        return {
            .available = v->at(sfAssetsAvailable),
            .reserved = v->at(sfAssetsReserved),
            .total = v->at(sfAssetsTotal),
            .debtTotal = b->at(sfDebtTotal),
            .coverAvailable = b->at(sfCoverAvailable),
            .brokerOwners = b->at(sfOwnerCount),
            .lenderOwners = env.ownerCount(lender_),
            .borrowerOwners = env.ownerCount(borrower_)};
    }

    // Proposes a loan from the broker owner to `borrower` on the kProposal*
    // terms, with a StartDate an hour out, and returns the keylet the loan
    // takes (or would have taken, when `extra` carries a failing Ter). The
    // ledger is left open: some scenarios must not close it.
    template <typename... Extra>
    [[nodiscard]] Keylet
    propose(
        jtx::Env& env,
        BrokerInfo const& broker,
        jtx::Account const& borrower,
        Extra const&... extra)
    {
        using namespace std::chrono_literals;
        auto const loanKeylet = nextLoanKeylet(env, broker);
        proposeLoan(
            env, broker, lender_, borrower, (env.now() + 1h).time_since_epoch().count(), extra...);
        return loanKeylet;
    }

    // propose, then close the ledger.
    template <typename... Extra>
    [[nodiscard]] Keylet
    proposeAndClose(
        jtx::Env& env,
        BrokerInfo const& broker,
        jtx::Account const& borrower,
        Extra const&... extra)
    {
        auto const loanKeylet = propose(env, broker, borrower, extra...);
        env.close();
        return loanKeylet;
    }

    // Accepts a pending loan as borrower_, closes the ledger, and checks the
    // loan is now active.
    void
    acceptLoan(jtx::Env& env, Keylet const& loanKeylet)
    {
        env(jtx::loan::accept(borrower_, loanKeylet.key));
        env.close();
        expectLoanActive(env, loanKeylet);
    }

    // Successful propose / accept flows across all three asset types, the
    // origination-fee variant, and impairment of an accepted loan while
    // another loan on the same vault is pending.
    void
    testTwoStepBasics()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        // Run under both accounting models: cash-basis (a FixedPrecision
        // vault, the default once V1.2 is enabled) and accrual (a Legacy
        // vault, via makeVaultInstantRecognition). Cash-basis recognises
        // interest into Vault.AssetsTotal as payments arrive; accrual
        // recognises it when the loan is accepted.
        for (auto const vaultVersion : {VaultVersion::FixedPrecision, VaultVersion::Legacy})
        {
            for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
            {
                testcase << "Two-step: propose then accept (" << getVersionName(vaultVersion)
                         << ", " << assetTypeName(assetType) << ")";

                Env env(*this, features_);
                auto const broker = makeBroker(env, assetType);
                // The Legacy LEVersion rewrite does not survive env.close().
                // Under Legacy, skip the close until the accept has been
                // applied and checked.
                auto const closeIfCashBasis = [&]() {
                    if (vaultVersion == VaultVersion::FixedPrecision)
                        env.close();
                };
                if (vaultVersion == VaultVersion::Legacy)
                    makeVaultInstantRecognition(env, broker);
                Number const principal = broker.asset(200).number();

                auto const books0 = readBooks(env, broker);
                // A StartDate comfortably in the future.
                auto const loanKeylet = propose(env, broker, borrower_);
                closeIfCashBasis();

                // The proposal creates a pending Loan, linked only into the
                // broker pseudo-account's directory.
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                {
                    BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                    BEAST_EXPECT(loan->at(sfBorrower) == borrower_.id());
                    BEAST_EXPECT(loan->isFieldPresent(sfLoanBrokerNode));
                    BEAST_EXPECT(!loan->isFieldPresent(sfOwnerNode));
                }

                // Vault bookkeeping: Available -= P, Reserved += P. Total is
                // unchanged under both models: a proposal books no interest,
                // so deleting it cannot move the share price. Broker
                // bookkeeping: DebtTotal += P + InterestDue (zero under
                // cash-basis), OwnerCount += 1, CoverAvailable unchanged. The
                // owner reserve is charged to the broker owner, not the
                // borrower.
                auto const books1 = readBooks(env, broker);
                Number const interestDue = books1.debtTotal - books0.debtTotal - principal;
                if (vaultVersion == VaultVersion::Legacy)
                {
                    BEAST_EXPECT(interestDue > beast::kZero);
                }
                else
                {
                    BEAST_EXPECT(interestDue == beast::kZero);
                }
                BEAST_EXPECT(
                    books1 ==
                    books0.adjusted(
                        {.available = -principal,
                         .reserved = principal,
                         .debtTotal = principal + interestDue,
                         .brokerOwners = 1,
                         .lenderOwners = 1}));
                // The Vault version must be unchanged by the proposal.
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                    BEAST_EXPECT(getVaultVersion(v) == vaultVersion);

                // Capture pre-acceptance balances to verify disbursement.
                auto const vaultPseudo = vaultPseudoAccount(env, broker, lender_);
                STAmount const pseudoBal0 = env.balance(vaultPseudo, broker.asset).value();
                STAmount const borrowerBal0 = env.balance(borrower_, broker.asset).value();

                env(accept(borrower_, loanKeylet.key));
                closeIfCashBasis();

                // The loan is now active and linked into the borrower's
                // directory.
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                {
                    BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                    BEAST_EXPECT(loan->isFieldPresent(sfLoanBrokerNode));
                    BEAST_EXPECT(loan->isFieldPresent(sfOwnerNode));
                }

                // The reserve is swapped from the broker owner to the
                // borrower. Reserved principal is released; Available is
                // unchanged from the proposal. Total grows by InterestDue
                // under accrual, now that the loan is active, and is
                // unchanged under cash-basis. Acceptance leaves DebtTotal,
                // the broker's OwnerCount and CoverAvailable where the
                // proposal put them.
                BEAST_EXPECT(
                    readBooks(env, broker) ==
                    books0.adjusted(
                        {.available = -principal,
                         .total = interestDue,
                         .debtTotal = principal + interestDue,
                         .brokerOwners = 1,
                         .borrowerOwners = 1}));

                // The principal is disbursed from the vault pseudo-account to
                // the borrower.
                BEAST_EXPECT(
                    env.balance(vaultPseudo, broker.asset).value() ==
                    pseudoBal0 - broker.asset(200).value());
                BEAST_EXPECT(env.balance(borrower_, broker.asset).value() > borrowerBal0);
            }
        }

        // Propose with a non-zero origination fee. On acceptance the principal
        // leaves the vault pseudo-account, the borrower receives the net, and
        // the broker owner receives the fee. IOU and MPT only.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: propose then accept with origination fee ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features_);
            auto const broker = makeBroker(env, assetType);
            Number const principal = broker.asset(200).number();
            Number const originationFee = broker.asset(5).number();

            auto const loanKeylet =
                proposeAndClose(env, broker, borrower_, kLoanOriginationFee(originationFee));

            // The pending loan records the origination fee.
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->at(sfLoanOriginationFee) == originationFee);
            }

            auto const vaultPseudo = vaultPseudoAccount(env, broker, lender_);
            STAmount const pseudoBal0 = env.balance(vaultPseudo, broker.asset).value();
            STAmount const borrowerBal0 = env.balance(borrower_, broker.asset).value();
            STAmount const lenderBal0 = env.balance(lender_, broker.asset).value();

            acceptLoan(env, loanKeylet);

            STAmount const netToBorrower{broker.asset, principal - originationFee};
            STAmount const feeToOwner{broker.asset, originationFee};

            // The full principal leaves the vault pseudo-account.
            BEAST_EXPECT(
                env.balance(vaultPseudo, broker.asset).value() ==
                pseudoBal0 - broker.asset(200).value());
            // The borrower receives the principal net of the origination fee.
            BEAST_EXPECT(
                env.balance(borrower_, broker.asset).value() == borrowerBal0 + netToBorrower);
            // The broker owner receives the origination fee.
            BEAST_EXPECT(env.balance(lender_, broker.asset).value() == lenderBal0 + feeToOwner);
        }

        // Impairing an active loan must succeed while another loan on the same
        // vault is pending, and must leave the pending loan alone: its reserved
        // principal and the vault's available balance do not move, it can still
        // be accepted, and the impairment can still be reversed. The same
        // fixture also covers the two LoanAccept permission checks: an accepted
        // loan cannot be accepted again, and a pending loan can only be
        // accepted by its named borrower. The ValidVault invariant already
        // bounds the loss by the assets lent out by active loans (AssetsTotal -
        // AssetsAvailable - AssetsReserved) on every transaction below, so that
        // bound is not re-derived here.
        //
        // A FixedPrecision vault takes the adjustVaultBalances path; a Legacy
        // vault takes LoanManage's own AssetsTotal-based bound. The Legacy
        // LEVersion rewrite does not survive a ledger close, so that variant
        // runs without fixCleanup3_4_0 (impair would otherwise need a late
        // payment, which needs a close) and never closes the ledger.
        for (bool const legacyVault : {false, true})
        {
            testcase << "Two-step: impair accepted loan while another loan is pending ("
                     << (legacyVault ? "Legacy" : "FixedPrecision") << " vault)";

            Env env(*this, legacyVault ? features_ - fixCleanup3_4_0 : features_);
            auto const broker = makeBroker(env, AssetType::XRP);
            auto const closeIfAllowed = [&] {
                if (!legacyVault)
                    env.close();
            };

            if (legacyVault)
                makeVaultInstantRecognition(env, broker);

            // L1: accepted (borrower).
            auto const l1Keylet = propose(env, broker, borrower_);
            closeIfAllowed();
            env(accept(borrower_, l1Keylet.key));
            closeIfAllowed();
            expectLoanActive(env, l1Keylet);

            // L2: still pending (evan), which leaves AssetsReserved > 0. Its
            // StartDate sits past L1's first due date, which the impairment
            // below advances the clock to.
            auto const l2Keylet = nextLoanKeylet(env, broker);
            proposeLoan(env, broker, lender_, evan_, (env.now() + 2h).time_since_epoch().count());
            closeIfAllowed();
            expectLoanPending(env, l2Keylet);

            // LoanAccept is rejected with tecNO_PERMISSION on a loan that is
            // no longer pending (L1, even from its own borrower) and on a
            // pending loan from any account other than its named borrower
            // (L2 belongs to evan). L2 stays pending throughout.
            env(accept(borrower_, l1Keylet.key), Ter(tecNO_PERMISSION));
            env(accept(borrower_, l2Keylet.key), Ter(tecNO_PERMISSION));
            env(accept(lender_, l2Keylet.key), Ter(tecNO_PERMISSION));
            closeIfAllowed();
            expectLoanPending(env, l2Keylet);
            expectLoanActive(env, l1Keylet);

            auto const books0 = readBooks(env, broker);
            Number const l2Principal = broker.asset(200).number();
            BEAST_EXPECT(books0.reserved == l2Principal);

            // Impair L1 while L2 is pending. Under fixCleanup3_4_0 the payment
            // must already be late.
            advancePastDueDate(env, l1Keylet);
            env(manage(lender_, l1Keylet.key, tfLoanImpair));
            closeIfAllowed();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->isFlag(lsfLoanImpaired));

            // The impairment books a loss on L1 only. L2's reserved principal
            // and the vault's available balance are untouched.
            auto const books1 = readBooks(env, broker);
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(v->at(sfLossUnrealized) > beast::kZero);
            BEAST_EXPECT(books1.reserved == books0.reserved);
            BEAST_EXPECT(books1.available == books0.available);
            expectLoanPending(env, l2Keylet);

            // L2 can still be accepted. Acceptance releases its principal from
            // AssetsReserved and disburses it, so AssetsAvailable is unchanged.
            env(accept(evan_, l2Keylet.key));
            closeIfAllowed();
            expectLoanActive(env, l2Keylet);
            auto const books2 = readBooks(env, broker);
            BEAST_EXPECT(books2.reserved == books1.reserved - l2Principal);
            BEAST_EXPECT(books2.available == books1.available);

            // Reversing the impairment on L1 clears the loss.
            env(manage(lender_, l1Keylet.key, tfLoanUnimpair));
            closeIfAllowed();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(!l1->isFlag(lsfLoanImpaired));
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(v->at(sfLossUnrealized) == beast::kZero);

            // L1 is still overdue after the unimpair, so a late payment catches
            // it up. A late payment settles exactly one instalment, and the
            // amount is only an upper bound. The Legacy variant never advances
            // the clock, so its loan is not late and this step is skipped.
            if (!legacyVault)
            {
                env(pay(borrower_, l1Keylet.key, broker.asset(400), tfLoanLatePayment));
                env.close();
                if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                    BEAST_EXPECT(l1->at(sfPaymentRemaining) == kProposalPayTotal - 1);
            }
        }
    }

    // A loan that has been accepted while the ledger clock is still before its
    // StartDate is live but has not started yet. Only the two-step flow can
    // reach that window: a one-step loan starts in the ledger that creates it.
    // The schedule is anchored to StartDate, and no interest accrues until the
    // loan starts, so paying is allowed but impairing and defaulting are not.
    void
    testTwoStepBeforeStartDate()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        testcase("Two-step: payments, impairment and deletion before StartDate");

        Env env(*this, features_);
        auto const broker = makeBroker(env, AssetType::XRP);

        auto const books0 = readBooks(env, broker);

        // Propose a loan starting an hour out and accept it immediately. Only
        // the two closes advance the clock, so the accepted loan has not
        // started yet. The overpayment flag is set so the overpayment step
        // below is allowed.
        std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
        auto const loanKeylet = nextLoanKeylet(env, broker);
        proposeLoan(env, broker, lender_, borrower_, startDate, Txflags(tfLoanOverpayment));
        env.close();
        acceptLoan(env, loanKeylet);
        if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            BEAST_EXPECT(loan->isFlag(lsfLoanOverpayment));
        BEAST_EXPECT(env.now().time_since_epoch().count() < startDate);

        // The first instalment is not due until one interval after StartDate.
        // The loan carries scheduled interest, none of which is owed yet: no
        // time has passed since StartDate, and the proposal set no close fee
        // or close interest rate.
        auto const state0 = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(state0.previousPaymentDate == 0);
        BEAST_EXPECT(state0.nextPaymentDate == startDate + kProposalPayInterval);
        BEAST_EXPECT(state0.paymentRemaining == kProposalPayTotal);
        BEAST_EXPECT(state0.totalValue > state0.principalOutstanding);

        // Nothing has accrued yet, so one periodic payment settles one
        // instalment: no flag, and no extra funds.
        STAmount const due{
            broker.asset,
            roundPeriodicPayment(broker.asset, state0.periodicPayment, state0.loanScale)};

        // There is nothing yet to impair or default on, and a payment flagged
        // as late is rejected for the same reason.
        env(manage(lender_, loanKeylet.key, tfLoanImpair), Ter(tecTOO_SOON));
        env.close();
        env(manage(lender_, loanKeylet.key, tfLoanDefault), Ter(tecTOO_SOON));
        env.close();
        env(pay(borrower_, loanKeylet.key, due + due, tfLoanLatePayment), Ter(tecTOO_SOON));
        env.close();

        // Neither party can simply walk away from the accepted loan: it has to
        // be paid off first.
        env(del(borrower_, loanKeylet.key), Ter(tecHAS_OBLIGATIONS));
        env(del(lender_, loanKeylet.key), Ter(tecHAS_OBLIGATIONS));
        env.close();

        // None of the failures changed the loan.
        {
            auto const state = getCurrentState(env, broker, loanKeylet);
            BEAST_EXPECT((state.flags & (lsfLoanImpaired | lsfLoanDefault)) == 0);
            BEAST_EXPECT(state.nextPaymentDate == state0.nextPaymentDate);
            BEAST_EXPECT(state.paymentRemaining == kProposalPayTotal);
            BEAST_EXPECT(state.principalOutstanding == state0.principalOutstanding);
        }

        // A regular payment takes one instalment, and the schedule moves on by
        // one interval from StartDate: the acceptance time plays no part.
        auto const vaultBeforePay = readBooks(env, broker);
        STAmount const borrowerBeforePay = env.balance(borrower_, broker.asset).value();
        env(pay(borrower_, loanKeylet.key, due));
        env.close();

        auto const state1 = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(state1.paymentRemaining == kProposalPayTotal - 1);
        BEAST_EXPECT(state1.previousPaymentDate == startDate + kProposalPayInterval);
        BEAST_EXPECT(state1.nextPaymentDate == startDate + (2 * kProposalPayInterval));
        BEAST_EXPECT(state1.principalOutstanding < state0.principalOutstanding);
        // The money reached the vault, and the borrower was charged no more
        // than the instalment (plus the transaction fee): starting early costs
        // nothing extra.
        BEAST_EXPECT(readBooks(env, broker).available > vaultBeforePay.available);
        BEAST_EXPECT(
            env.balance(borrower_, broker.asset).value() >
            borrowerBeforePay - due - XRP(1).value());

        // An overpayment takes the next instalment plus an extra that is
        // smaller than a further instalment, so exactly one scheduled payment
        // is consumed and the remainder is the overpayment.
        STAmount const extra = broker.asset(5).value();
        BEAST_EXPECT(extra < due);
        env(pay(borrower_, loanKeylet.key, due + extra, tfLoanOverpayment));
        env.close();

        auto const state2 = getCurrentState(env, broker, loanKeylet);
        // The overpayment consumes no scheduled payment of its own, so the
        // schedule advances by exactly one more interval.
        BEAST_EXPECT(state2.paymentRemaining == kProposalPayTotal - 2);
        BEAST_EXPECT(state2.previousPaymentDate == startDate + (2 * kProposalPayInterval));
        BEAST_EXPECT(state2.nextPaymentDate == startDate + (3 * kProposalPayInterval));
        // The extra came off the principal on top of the instalment's own
        // principal part, and the loan re-amortized to a smaller payment.
        BEAST_EXPECT(state2.principalOutstanding <= state1.principalOutstanding - Number(extra));
        BEAST_EXPECT(state2.periodicPayment < state1.periodicPayment);

        // A full payment this early charges only the remaining principal: the
        // loan still carries scheduled interest, but the clock has not reached
        // the last payment date, so nothing has accrued. One drop short is
        // rejected.
        BEAST_EXPECT(state2.totalValue > state2.principalOutstanding);
        STAmount const principal{broker.asset, state2.principalOutstanding};
        STAmount const borrowerBeforePayoff = env.balance(borrower_, broker.asset).value();

        env(pay(borrower_,
                loanKeylet.key,
                principal - STAmount{broker.asset, 1},
                tfLoanFullPayment),
            Ter(tecINSUFFICIENT_PAYMENT));
        env.close();

        env(pay(borrower_, loanKeylet.key, principal, tfLoanFullPayment));
        env.close();

        // The loan is fully paid.
        auto const state3 = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(state3.paymentRemaining == 0);
        BEAST_EXPECT(state3.principalOutstanding == beast::kZero);
        BEAST_EXPECT(state3.totalValue == beast::kZero);

        // Only the principal was taken from the borrower. The comparison
        // allows for the transaction fees of the two LoanPays above.
        STAmount const borrowerAfterPayoff = env.balance(borrower_, broker.asset).value();
        BEAST_EXPECT(borrowerAfterPayoff < borrowerBeforePayoff - principal);
        BEAST_EXPECT(borrowerAfterPayoff > borrowerBeforePayoff - principal - XRP(1).value());

        // Nothing is outstanding, so the loan can now be deleted.
        env(del(borrower_, loanKeylet.key));
        env.close();
        BEAST_EXPECT(!env.le(loanKeylet));

        // The balance sheet closes out: no reserved principal, no debt, and
        // everything the vault holds is available again. The two instalments
        // paid interest, so the vault ends with at least what it started with.
        auto const books1 = readBooks(env, broker);
        BEAST_EXPECT(books1.reserved == books0.reserved);
        BEAST_EXPECT(books1.available == books1.total);
        BEAST_EXPECT(books1.total >= books0.total);
        BEAST_EXPECT(books1.debtTotal == books0.debtTotal);
        BEAST_EXPECT(books1.coverAvailable == books0.coverAvailable);
    }

    // Freeze / deep-freeze / MPT lock / authorisation scenarios across both
    // sides of the two-step flow (LoanSet at proposal time, LoanAccept at
    // acceptance time), plus the holding and reserve gates of LoanAccept.
    // Each environment carries one pending loan through every gate in turn:
    // the gate is raised, shown to reject both a new proposal and the
    // acceptance, and lowered again. The loan is accepted once every gate is
    // clear, which proves the gates were the only thing in the way.
    //
    // A rejected transaction with a tec result stays in the open ledger. If
    // the fix for it lands in the same open ledger, the two are reordered
    // canonically at close and the rejected transaction can succeed on
    // replay. Every rejection below is therefore followed by a close before
    // the gate is lowered.
    void
    testTwoStepFreeze()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        static constexpr FreezeCase freezeCases[] = {
            {.label = "frozen vault pseudo-account",
             .target = FreezeTarget::VaultPseudo,
             .trustFlags = tfSetFreeze},
            {.label = "deep frozen broker pseudo-account",
             .target = FreezeTarget::BrokerPseudo,
             .trustFlags = tfSetFreeze | tfSetDeepFreeze},
            {.label = "frozen borrower",
             .target = FreezeTarget::Borrower,
             .trustFlags = tfSetFreeze},
            {.label = "deep frozen broker owner",
             .target = FreezeTarget::BrokerOwner,
             .trustFlags = tfSetFreeze | tfSetDeepFreeze},
        };

        // Raise each freeze (IOU) or lock (MPT) in turn while `pendingKeylet`
        // is outstanding. A new proposal is rejected with tecFROZEN /
        // tecLOCKED and creates no loan, and the acceptance is rejected with
        // the same code and leaves the loan pending.
        auto const runFreezeCases = [&](Env& env,
                                        BrokerInfo const& broker,
                                        AssetType assetType,
                                        Keylet const& pendingKeylet) {
            for (auto const& fc : freezeCases)
            {
                testcase << "Two-step: LoanSet and LoanAccept with " << fc.label << " ("
                         << assetTypeName(assetType) << ")";

                auto const target = resolveFreezeTarget(env, broker, fc.target, borrower_, lender_);
                TER const expected = freezeHolding(env, broker, issuer_, target, fc.trustFlags);

                auto const rejectedKeylet = propose(env, broker, borrower_, Ter(expected));
                BEAST_EXPECT(!env.le(rejectedKeylet));

                env(accept(borrower_, pendingKeylet.key), Ter(expected));
                expectLoanPending(env, pendingKeylet);
                env.close();

                thawHolding(env, broker, issuer_, target, fc.trustFlags);
            }
        };

        {
            // IOU: the freeze cases, then the borrower's XRP reserve, then
            // the trust-line gate for the borrower and, when the loan carries
            // an origination fee, for the broker owner.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::IOU);
            Issue const iou = broker.asset.raw().get<Issue>();
            auto const borrowerLine = keylet::trustLine(borrower_, iou);
            auto const lenderLine = keylet::trustLine(lender_, iou);

            auto const l1Keylet = proposeAndClose(env, broker, borrower_);

            runFreezeCases(env, broker, AssetType::IOU, l1Keylet);

            testcase("Two-step: LoanAccept with insufficient reserve");

            // A Borrower without reserve for the Loan object is rejected with
            // tecINSUFFICIENT_RESERVE and the loan stays pending. Drain the
            // borrower's XRP down to its current reserve, then give it back.
            auto const drained = env.balance(borrower_) -
                accountReserve(*env.current(), borrower_.id(), env.journal);
            env(pay(borrower_, issuer_, drained));
            env.close();
            env(accept(borrower_, l1Keylet.key), Ter(tecINSUFFICIENT_RESERVE));
            expectLoanPending(env, l1Keylet);
            env.close();
            env(pay(issuer_, borrower_, drained));
            env.close();

            testcase("Two-step: LoanAccept when a holding cannot be added (IOU)");

            // L2 carries an origination fee, so accepting it needs the broker
            // owner to be able to hold the asset as well as the borrower.
            // Propose it while both trust lines still exist.
            Number const originationFee = broker.asset(5).number();
            auto const l2Keylet =
                proposeAndClose(env, broker, borrower_, kLoanOriginationFee(originationFee));

            // Remove both lines. Both balances are zero, and the broker
            // owner's line needs NoRipple to match its account default. Then
            // clear the issuer's DefaultRipple so a transactor cannot recreate
            // a line for either account.
            env.trust(broker.asset(0), borrower_);
            env(trust(lender_, broker.asset(0), tfSetNoRipple));
            env.close();
            BEAST_EXPECT(!env.le(borrowerLine));
            BEAST_EXPECT(!env.le(lenderLine));
            env(fclear(issuer_, asfDefaultRipple));
            env.close();

            // The borrower has no line: the acceptance is rejected with
            // terNO_RIPPLE and the loan stays pending.
            env(accept(borrower_, l1Keylet.key), Ter(terNO_RIPPLE));
            expectLoanPending(env, l1Keylet);

            // The borrower recreates its line. L2 is still rejected, because
            // the gate covers the broker owner too when there is a fee, and
            // the rejection leaves the borrower's line in place. L1 has no
            // fee, so it is accepted.
            env(trust(borrower_, broker.asset(1'000'000)));
            env.close();
            BEAST_EXPECT(env.le(borrowerLine));
            env(accept(borrower_, l2Keylet.key), Ter(terNO_RIPPLE));
            expectLoanPending(env, l2Keylet);
            BEAST_EXPECT(env.le(borrowerLine));
            BEAST_EXPECT(!env.le(lenderLine));

            acceptLoan(env, l1Keylet);
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == broker.asset(200).value());

            // Once the broker owner has a line again, L2 is accepted and the
            // fee reaches the broker owner.
            env(trust(lender_, broker.asset(1'000'000)));
            env.close();
            BEAST_EXPECT(env.le(lenderLine));
            acceptLoan(env, l2Keylet);
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == broker.asset(395).value());
            BEAST_EXPECT(env.balance(lender_, broker.asset).value() == broker.asset(5).value());
        }

        {
            // MPT: the lock cases, then an acceptance that has to create
            // both MPTokens.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::MPT);
            auto const mptID = broker.asset.raw().get<MPTIssue>().getMptID();
            MPTTester mptt{env, issuer_, mptID};
            auto const borrowerMPToken = keylet::mptoken(mptID, borrower_);
            auto const lenderMPToken = keylet::mptoken(mptID, lender_);

            auto const l1Keylet = proposeAndClose(env, broker, borrower_);

            runFreezeCases(env, broker, AssetType::MPT, l1Keylet);

            testcase("Two-step: LoanAccept creating two MPTokens");

            // L2 carries an origination fee. With neither the borrower nor
            // the broker owner holding an MPToken, LoanAccept creates both
            // and succeeds.
            Number const originationFee = broker.asset(5).number();
            auto const l2Keylet =
                proposeAndClose(env, broker, borrower_, kLoanOriginationFee(originationFee));

            // Delete both holdings after the proposal. The broker owner's
            // remaining balance goes back to the issuer first.
            mptt.authorize({.account = borrower_, .flags = tfMPTUnauthorize});
            if (auto const lenderBalance = env.balance(lender_, broker.asset);
                lenderBalance.value() != beast::kZero)
                env(pay(lender_, issuer_, lenderBalance));
            env.close();
            mptt.authorize({.account = lender_, .flags = tfMPTUnauthorize});
            env.close();
            BEAST_EXPECT(!env.le(borrowerMPToken));
            BEAST_EXPECT(!env.le(lenderMPToken));

            acceptLoan(env, l2Keylet);

            // Both holdings exist again and hold the disbursed amounts.
            BEAST_EXPECT(env.le(borrowerMPToken));
            BEAST_EXPECT(env.le(lenderMPToken));
            STAmount const netToBorrower{broker.asset, broker.asset(200).number() - originationFee};
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == netToBorrower);
            BEAST_EXPECT(
                env.balance(lender_, broker.asset).value() ==
                STAmount(broker.asset, originationFee));

            // With every lock lifted and the holdings back, L1 is accepted
            // into the borrower's recreated MPToken.
            acceptLoan(env, l1Keylet);
            BEAST_EXPECT(
                env.balance(borrower_, broker.asset).value() ==
                netToBorrower + broker.asset(200).value());
        }

        // Which account loses its authorisation after the proposal. The
        // broker owner (lender_) is funded with noripple, so its trust line
        // only deletes once it carries NoRipple to match the account's
        // default state. The borrower has default ripple on and needs no
        // flag.
        struct AuthCase
        {
            char const* label;
            Account const& holder;
            std::uint32_t deleteFlags;
        };
        AuthCase const authCases[] = {
            {.label = "borrower", .holder = borrower_, .deleteFlags = 0},
            {.label = "broker owner", .holder = lender_, .deleteFlags = tfSetNoRipple},
        };

        {
            testcase("Two-step: MPT authorisation revoked between LoanSet and LoanAccept");

            // The issuer requires authorisation. Revoking either recipient's
            // authorisation after the proposal makes a new proposal and the
            // acceptance fail with tecNO_AUTH, and the loan stays pending.
            // Once both are authorised again the loan is accepted.
            Env env(*this, features_);
            auto fixture = makeRequireAuthMptBroker(env, issuer_, lender_, borrower_, 2'000'000);
            auto& asset = fixture.asset;
            BrokerInfo const& broker = fixture.broker;

            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            for (auto const& authCase : authCases)
            {
                asset.authorize(
                    {.account = issuer_, .holder = authCase.holder, .flags = tfMPTUnauthorize});
                env.close();

                auto const rejectedKeylet = propose(env, broker, borrower_, Ter(tecNO_AUTH));
                BEAST_EXPECT(!env.le(rejectedKeylet));

                env(accept(borrower_, loanKeylet.key), Ter(tecNO_AUTH));
                expectLoanPending(env, loanKeylet);
                env.close();

                asset.authorize({.account = issuer_, .holder = authCase.holder});
                env.close();
            }

            acceptLoan(env, loanKeylet);
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == broker.asset(200).value());
        }

        {
            testcase("Two-step: IOU authorisation lost between LoanSet and LoanAccept");

            // The issuer has asfRequireAuth. After the proposal each holder in
            // turn deletes its authorised trust line: LoanAccept fails with
            // tecNO_LINE. The holder recreates the line, still unauthorised:
            // LoanAccept fails with tecNO_AUTH. The loan stays pending across
            // both rejections and the other party's line is untouched. Once
            // the issuer has authorised both new lines, LoanAccept succeeds.
            Env env(*this, features_);
            auto const broker = makeRequireAuthIouBroker(env);
            Issue const iou = broker.asset.raw().get<Issue>();

            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            for (auto const& authCase : authCases)
            {
                Account const& other = authCase.holder == borrower_ ? lender_ : borrower_;

                // Zeroing the limit deletes the holder's line.
                auto const holderLine = keylet::trustLine(authCase.holder, iou);
                env(trust(authCase.holder, broker.asset(0), authCase.deleteFlags));
                env.close();
                BEAST_EXPECT(!env.le(holderLine));
                BEAST_EXPECT(env.le(keylet::trustLine(other, iou)));

                env(accept(borrower_, loanKeylet.key), Ter(tecNO_LINE));
                expectLoanPending(env, loanKeylet);

                env(trust(authCase.holder, broker.asset(1'000'000)));
                env.close();
                BEAST_EXPECT(env.le(holderLine));
                env(accept(borrower_, loanKeylet.key), Ter(tecNO_AUTH));
                expectLoanPending(env, loanKeylet);
                env.close();

                env(trust(issuer_, broker.asset(0), authCase.holder, tfSetfAuth));
                env.close();
            }

            acceptLoan(env, loanKeylet);
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == broker.asset(200).value());
        }
    }

    // Delete/interlock scenarios that exercise how a pending loan participates
    // in downstream lifecycle operations: LoanDelete by either party,
    // LoanBrokerDelete blocked by outstanding pending loans, multiple pending
    // loans coexisting on the same broker, DebtMaximum accounting, and
    // VaultDelete rejection.
    void
    testTwoStepPendingLifecycle()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        // Deleting a pending loan reverses the proposal-time bookkeeping and
        // releases the broker owner's reserve. It can be done by either the
        // broker owner or the borrower.
        auto const testDeletePending = [&](AssetType assetType, Account const& deleter) {
            Env env(*this, features_);
            auto const broker = makeBroker(env, assetType);

            auto const books0 = readBooks(env, broker);
            // A StartDate comfortably in the future.
            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            BEAST_EXPECT(env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender_) == books0.lenderOwners + 1);

            // An unrelated account cannot delete the loan.
            env(del(evan_, loanKeylet.key), Ter(tecNO_PERMISSION));

            env(del(deleter, loanKeylet.key));
            env.close();

            // The loan is gone, the reserve is released, and the vault and
            // broker bookkeeping are fully reversed: DebtTotal and OwnerCount
            // return to their pre-proposal values, CoverAvailable is
            // untouched throughout.
            BEAST_EXPECT(!env.le(loanKeylet));
            BEAST_EXPECT(readBooks(env, broker) == books0);
        };

        for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanDelete of pending loan by broker owner ("
                     << assetTypeName(assetType) << ")";
            testDeletePending(assetType, lender_);

            testcase << "Two-step: LoanDelete of pending loan by borrower ("
                     << assetTypeName(assetType) << ")";
            testDeletePending(assetType, borrower_);
        }

        {
            testcase("Two-step: LoanBrokerDelete blocked by pending loan");

            // While a pending loan is outstanding, LoanBrokerDelete is rejected
            // with tecHAS_OBLIGATIONS. Once the pending loan is deleted, the
            // broker can be deleted.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            // The loan is pending; the broker's OwnerCount is non-zero.
            BEAST_EXPECT(readBooks(env, broker).brokerOwners != 0u);

            env(jtx::loan_broker::del(lender_, broker.brokerID), Ter(tecHAS_OBLIGATIONS));
            env.close();

            // Broker and loan are both still present.
            BEAST_EXPECT(env.le(broker.brokerKeylet()));
            BEAST_EXPECT(env.le(loanKeylet));

            // Delete the pending loan, then the broker can be deleted.
            env(del(lender_, loanKeylet.key));
            env.close();
            env(jtx::loan_broker::del(lender_, broker.brokerID));
            env.close();
            BEAST_EXPECT(!env.le(broker.brokerKeylet()));
        }

        {
            testcase("Two-step: two pending loans coexist on the same broker");

            // Two pending proposals from the same broker each contribute
            // independently to DebtTotal, AssetsReserved, and OwnerCount.
            // Deleting one pending loan must leave the other's bookkeeping
            // untouched.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            Number const principal = broker.asset(200).number();
            auto const books0 = readBooks(env, broker);

            // Propose L1 (borrower) to establish a baseline delta.
            auto const l1Keylet = proposeAndClose(env, broker, borrower_);
            auto const books1 = readBooks(env, broker);
            Books::Delta const proposalDelta{
                .available = -principal,
                .reserved = principal,
                .debtTotal = books1.debtTotal - books0.debtTotal,
                .brokerOwners = 1,
                .lenderOwners = 1};
            BEAST_EXPECT(books1 == books0.adjusted(proposalDelta));

            // Propose L2 (evan) on the same broker while L1 is still
            // pending. Each proposal contributes an equal delta.
            auto const l2Keylet = proposeAndClose(env, broker, evan_);
            BEAST_EXPECT(readBooks(env, broker) == books1.adjusted(proposalDelta));
            expectLoanPending(env, l1Keylet);
            expectLoanPending(env, l2Keylet);

            // Delete L1. L2's bookkeeping is untouched: the books carry
            // exactly one proposal again.
            env(del(lender_, l1Keylet.key));
            env.close();
            BEAST_EXPECT(!env.le(l1Keylet));
            BEAST_EXPECT(readBooks(env, broker) == books1);
            expectLoanPending(env, l2Keylet);
        }

        {
            testcase("Two-step: DebtMaximum constrains a second pending proposal");

            // A pending loan counts toward DebtMaximum. With DebtMaximum set
            // to L1's DebtTotal, a same-sized L2 is rejected with
            // tecLIMIT_EXCEEDED.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const l1Keylet = proposeAndClose(env, broker, borrower_);
            Number const debtAfterL1 = readBooks(env, broker).debtTotal;

            // Tighten DebtMaximum to exactly L1's DebtTotal.
            env(jtx::loan_broker::set(lender_, broker.vaultID),
                jtx::loan_broker::kLoanBrokerId(broker.brokerID),
                jtx::loan_broker::kDebtMaximum(debtAfterL1));
            env.close();

            // Second proposal exceeds the debt cap.
            auto const l2Keylet = propose(env, broker, evan_, Ter(tecLIMIT_EXCEEDED));
            env.close();

            // L1 remains pending; L2 was not created.
            expectLoanPending(env, l1Keylet);
            BEAST_EXPECT(!env.le(l2Keylet));
        }

        // A Batch containing an inner two-step LoanSet (Borrower + StartDate,
        // no Counterparty or CounterpartySignature), signed by the LoanBroker
        // owner. While ttLOAN_SET is on Batch::kDisabledTxTypes the batch is
        // rejected with temINVALID_INNER_BATCH; otherwise it creates a
        // pending loan.
        {
            bool const lendingBatchEnabled = !std::ranges::any_of(
                Batch::kDisabledTxTypes,
                [](auto const& disabled) { return disabled == ttLOAN_SET; });

            testcase(
                lendingBatchEnabled
                    ? "Two-step: Batch inner LoanSet creates a pending loan"
                    : "Two-step: Batch inner LoanSet rejected while ttLOAN_SET is disabled");

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            Number const principal = broker.asset(200).number();
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();

            auto const books0 = readBooks(env, broker);
            auto const loanKeylet = nextLoanKeylet(env, broker);
            auto const lenderSeq = env.seq(lender_);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            env(batch::outer(lender_, lenderSeq, batchFee, tfAllOrNothing),
                batch::Inner(
                    env.json(
                        set(lender_, broker.brokerID, principal),
                        kBorrower(borrower_.id()),
                        kStartDate(startDate),
                        kInterestRate(kProposalInterest),
                        kPaymentTotal(kProposalPayTotal),
                        kPaymentInterval(kProposalPayInterval),
                        Sig(kNone),
                        Fee(kNone),
                        Seq(kNone)),
                    lenderSeq + 1),
                batch::Inner(pay(lender_, borrower_, XRP(1)), lenderSeq + 2),
                Ter(lendingBatchEnabled ? TER(tesSUCCESS) : TER(temINVALID_INNER_BATCH)));
            env.close();

            if (lendingBatchEnabled)
            {
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                {
                    BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                    BEAST_EXPECT(loan->at(sfBorrower) == borrower_.id());
                    BEAST_EXPECT(loan->at(sfStartDate) == startDate);
                }

                // Broker bookkeeping matches a non-batch two-step proposal:
                // DebtTotal grows by principal + interestDue, and OwnerCount
                // grows by one (the pending loan).
                auto const books1 = readBooks(env, broker);
                BEAST_EXPECT(books1.debtTotal > books0.debtTotal);
                BEAST_EXPECT(books1.brokerOwners == books0.brokerOwners + 1);
            }
            else
            {
                // No loan was created and the books are unchanged.
                BEAST_EXPECT(!env.le(loanKeylet));
                BEAST_EXPECT(readBooks(env, broker) == books0);
            }
        }

        // A Batch containing an inner LoanAccept. While ttLOAN_ACCEPT is on
        // Batch::kDisabledTxTypes the batch is rejected with
        // temINVALID_INNER_BATCH; otherwise it accepts the pending loan.
        {
            bool const lendingBatchEnabled = !std::ranges::any_of(
                Batch::kDisabledTxTypes,
                [](auto const& disabled) { return disabled == ttLOAN_ACCEPT; });

            testcase(
                lendingBatchEnabled
                    ? "Two-step: Batch inner LoanAccept accepts the pending loan"
                    : "Two-step: Batch inner LoanAccept rejected while ttLOAN_ACCEPT is disabled");

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            auto const borrowerSeq = env.seq(borrower_);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            env(batch::outer(borrower_, borrowerSeq, batchFee, tfAllOrNothing),
                batch::Inner(accept(borrower_, loanKeylet.key), borrowerSeq + 1),
                batch::Inner(pay(borrower_, evan_, XRP(1)), borrowerSeq + 2),
                Ter(lendingBatchEnabled ? TER(tesSUCCESS) : TER(temINVALID_INNER_BATCH)));
            env.close();

            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            // While the type is disabled the loan is still pending.
            BEAST_EXPECT(loan->isFlag(lsfLoanPending) != lendingBatchEnabled);
        }

        // After a full two-step lifecycle (propose, accept, full pay, delete)
        // on a cash-basis vault, the balance sheet must close out: no
        // reserved principal, no debt, AssetsTotal == AssetsAvailable.
        for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: cash-basis balance sheet closes out ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features_);
            auto const broker = makeBroker(env, assetType);

            auto const books0 = readBooks(env, broker);
            auto const loanKeylet = proposeAndClose(env, broker, borrower_);
            acceptLoan(env, loanKeylet);

            // For IOU / MPT, fund the borrower with enough to cover principal
            // plus interest.
            if (!broker.asset.native())
            {
                env(pay(issuer_, borrower_, broker.asset(400)));
                env.close();
            }

            // Pay the loan off in full while the first payment is still on
            // time.
            env(pay(borrower_, loanKeylet.key, broker.asset(400), tfLoanFullPayment));
            env.close();
            env(del(borrower_, loanKeylet.key));
            env.close();

            // No reserved principal, no outstanding debt, and AssetsTotal
            // equals AssetsAvailable.
            auto const books1 = readBooks(env, broker);
            BEAST_EXPECT(books1.reserved == beast::kZero);
            BEAST_EXPECT(books1.available == books1.total);
            BEAST_EXPECT(books1.debtTotal == beast::kZero);
            // The vault ends with at least what it started with.
            BEAST_EXPECT(books1.total >= books0.total);
            BEAST_EXPECT(books1.available >= books0.available);
        }
    }

    // Edge-case scenarios that stress the interaction between two-step
    // proposals and other subsystems: closed-ended vault phase gate, cover
    // clawback bounded by pending debt, XRP precision loss, LoanSequence
    // rollover, and same-ledger propose+accept.
    void
    testTwoStepEdgeCases()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        // With StartDate still in the future, force the vault into the
        // Subscription or Redemption phase by rewriting its dates on the
        // open ledger. LoanAccept is rejected with tecTOO_SOON or tecEXPIRED
        // respectively and the loan stays pending.
        for (auto const scenario : {VaultPhase::Subscription, VaultPhase::Redemption})
        {
            char const* const phaseName =
                scenario == VaultPhase::Subscription ? "Subscription" : "Redemption";
            TER const expected =
                scenario == VaultPhase::Subscription ? TER{tecTOO_SOON} : TER{tecEXPIRED};
            testcase << "Two-step: LoanAccept rejected during " << phaseName
                     << " (StartDate not yet expired)";

            Env env(*this, features_);
            env.fund(XRP(100'000'000), noripple(lender_));
            env.fund(XRP(1'000'000), borrower_);
            env.close();

            BrokerParameters params{};
            params.vaultKind = VaultKind::ClosedEnded;
            params.subscriptionOffset = 60;
            // Far enough out for the loan schedule to end before RedemptionDate.
            params.redemptionOffset = 10u * 365u * 24u * 60u * 60u;
            auto const asset =
                createAsset(env, AssetType::XRP, params, issuer_, lender_, borrower_);
            auto const broker = createVaultAndBroker(env, asset, lender_, params);

            // Propose while the vault is in Investment, with StartDate 1h out.
            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            proposeLoan(env, broker, lender_, borrower_, startDate);
            env.close();
            expectLoanPending(env, loanKeylet);

            // Force the vault into the target phase on the open ledger. No
            // close between here and the LoanAccept, or the mutation is lost.
            std::uint32_t const parentClose =
                env.current()->parentCloseTime().time_since_epoch().count();
            auto const changed = modifyOpenLedgerEntry(env, broker.vaultKeylet(), [&](SLE& v) {
                if (scenario == VaultPhase::Subscription)
                {
                    // parentClose < SubscriptionDate puts the vault in
                    // Subscription.
                    v.setFieldU32(sfSubscriptionDate, parentClose + 600);
                }
                else
                {
                    // RedemptionDate < parentClose puts the vault in
                    // Redemption.
                    v.setFieldU32(sfRedemptionDate, parentClose - 1);
                }
            });
            if (!BEAST_EXPECT(changed))
                continue;

            // The open ledger reports the intended phase and StartDate is
            // still in the future.
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(getVaultPhase(*env.current(), v) == scenario);
            BEAST_EXPECT(parentClose < startDate);

            env(accept(borrower_, loanKeylet.key), Ter(expected));
            expectLoanPending(env, loanKeylet);
        }

        {
            testcase("Two-step: pending loan bounds cover clawback, LoanAccept still succeeds");

            // A pending loan's DebtTotal contribution raises the floor on
            // LoanBrokerCoverClawback: the clawback is capped at
            // CoverAvailable - DebtTotal * CoverRateMinimum. After the issuer
            // claws back to that minimum, LoanAccept still succeeds.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::IOU, /*enableClawback=*/true);

            BrokerParameters const defaults{};
            Number const coverMinRate =
                Number{defaults.coverRateMin.value()} / kTenthBipsPerUnity.value();

            // Snapshot CoverAvailable before the proposal.
            Number const cover0 = readBooks(env, broker).coverAvailable;

            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            // The pending loan has raised DebtTotal above zero.
            Number const debtWithPending = readBooks(env, broker).debtTotal;
            Number const expectedMinCover = debtWithPending * coverMinRate;
            BEAST_EXPECT(debtWithPending > beast::kZero);

            // Attempt to claw back the entire cover deposit. The withdrawal is
            // capped at the headroom.
            env(jtx::loan_broker::coverClawback(issuer_),
                jtx::loan_broker::kLoanBrokerId(broker.brokerID),
                kAmount(broker.asset(defaults.coverDeposit)));
            env.close();

            Number const coverAfter = readBooks(env, broker).coverAvailable;
            // Cover went down, but not below the minimum.
            BEAST_EXPECT(coverAfter < cover0);
            BEAST_EXPECT(coverAfter >= expectedMinCover);

            // LoanAccept succeeds with cover at the minimum.
            acceptLoan(env, loanKeylet);
        }

        {
            testcase("Two-step: pending loan blocks VaultDelete");

            // While a loan is pending, VaultDelete is rejected with
            // tecHAS_OBLIGATIONS. Deleting the pending loan restores the
            // vault's pre-proposal accounting.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const books0 = readBooks(env, broker);
            auto const loanKeylet = proposeAndClose(env, broker, borrower_);

            // AssetsReserved is non-zero and VaultDelete is rejected.
            BEAST_EXPECT(readBooks(env, broker).reserved > beast::kZero);
            Vault const vault{env};
            env(vault.del({.owner = lender_, .id = broker.vaultID}), Ter(tecHAS_OBLIGATIONS));
            env.close();

            // Deleting the pending loan returns the vault to its pre-proposal
            // snapshot.
            env(del(lender_, loanKeylet.key));
            env.close();
            BEAST_EXPECT(readBooks(env, broker) == books0);
        }

        {
            testcase("Two-step: precision loss on fractional origination fee (XRP)");

            // An origination fee that cannot be represented in the Vault.Asset
            // is rejected with tecPRECISION_LOSS.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            // 1.5 drops is not representable as XRP.
            auto const loanKeylet = propose(
                env,
                broker,
                borrower_,
                kLoanOriginationFee(Number{15, -1}),
                Ter(tecPRECISION_LOSS));
            env.close();
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Two-step: LoanAccept in same ledger as proposal");

            // Propose and accept in the same open ledger, without a close in
            // between. The accept succeeds.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = propose(env, broker, borrower_);
            // No env.close() here.
            acceptLoan(env, loanKeylet);
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFieldPresent(sfOwnerNode));
        }
    }

    // On a Legacy (accrual) vault, a rejected LoanAccept must leave the
    // proposal-time bookkeeping intact, with the interest carried only in
    // LoanBroker.DebtTotal and not yet in Vault.AssetsTotal, and deleting
    // the pending loan must reverse exactly that bookkeeping.
    void
    testTwoStepLegacyVault()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanAccept failure and pending LoanDelete (accrual, "
                     << assetTypeName(assetType) << ")";

            Env env(*this, features_);
            auto const broker = makeBroker(env, assetType);

            // The LEVersion rewrite does not survive a close. Nothing below
            // closes the ledger.
            makeVaultInstantRecognition(env, broker);

            Number const principal = broker.asset(200).number();
            auto const books0 = readBooks(env, broker);
            auto const loanKeylet = propose(env, broker, borrower_);

            // Under Legacy accounting the broker debt carries the interest
            // from proposal, but the vault does not recognise it until the
            // loan is accepted.
            auto const books1 = readBooks(env, broker);
            Number const interestDue = books1.debtTotal - books0.debtTotal - principal;
            BEAST_EXPECT(interestDue > beast::kZero);
            BEAST_EXPECT(
                books1 ==
                books0.adjusted(
                    {.available = -principal,
                     .reserved = principal,
                     .debtTotal = principal + interestDue,
                     .brokerOwners = 1,
                     .lenderOwners = 1}));

            // Make the acceptance fail: for XRP drain the borrower's reserve,
            // for IOU freeze the borrower's line, for MPT lock the borrower's
            // MPToken.
            TER expected = tesSUCCESS;
            switch (assetType)
            {
                case AssetType::XRP: {
                    auto const amt = env.balance(borrower_) -
                        accountReserve(*env.current(), borrower_.id(), env.journal);
                    env(pay(borrower_, evan_, amt));
                    expected = TER{tecINSUFFICIENT_RESERVE};
                    break;
                }
                case AssetType::IOU:
                    env(trust(issuer_, borrower_[iouCurrency_](0), tfSetFreeze));
                    expected = TER{tecFROZEN};
                    break;
                case AssetType::MPT: {
                    // close = false: a ledger close would discard the Legacy
                    // LEVersion rewrite made by makeVaultInstantRecognition.
                    MPTTester mptt{
                        env, issuer_, broker.asset.raw().get<MPTIssue>().getMptID(), {}, false};
                    mptt.set({.account = issuer_, .holder = borrower_, .flags = tfMPTLock});
                    expected = TER{tecLOCKED};
                    break;
                }
            }

            env(accept(borrower_, loanKeylet.key), Ter(expected));
            expectLoanPending(env, loanKeylet);

            // The rejected acceptance changed nothing: the interest is still
            // only in DebtTotal, not AssetsTotal, and the owner reserve is
            // still on the broker owner.
            BEAST_EXPECT(readBooks(env, broker) == books1);

            // Deleting the pending loan reverses the proposal exactly. The
            // proposal never touched AssetsTotal, so neither does the delete.
            env(del(lender_, loanKeylet.key));
            BEAST_EXPECT(!env.le(loanKeylet));
            BEAST_EXPECT(readBooks(env, broker) == books0);
        }
    }

    // On a Legacy (instant recognition) vault a pending loan's interest only
    // lands in AssetsTotal at acceptance. LoanSet's AssetsMaximum guard
    // passed at proposal, but another loan originated in between does not
    // see the pending interest and may use up the headroom that guard relied
    // on. LoanAccept must re-run the guard rather than push the vault past
    // AssetsMaximum. The whole scenario runs without a ledger close, because
    // the Legacy LEVersion rewrite does not survive one.
    void
    testTwoStepLegacyAssetsMaximum()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        for (bool const fillHeadroom : {false, true})
        {
            testcase << "Two-step: LoanAccept re-checks AssetsMaximum (accrual, "
                     << (fillHeadroom ? "headroom taken by a second loan" : "headroom intact")
                     << ")";

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);
            makeVaultInstantRecognition(env, broker);

            Number const principal = broker.asset(200).number();
            auto const books0 = readBooks(env, broker);

            // Propose, and read the interest the proposal carries off the
            // broker debt. The vault total is untouched by the proposal.
            auto const loanKeylet = propose(env, broker, borrower_);
            expectLoanPending(env, loanKeylet);
            auto const proposed = readBooks(env, broker);
            Number const interestDue = proposed.debtTotal - books0.debtTotal - principal;
            BEAST_EXPECT(interestDue > beast::kZero);
            BEAST_EXPECT(proposed.total == books0.total);

            // Leave room for one and a half loans' worth of interest: the
            // pending loan and one more immediately active loan each fit on
            // their own, but not both. The cap must sit on the asset grid.
            {
                Number assetsMaximum = books0.total + interestDue + interestDue / 2;
                roundToAsset(broker.asset.raw(), assetsMaximum);
                Vault const vault{env};
                auto tx = vault.set({.owner = lender_, .id = broker.vaultID});
                tx[sfAssetsMaximum] = assetsMaximum;
                env(tx);
            }

            if (fillHeadroom)
            {
                // A second, immediately active loan on the same terms. Its
                // AssetsMaximum guard does not count the pending interest, so
                // it takes the headroom and books its own interest into
                // AssetsTotal.
                auto const secondKeylet = nextLoanKeylet(env, broker);
                env(set(borrower_, broker.brokerID, principal),
                    kInterestRate(kProposalInterest),
                    kPaymentTotal(kProposalPayTotal),
                    kPaymentInterval(kProposalPayInterval),
                    Sig(sfCounterpartySignature, lender_),
                    Fee(env.current()->fees().base * 2));
                expectLoanActive(env, secondKeylet);
                BEAST_EXPECT(readBooks(env, broker).total == books0.total + interestDue);
            }

            auto const books1 = readBooks(env, broker);

            if (fillHeadroom)
            {
                // Accepting now would push AssetsTotal past AssetsMaximum.
                // Nothing changes.
                env(accept(borrower_, loanKeylet.key), Ter(tecLIMIT_EXCEEDED));
                expectLoanPending(env, loanKeylet);
                BEAST_EXPECT(readBooks(env, broker) == books1);

                // Deleting the proposal leaves AssetsTotal where the active
                // loan put it: there is no interest to unwind.
                env(del(lender_, loanKeylet.key));
                BEAST_EXPECT(!env.le(loanKeylet));
                auto const books2 = readBooks(env, broker);
                BEAST_EXPECT(books2.available == books1.available + principal);
                BEAST_EXPECT(books2.reserved == books1.reserved - principal);
                BEAST_EXPECT(books2.total == books1.total);
            }
            else
            {
                // With the headroom intact the acceptance books the interest
                // into AssetsTotal and stays under AssetsMaximum. The owner
                // reserve moves from the broker owner to the borrower.
                env(accept(borrower_, loanKeylet.key));
                expectLoanActive(env, loanKeylet);

                auto const books2 = readBooks(env, broker);
                BEAST_EXPECT(
                    books2 ==
                    books1.adjusted(
                        {.reserved = -principal,
                         .total = interestDue,
                         .lenderOwners = -1,
                         .borrowerOwners = 1}));
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                    BEAST_EXPECT(books2.total <= v->at(sfAssetsMaximum));
            }
        }
    }

    // On a FixedPrecision vault a pending loan's interest only lands in
    // YieldUnrealized at acceptance. LoanSet's Open-zone guard passed at
    // proposal, but another loan originated in between does not see the
    // pending interest and may use up the headroom that guard relied on.
    // (A deposit cannot: brokers sit on closed-ended vaults, which refuse
    // deposits once in the Investment phase.) LoanAccept must re-run the
    // guard rather than push the vault past the Open zone.
    void
    testTwoStepFixedPrecisionOpenZone()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        for (bool const fillHeadroom : {false, true})
        {
            testcase << "Two-step: LoanAccept re-checks the FixedPrecision Open zone ("
                     << (fillHeadroom ? "headroom taken by a second loan" : "headroom intact")
                     << ")";

            Env env(*this, features_);
            env.fund(XRP(100'000'000), noripple(lender_));
            env.fund(XRP(1'000'000), issuer_, borrower_, evan_);
            env.close();

            // Scale 6 puts the Open-zone ceiling at 9e9 units.
            std::uint8_t const vaultScale{6};
            Number const openLimit{9, 9};
            PrettyAsset const asset{issuer_[iouCurrency_]};
            Number const principal = asset(200).number();

            BrokerParameters params{};
            params.vaultScale = vaultScale;

            // The interest the proposal will carry, computed as LoanSet does.
            auto const properties = computeLoanProperties(
                env.current()->rules(),
                asset.raw(),
                principal,
                kProposalInterest,
                kProposalPayInterval,
                kProposalPayTotal,
                params.managementFeeRate,
                -static_cast<std::int32_t>(vaultScale));
            Number const interestDue = properties.loanState.interestDue;
            BEAST_EXPECT(interestDue > beast::kZero);

            // Fill the vault so that exactly interestDue of Open-zone headroom
            // remains: the proposal fits, and nothing more.
            params.vaultDeposit = openLimit - interestDue;
            createAsset(env, AssetType::IOU, params, issuer_, lender_, borrower_);
            env.close();
            env(pay(
                issuer_, lender_, asset(params.vaultDeposit + params.coverDeposit + interestDue)));
            env.close();
            auto const broker = createVaultAndBroker(env, asset, lender_, params);
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
            {
                BEAST_EXPECT(getVaultVersion(v) == VaultVersion::FixedPrecision);
                BEAST_EXPECT(getVaultOpenLimit(v) == openLimit);
            }

            auto const loanKeylet = proposeAndClose(env, broker, borrower_);
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(constructLoanState(loan).interestDue == interestDue);
            }
            // While pending, the interest is not yet in YieldUnrealized.
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(v->at(sfYieldUnrealized) == beast::kZero);

            if (fillHeadroom)
            {
                // A second, immediately active loan on the same terms. Its
                // Open-zone guard does not count the pending interest, so it
                // takes the last of the headroom and books its own interest
                // into YieldUnrealized.
                auto const secondKeylet = nextLoanKeylet(env, broker);
                env(set(borrower_, broker.brokerID, principal),
                    kInterestRate(kProposalInterest),
                    kPaymentTotal(kProposalPayTotal),
                    kPaymentInterval(kProposalPayInterval),
                    Sig(sfCounterpartySignature, lender_),
                    Fee(env.current()->fees().base * 2));
                env.close();
                expectLoanActive(env, secondKeylet);
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                {
                    BEAST_EXPECT(v->at(sfYieldUnrealized) == interestDue);
                    BEAST_EXPECT(vaultOpenZoneCapacity(v, kNumZero) == openLimit);
                }
            }

            auto const books1 = readBooks(env, broker);

            if (fillHeadroom)
            {
                // Accepting now would push the Open-zone capacity past the
                // ceiling. Nothing changes.
                env(accept(borrower_, loanKeylet.key), Ter(tecLIMIT_EXCEEDED));
                env.close();
                expectLoanPending(env, loanKeylet);
                BEAST_EXPECT(readBooks(env, broker) == books1);
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                    BEAST_EXPECT(v->at(sfYieldUnrealized) == interestDue);
            }
            else
            {
                // With the headroom intact the capacity lands exactly on the
                // ceiling, which the guard allows.
                acceptLoan(env, loanKeylet);
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                {
                    BEAST_EXPECT(v->at(sfYieldUnrealized) == interestDue);
                    BEAST_EXPECT(vaultOpenZoneCapacity(v, kNumZero) == openLimit);
                }
                BEAST_EXPECT(readBooks(env, broker).reserved == books1.reserved - principal);
            }
        }
    }

    // Runs every two-step scenario under `featuresToTest`. The
    // amendment-disabled case lives in LoanValidation::testDisabled.
    void
    testTwoStep(FeatureBitset const& featuresToTest)
    {
        features_ = featuresToTest;

        testTwoStepBasics();
        testTwoStepBeforeStartDate();
        testTwoStepFreeze();
        testTwoStepPendingLifecycle();
        testTwoStepLegacyVault();
        testTwoStepLegacyAssetsMaximum();
        testTwoStepEdgeCases();
        testTwoStepFixedPrecisionOpenZone();
    }

public:
    void
    run() override
    {
        testTwoStep(all_ | featureLendingProtocolV1_1);
    }
};

BEAST_DEFINE_TESTSUITE(LoanTwoStep, tx, xrpl);

}  // namespace xrpl::test
