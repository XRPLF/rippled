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
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
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
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/lending/LoanAccept.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace xrpl::test {

class LoanTwoStep_test : public LoanTestBase
{
private:
    // Snapshot of the vault's asset accounting.
    struct VaultAmounts
    {
        Number available;
        Number reserved;
        Number total;
    };

    // Snapshot of the LoanBroker's own bookkeeping.
    struct BrokerAmounts
    {
        Number debtTotal;
        Number coverAvailable;
        std::uint32_t ownerCount{};
    };

    // Shared context used by every two-step scenario. The accounts and loan
    // terms are fixed; `features` is set by testTwoStep for each run.
    FeatureBitset features_;
    jtx::Account const issuer_{"issuer"};  // Issues the IOU / MPT assets
    jtx::Account const lender_{"lender"};  // Vault + LoanBroker owner
    jtx::Account const borrower_{"borrower"};
    jtx::Account const evan_{"evan"};  // unrelated third party

    // Loan terms shared across the scenarios. The principal is derived
    // from the broker's asset, so it adapts to XRP, IOU and MPT.
    TenthBips32 const interest_{50'000};
    std::uint32_t const payTotal_{10};
    std::uint32_t const payInterval_{200};

    // Build a funded environment with a Vault + LoanBroker owned by
    // `lender`, using the requested asset type, and return the broker.
    // When enableClawback is true and the asset is IOU, sets
    // asfAllowTrustLineClawback on the issuer before any trust lines exist.
    BrokerInfo
    makeBroker(jtx::Env& env, AssetType assetType, bool enableClawback = false)
    {
        using namespace jtx;
        env.fund(XRP(100'000'000), noripple(lender_));
        env.fund(XRP(1'000'000), borrower_, evan_);
        if (assetType != AssetType::XRP)
            env.fund(XRP(1'000'000), issuer_);
        env.close();
        if (enableClawback && assetType == AssetType::IOU)
        {
            env(fset(issuer_, asfAllowTrustLineClawback));
            env.close();
        }
        BrokerParameters const params{};
        auto const asset = createAsset(env, assetType, params, issuer_, lender_, borrower_);
        env.close();
        if (!asset.native())
            env(pay(issuer_, lender_, asset(params.vaultDeposit + params.coverDeposit)));
        env.close();
        return createVaultAndBroker(env, asset, lender_, params);
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
    // simulate a vault created before LendingProtocolV1_1.
    static void
    makeVaultInstantRecognition(jtx::Env& env, BrokerInfo const& broker)
    {
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
            Sandbox sb(&view, TapNone);
            auto v = sb.peek(broker.vaultKeylet());
            if (!v)
                return false;
            v->setFieldU8(sfLEVersion, std::to_underlying(VaultVersion::Legacy));
            sb.update(v);
            sb.apply(view);
            return true;
        });
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

    // The keylet of the next loan the broker will create.
    static Keylet
    nextLoanKeylet(jtx::Env& env, BrokerInfo const& broker)
    {
        auto const brokerSle = env.le(broker.brokerKeylet());
        return keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
    }

    static VaultAmounts
    readVault(jtx::Env& env, BrokerInfo const& broker)
    {
        auto const v = env.le(broker.vaultKeylet());
        return {
            .available = v->at(sfAssetsAvailable),
            .reserved = v->at(sfAssetsReserved),
            .total = v->at(sfAssetsTotal)};
    }

    static BrokerAmounts
    readBroker(jtx::Env& env, BrokerInfo const& broker)
    {
        auto const b = env.le(broker.brokerKeylet());
        return {
            .debtTotal = b->at(sfDebtTotal),
            .coverAvailable = b->at(sfCoverAvailable),
            .ownerCount = b->at(sfOwnerCount)};
    }

    // Submit a valid two-step proposal from `proposer` on behalf of
    // `theBorrower`, with the supplied StartDate and any extra functors.
    template <typename... Extra>
    void
    propose(
        jtx::Env& env,
        BrokerInfo const& broker,
        jtx::Account const& proposer,
        jtx::Account const& theBorrower,
        std::uint32_t startDate,
        Extra const&... extra)
    {
        using namespace jtx;
        using namespace jtx::loan;
        env(set(proposer, broker.brokerID, broker.asset(200).number()),
            kBorrower(theBorrower),
            kStartDate(startDate),
            kInterestRate(interest_),
            kPaymentTotal(payTotal_),
            kPaymentInterval(payInterval_),
            extra...);
    }

    // A failed LoanAccept must leave the Loan in place and still pending.
    void
    expectStillPending(jtx::Env& env, Keylet const& k)
    {
        if (auto const loan = env.le(k); BEAST_EXPECT(loan))
            BEAST_EXPECT(loan->isFlag(lsfLoanPending));
    }

    // Amendment disabled: the two-step fields and LoanAccept are gated off.
    void
    testTwoStepAmendmentDisabled()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        testcase("Two-step: rejected as before");

        Env env(*this, features_);
        auto const broker = makeBroker(env, AssetType::XRP);
        // With the amendment disabled, a LoanSet carrying Borrower/StartDate
        // is rejected with temDISABLED.
        propose(
            env,
            broker,
            lender_,
            borrower_,
            (env.now() + 1h).time_since_epoch().count(),
            Ter(temDISABLED));

        // With the amendment disabled, a LoanSet without CounterpartySignature
        // is rejected with temBAD_SIGNER.
        env(set(lender_, broker.brokerID, broker.asset(200).number()), Ter(temBAD_SIGNER));

        // With the amendment disabled, LoanAccept is rejected with temDISABLED.
        env(accept(borrower_, keylet::loan(broker.brokerID, SeqProxy::rawSequence(1)).key),
            Ter(temDISABLED));
    }

    // Successful propose / accept flows across all three asset types, the
    // origination-fee variant, the accepted-loan lifecycle, and the
    // pending-loan / LoanPay coexistence regression.
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
                {
                    makeVaultInstantRecognition(env, broker);
                    // The vault must now resolve to VaultVersion::Legacy.
                    if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                        BEAST_EXPECT(getVaultVersion(v) == VaultVersion::Legacy);
                }
                Number const principal = broker.asset(200).number();

                auto const vault0 = readVault(env, broker);
                auto const broker0 = readBroker(env, broker);
                auto const lenderOwners0 = env.ownerCount(lender_);
                auto const borrowerOwners0 = env.ownerCount(borrower_);

                auto const loanKeylet = nextLoanKeylet(env, broker);
                // A StartDate comfortably in the future.
                propose(
                    env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
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

                // The owner reserve is charged to the broker owner, not the
                // borrower.
                BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0 + 1);
                BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners0);

                // Vault bookkeeping: Available -= P, Reserved += P. Total is
                // unchanged under both models: a proposal books no interest,
                // so deleting it cannot move the share price.
                auto const vault1 = readVault(env, broker);
                BEAST_EXPECT(vault1.available == vault0.available - principal);
                BEAST_EXPECT(vault1.reserved == vault0.reserved + principal);
                BEAST_EXPECT(vault1.total == vault0.total);
                // The Vault version must be unchanged by the proposal.
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                    BEAST_EXPECT(getVaultVersion(v) == vaultVersion);

                // Broker bookkeeping: DebtTotal += P + InterestDue (InterestDue
                // is zero under cash-basis), OwnerCount += 1, CoverAvailable
                // unchanged.
                auto const broker1 = readBroker(env, broker);
                BEAST_EXPECT(broker1.debtTotal >= broker0.debtTotal + principal);
                Number const interestDue = broker1.debtTotal - broker0.debtTotal - principal;
                if (vaultVersion == VaultVersion::Legacy)
                {
                    BEAST_EXPECT(interestDue > beast::kZero);
                }
                else
                {
                    BEAST_EXPECT(interestDue == beast::kZero);
                }
                BEAST_EXPECT(broker1.ownerCount == broker0.ownerCount + 1);
                BEAST_EXPECT(broker1.coverAvailable == broker0.coverAvailable);

                // Capture pre-acceptance balances to verify disbursement.
                auto const vaultPseudo = [&]() {
                    auto const v = env.le(broker.vaultKeylet());
                    return Account("vault pseudo-account", v->at(sfAccount));
                }();
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
                // borrower.
                BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0);
                BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners0 + 1);

                // Reserved principal is released; Available is unchanged from
                // the proposal. Total grows by InterestDue under accrual, now
                // that the loan is active, and is unchanged under cash-basis.
                auto const vault2 = readVault(env, broker);
                BEAST_EXPECT(vault2.reserved == vault0.reserved);
                BEAST_EXPECT(vault2.available == vault0.available - principal);
                BEAST_EXPECT(vault2.total == vault0.total + interestDue);

                // Broker bookkeeping: acceptance leaves DebtTotal, OwnerCount,
                // and CoverAvailable unchanged from the pending snapshot.
                auto const broker2 = readBroker(env, broker);
                BEAST_EXPECT(broker2.debtTotal == broker1.debtTotal);
                BEAST_EXPECT(broker2.ownerCount == broker1.ownerCount);
                BEAST_EXPECT(broker2.coverAvailable == broker1.coverAvailable);

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

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(
                env,
                broker,
                lender_,
                borrower_,
                (env.now() + 1h).time_since_epoch().count(),
                kLoanOriginationFee(originationFee));
            env.close();

            // The pending loan records the origination fee.
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->at(sfLoanOriginationFee) == originationFee);
            }

            auto const vaultPseudo = [&]() {
                auto const v = env.le(broker.vaultKeylet());
                return Account("vault pseudo-account", v->at(sfAccount));
            }();
            STAmount const pseudoBal0 = env.balance(vaultPseudo, broker.asset).value();
            STAmount const borrowerBal0 = env.balance(borrower_, broker.asset).value();
            STAmount const lenderBal0 = env.balance(lender_, broker.asset).value();

            env(accept(borrower_, loanKeylet.key));
            env.close();

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

        {
            testcase("Two-step: accepted loan behaves as a normal loan");

            // An accepted two-step loan can be paid, impaired, unimpaired and
            // deleted like a one-step loan.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender_, borrower_, startDate);
            env.close();

            env(accept(borrower_, loanKeylet.key));
            env.close();

            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->at(sfPaymentRemaining) == payTotal_);
            }

            // LoanPay: a regular payment within the first payment interval
            // succeeds.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 30s);
            env(pay(borrower_, loanKeylet.key, broker.asset(30)));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->at(sfPaymentRemaining) < payTotal_);

            // LoanManage: once the payment is late, the loan can be impaired
            // and then unimpaired.
            advancePastDueDate(env, loanKeylet);
            env(manage(lender_, loanKeylet.key, tfLoanImpair));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));

            env(manage(lender_, loanKeylet.key, tfLoanUnimpair));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));

            // The loan is still overdue after unimpairing: catch it up with a
            // late payment, then pay it off.
            env(pay(borrower_, loanKeylet.key, broker.asset(400), tfLoanLatePayment));
            env.close();

            // A generous upper bound (2x principal) clears principal + interest.
            env(pay(borrower_, loanKeylet.key, broker.asset(400), tfLoanFullPayment));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->at(sfPaymentRemaining) == 0);

            // LoanDelete succeeds once the loan is fully paid.
            env(del(borrower_, loanKeylet.key));
            env.close();
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Two-step: LoanPay on accepted loan while another loan is pending");

            // A LoanPay on an accepted loan must succeed while another loan is
            // pending and AssetsReserved is non-zero.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            // L1: accepted (borrower) — disburses principal, drains
            // AssetsReserved back to 0.
            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();
            env(accept(borrower_, l1Keylet.key));
            env.close();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(!l1->isFlag(lsfLoanPending));

            // L2: still pending (evan) — leaves AssetsReserved > 0.
            propose(env, broker, lender_, evan_, (env.now() + 1h).time_since_epoch().count());
            env.close();
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(v->at(sfAssetsReserved) > beast::kZero);

            // A payment on L1 must succeed with L2 still pending.
            env(pay(borrower_, l1Keylet.key, broker.asset(30)));
            env.close();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->at(sfPaymentRemaining) < payTotal_);
        }

        // Impairing an active loan must succeed while another loan on the same
        // vault is pending. The unrealized loss is bounded by the assets lent
        // out by active loans: AssetsTotal - AssetsAvailable - AssetsReserved.
        // The reserved principal of the pending loan is still held by the
        // vault, so the impairment must not be able to discount it.
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
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
            {
                BEAST_EXPECT(
                    getVaultVersion(v) ==
                    (legacyVault ? VaultVersion::Legacy : VaultVersion::FixedPrecision));
            }

            // L1: accepted (borrower).
            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            closeIfAllowed();
            env(accept(borrower_, l1Keylet.key));
            closeIfAllowed();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(!l1->isFlag(lsfLoanPending));

            // L2: still pending (evan) — leaves AssetsReserved > 0.
            auto const l2Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, evan_, (env.now() + 2h).time_since_epoch().count());
            closeIfAllowed();
            expectStillPending(env, l2Keylet);

            auto const vault0 = readVault(env, broker);
            Number const l2Principal = broker.asset(200).number();
            BEAST_EXPECT(vault0.reserved == l2Principal);

            // Impair L1 while L2 is pending. Under fixCleanup3_4_0 the payment
            // must already be late.
            advancePastDueDate(env, l1Keylet);
            env(manage(lender_, l1Keylet.key, tfLoanImpair));
            closeIfAllowed();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->isFlag(lsfLoanImpaired));

            // The loss covers L1 only, and stays within the assets lent out by
            // active loans; the reserved principal of L2 is untouched.
            auto const vault1 = readVault(env, broker);
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
            {
                Number const loss = v->at(sfLossUnrealized);
                BEAST_EXPECT(loss > beast::kZero);
                BEAST_EXPECT(loss <= vault1.total - vault1.available - vault1.reserved);
            }
            BEAST_EXPECT(vault1.reserved == vault0.reserved);
            BEAST_EXPECT(vault1.available == vault0.available);
            expectStillPending(env, l2Keylet);

            // L2 can still be accepted: its principal was never discounted by
            // the impairment, and acceptance releases it from AssetsReserved.
            env(accept(evan_, l2Keylet.key));
            closeIfAllowed();
            if (auto const l2 = env.le(l2Keylet); BEAST_EXPECT(l2))
                BEAST_EXPECT(!l2->isFlag(lsfLoanPending));
            auto const vault2 = readVault(env, broker);
            BEAST_EXPECT(vault2.reserved == vault1.reserved - l2Principal);
            BEAST_EXPECT(vault2.available == vault1.available);
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
            {
                Number const loss = v->at(sfLossUnrealized);
                BEAST_EXPECT(loss <= vault2.total - vault2.available - vault2.reserved);
            }

            // Reversing the impairment on L1 clears the loss.
            env(manage(lender_, l1Keylet.key, tfLoanUnimpair));
            closeIfAllowed();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(!l1->isFlag(lsfLoanImpaired));
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
            {
                Number const loss = v->at(sfLossUnrealized);
                BEAST_EXPECT(loss == Number{0});
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

        // Propose a loan starting at `startDate` and accept it immediately.
        // Only the two closes advance the clock, so a StartDate an hour out
        // leaves the accepted loan unstarted. Returns the Loan's keylet.
        auto proposeAndAccept =
            [&](Env& env, BrokerInfo const& broker, std::uint32_t startDate, auto const&... extra) {
                auto const loanKeylet = nextLoanKeylet(env, broker);
                propose(env, broker, lender_, borrower_, startDate, extra...);
                env.close();
                env(accept(borrower_, loanKeylet.key));
                env.close();
                // The loan is accepted, and the first instalment is not due until
                // one payment interval after StartDate.
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                    BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(env.now().time_since_epoch().count() < startDate);
                return loanKeylet;
            };

        {
            testcase("Two-step: regular payment before StartDate");

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            auto const loanKeylet = proposeAndAccept(env, broker, startDate);

            auto const state = getCurrentState(env, broker, loanKeylet);
            BEAST_EXPECT(state.previousPaymentDate == 0);
            BEAST_EXPECT(state.nextPaymentDate == startDate + payInterval_);
            BEAST_EXPECT(state.paymentRemaining == payTotal_);

            // Nothing has accrued yet, so one periodic payment is enough to
            // settle the first instalment: no flag, and no extra funds.
            STAmount const due{
                broker.asset,
                roundPeriodicPayment(broker.asset, state.periodicPayment, state.loanScale)};
            auto const vault0 = readVault(env, broker);
            STAmount const borrowerBal0 = env.balance(borrower_, broker.asset).value();

            env(pay(borrower_, loanKeylet.key, due));
            env.close();

            // One instalment was taken, and the schedule moved on by one
            // interval from StartDate: the acceptance time plays no part.
            auto const state1 = getCurrentState(env, broker, loanKeylet);
            BEAST_EXPECT(state1.paymentRemaining == payTotal_ - 1);
            BEAST_EXPECT(state1.previousPaymentDate == startDate + payInterval_);
            BEAST_EXPECT(state1.nextPaymentDate == startDate + (2 * payInterval_));
            BEAST_EXPECT(state1.principalOutstanding < state.principalOutstanding);
            // The money reached the vault, and the borrower was charged no
            // more than the instalment (plus the transaction fee): starting
            // early costs nothing extra.
            BEAST_EXPECT(readVault(env, broker).available > vault0.available);
            BEAST_EXPECT(
                env.balance(borrower_, broker.asset).value() > borrowerBal0 - due - XRP(1).value());
        }

        {
            testcase("Two-step: overpayment before StartDate");

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            auto const loanKeylet =
                proposeAndAccept(env, broker, startDate, Txflags(tfLoanOverpayment));
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanOverpayment));

            auto const state = getCurrentState(env, broker, loanKeylet);
            STAmount const due{
                broker.asset,
                roundPeriodicPayment(broker.asset, state.periodicPayment, state.loanScale)};
            // The extra is smaller than a second instalment, so exactly one
            // scheduled payment is taken and the remainder is the overpayment.
            STAmount const extra = broker.asset(5).value();
            BEAST_EXPECT(extra < due);

            env(pay(borrower_, loanKeylet.key, due + extra, tfLoanOverpayment));
            env.close();

            auto const state1 = getCurrentState(env, broker, loanKeylet);
            // The overpayment consumes no scheduled payment of its own, so the
            // schedule advances by exactly one interval.
            BEAST_EXPECT(state1.paymentRemaining == payTotal_ - 1);
            BEAST_EXPECT(state1.previousPaymentDate == startDate + payInterval_);
            BEAST_EXPECT(state1.nextPaymentDate == startDate + (2 * payInterval_));
            // The extra came off the principal on top of the instalment's own
            // principal part, and the loan re-amortized to a smaller payment.
            BEAST_EXPECT(state1.principalOutstanding <= state.principalOutstanding - Number(extra));
            BEAST_EXPECT(state1.periodicPayment < state.periodicPayment);
        }

        {
            testcase("Two-step: full payment before StartDate charges principal only");

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);

            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            auto const loanKeylet = proposeAndAccept(env, broker, startDate);

            auto const state = getCurrentState(env, broker, loanKeylet);
            // The loan carries scheduled interest, none of which is owed on a
            // close this early: no time has passed since StartDate, and the
            // proposal set no close fee or close interest rate.
            BEAST_EXPECT(state.totalValue > state.principalOutstanding);
            STAmount const principal{broker.asset, state.principalOutstanding};
            STAmount const borrowerBal0 = env.balance(borrower_, broker.asset).value();

            env(pay(borrower_,
                    loanKeylet.key,
                    principal - STAmount{broker.asset, 1},
                    tfLoanFullPayment),
                Ter(tecINSUFFICIENT_PAYMENT));
            env.close();

            env(pay(borrower_, loanKeylet.key, principal, tfLoanFullPayment));
            env.close();

            // The loan is fully paid.
            auto const state1 = getCurrentState(env, broker, loanKeylet);
            BEAST_EXPECT(state1.paymentRemaining == 0);
            BEAST_EXPECT(state1.principalOutstanding == beast::kZero);
            BEAST_EXPECT(state1.totalValue == beast::kZero);

            // Only the principal was taken from the borrower. The comparison
            // allows for the transaction fees of the two LoanPays above.
            STAmount const borrowerBal1 = env.balance(borrower_, broker.asset).value();
            BEAST_EXPECT(borrowerBal1 < borrowerBal0 - principal);
            BEAST_EXPECT(borrowerBal1 > borrowerBal0 - principal - XRP(1).value());

            // The vault and the broker are back where they were before the
            // proposal: a loan closed before it starts earns the vault nothing.
            auto const vault1 = readVault(env, broker);
            auto const broker1 = readBroker(env, broker);
            BEAST_EXPECTS(
                vault1.available == vault0.available,
                "AssetsAvailable: " + to_string(vault1.available) +
                    " != " + to_string(vault0.available));
            BEAST_EXPECT(vault1.reserved == vault0.reserved);
            BEAST_EXPECTS(
                vault1.total == vault0.total,
                "AssetsTotal: " + to_string(vault1.total) + " != " + to_string(vault0.total));
            BEAST_EXPECT(broker1.debtTotal == broker0.debtTotal);
            BEAST_EXPECT(broker1.coverAvailable == broker0.coverAvailable);

            // Nothing is outstanding, so the loan can now be deleted.
            env(del(borrower_, loanKeylet.key));
            env.close();
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Two-step: impair, default and late payment rejected before StartDate");

            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            auto const loanKeylet = proposeAndAccept(env, broker, startDate);

            auto const state = getCurrentState(env, broker, loanKeylet);

            // The first instalment is not due until one interval after
            // StartDate, so there is nothing yet to impair or default on.
            env(manage(lender_, loanKeylet.key, tfLoanImpair), Ter(tecTOO_SOON));
            env.close();
            env(manage(lender_, loanKeylet.key, tfLoanDefault), Ter(tecTOO_SOON));
            env.close();

            // A payment flagged as late is rejected for the same reason.
            STAmount const due{
                broker.asset,
                roundPeriodicPayment(broker.asset, state.periodicPayment, state.loanScale)};
            env(pay(borrower_, loanKeylet.key, due + due, tfLoanLatePayment), Ter(tecTOO_SOON));
            env.close();

            // Neither party can simply walk away from the accepted loan: it
            // has to be paid off first.
            env(del(borrower_, loanKeylet.key), Ter(tecHAS_OBLIGATIONS));
            env(del(lender_, loanKeylet.key), Ter(tecHAS_OBLIGATIONS));
            env.close();

            // None of the failures changed the loan.
            auto const state1 = getCurrentState(env, broker, loanKeylet);
            BEAST_EXPECT((state1.flags & (lsfLoanImpaired | lsfLoanDefault)) == 0);
            BEAST_EXPECT(state1.nextPaymentDate == state.nextPaymentDate);
            BEAST_EXPECT(state1.paymentRemaining == payTotal_);
            BEAST_EXPECT(state1.principalOutstanding == state.principalOutstanding);
        }
    }

    // Which account has its IOU trust line frozen or its MPToken locked in
    // the freeze scenarios of testTwoStepFreeze.
    enum class FreezeTarget { VaultPseudo, BrokerPseudo, Borrower, BrokerOwner };

    struct FreezeCase
    {
        char const* label;
        FreezeTarget target;
        std::uint32_t trustFlags;  // TrustSet flags used for the IOU case
    };

    // Map a FreezeTarget to the account it refers to in this environment.
    jtx::Account
    resolveFreezeTarget(jtx::Env& env, BrokerInfo const& broker, FreezeTarget target) const
    {
        using namespace jtx;
        switch (target)
        {
            case FreezeTarget::VaultPseudo: {
                auto const v = env.le(broker.vaultKeylet());
                return Account("vault pseudo-account", v->at(sfAccount));
            }
            case FreezeTarget::BrokerPseudo: {
                auto const b = env.le(broker.brokerKeylet());
                return Account("broker pseudo-account", b->at(sfAccount));
            }
            case FreezeTarget::Borrower:
                return borrower_;
            case FreezeTarget::BrokerOwner:
                return lender_;
        }
        UNREACHABLE("LoanTwoStep_test::resolveFreezeTarget : unknown target");
        return borrower_;
    }

    // Freeze `target`'s trust line (IOU, using `trustFlags`) or lock its
    // MPToken (MPT), close the ledger, and return the error code a
    // transaction touching that holding is expected to fail with.
    TER
    freezeHolding(
        jtx::Env& env,
        BrokerInfo const& broker,
        jtx::Account const& target,
        AssetType assetType,
        std::uint32_t trustFlags)
    {
        using namespace jtx;
        if (assetType == AssetType::IOU)
        {
            env(trust(issuer_, target[iouCurrency_](0), trustFlags));
            env.close();
            return TER{tecFROZEN};
        }
        MPTTester mptt{env, issuer_, broker.asset.raw().get<MPTIssue>().getMptID()};
        mptt.set({.account = issuer_, .holder = target, .flags = tfMPTLock});
        env.close();
        return TER{tecLOCKED};
    }

    // Freeze / deep-freeze / MPT lock / authorization scenarios across both
    // sides of the two-step flow (LoanSet at proposal time, LoanAccept at
    // acceptance time), plus the "cannot add holding" and reserve-drained
    // acceptance cases that share the same testing shape.
    void
    testTwoStepFreeze()
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        // Whether the freeze happens before LoanSet or between LoanSet and
        // LoanAccept.
        enum class FreezeStage { BeforeSet, BeforeAccept };

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

        // Freeze (IOU) or lock (MPT) one of the accounts involved in the
        // loan, either before LoanSet or between LoanSet and LoanAccept. In
        // the first case the proposal is rejected with tecFROZEN / tecLOCKED
        // and no pending Loan is created; in the second the acceptance is
        // rejected with the same code and the loan stays pending.
        for (auto const stage : {FreezeStage::BeforeSet, FreezeStage::BeforeAccept})
        {
            for (auto const& fc : freezeCases)
            {
                for (auto const assetType : {AssetType::IOU, AssetType::MPT})
                {
                    testcase << "Two-step: "
                             << (stage == FreezeStage::BeforeSet ? "LoanSet" : "LoanAccept")
                             << " with " << fc.label << " (" << assetTypeName(assetType) << ")";

                    Env env(*this, features_);
                    auto const broker = makeBroker(env, assetType);
                    auto const loanKeylet = nextLoanKeylet(env, broker);
                    if (stage == FreezeStage::BeforeAccept)
                    {
                        propose(
                            env,
                            broker,
                            lender_,
                            borrower_,
                            (env.now() + 1h).time_since_epoch().count());
                        env.close();
                    }

                    auto const target = resolveFreezeTarget(env, broker, fc.target);
                    TER const expected =
                        freezeHolding(env, broker, target, assetType, fc.trustFlags);

                    if (stage == FreezeStage::BeforeSet)
                    {
                        propose(
                            env,
                            broker,
                            lender_,
                            borrower_,
                            (env.now() + 1h).time_since_epoch().count(),
                            Ter(expected));
                        BEAST_EXPECT(!env.le(loanKeylet));
                    }
                    else
                    {
                        env(accept(borrower_, loanKeylet.key), Ter(expected));
                        expectStillPending(env, loanKeylet);
                    }
                }
            }
        }

        {
            testcase("Two-step: LoanAccept with insufficient reserve");

            // A Borrower without reserve for the Loan object is rejected with
            // tecINSUFFICIENT_RESERVE and the loan stays pending.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::IOU);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // Drain the borrower's XRP down to its current reserve.
            auto const amt = env.balance(borrower_) -
                accountReserve(*env.current(), borrower_.id(), env.journal);
            env(pay(borrower_, issuer_, amt));
            env.close();

            env(accept(borrower_, loanKeylet.key), Ter(tecINSUFFICIENT_RESERVE));
            expectStillPending(env, loanKeylet);
        }

        {
            testcase("Two-step: LoanAccept when a holding cannot be added (IOU)");

            // If the borrower has no trust line and the issuer has cleared
            // asfDefaultRipple when LoanAccept runs, the acceptance is
            // rejected with terNO_RIPPLE and the loan stays pending.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::IOU);
            Issue const iou = broker.asset.raw().get<Issue>();

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // Remove the borrower's trust line.
            auto const borrowerLine = keylet::trustLine(borrower_, iou);
            env.trust(broker.asset(0), borrower_);
            env.close();
            BEAST_EXPECT(!env.le(borrowerLine));

            env(fclear(issuer_, asfDefaultRipple));
            env.close();

            env(accept(borrower_, loanKeylet.key), Ter(terNO_RIPPLE));
            expectStillPending(env, loanKeylet);
        }

        {
            testcase("Two-step: LoanAccept canAddHolding gate uses the loan's origination fee");

            // The loan carries an origination fee. If the broker owner has no
            // trust line and the issuer has cleared asfDefaultRipple when
            // LoanAccept runs, the acceptance is rejected with terNO_RIPPLE,
            // both through the full pipeline and from LoanAccept::preclaim
            // called directly. The borrower keeps its line throughout.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::IOU);
            Issue const iou = broker.asset.raw().get<Issue>();

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(
                env,
                broker,
                lender_,
                borrower_,
                (env.now() + 1h).time_since_epoch().count(),
                kLoanOriginationFee(broker.asset(5).number()));
            env.close();

            // Remove the lender's trust line. Its balance is already zero, and
            // tfSetNoRipple puts the line in its default state.
            auto const lenderLine = keylet::trustLine(lender_, iou);
            env(trust(lender_, broker.asset(0), tfSetNoRipple));
            env.close();
            BEAST_EXPECT(!env.le(lenderLine));
            // The borrower's line is untouched.
            BEAST_EXPECT(env.le(keylet::trustLine(borrower_, iou)));

            env(fclear(issuer_, asfDefaultRipple));
            env.close();

            // Full pipeline: rejected, and the loan stays pending.
            env(accept(borrower_, loanKeylet.key), Ter(terNO_RIPPLE));
            expectStillPending(env, loanKeylet);

            // Direct preclaim against a scratch view of the same ledger.
            STTx const tx{ttLOAN_ACCEPT, [&](STObject& obj) {
                              obj.setAccountID(sfAccount, borrower_.id());
                              obj.setFieldH256(sfLoanID, loanKeylet.key);
                          }};
            OpenView ov{*env.current()};
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ApplyContext ac{
                env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};
            PreclaimContext const pctx{env.app(), ac.view(), tesSUCCESS, tx, TapNone, jlog};
            BEAST_EXPECT(LoanAccept::preclaim(pctx) == TER{terNO_RIPPLE});
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

        for (auto const& authCase : authCases)
        {
            testcase << "Two-step: LoanAccept with unauthorised " << authCase.label << " (MPT)";

            // If the issuer revokes the holder's MPToken authorisation
            // between the LoanSet proposal and LoanAccept, LoanAccept fails
            // with tecNO_AUTH and the loan stays pending.
            Env env(*this, features_);

            env.fund(XRP(1'000'000), issuer_, noripple(lender_), borrower_);
            env.close();

            MPTTester asset(
                {.env = env,
                 .issuer = issuer_,
                 .holders = {lender_, borrower_},
                 .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
                 .authHolder = true});

            env(pay(issuer_, lender_, asset(2'000'000)));
            env.close();

            auto const broker = createVaultAndBroker(env, asset, lender_);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            asset.authorize(
                {.account = issuer_, .holder = authCase.holder, .flags = tfMPTUnauthorize});
            env.close();

            env(accept(borrower_, loanKeylet.key), Ter(tecNO_AUTH));
            expectStillPending(env, loanKeylet);
        }

        for (auto const& authCase : authCases)
        {
            testcase << "Two-step: LoanAccept with unauthorised " << authCase.label << " (IOU)";

            // The issuer has asfRequireAuth. After the proposal the holder
            // deletes its authorised trust line: LoanAccept fails with
            // tecNO_LINE. The holder recreates the line, still unauthorised:
            // LoanAccept fails with tecNO_AUTH. Once the issuer authorises
            // the new line, LoanAccept succeeds. The loan stays pending
            // across both rejections and the other party's line is untouched.
            Env env(*this, features_);
            auto const broker = makeRequireAuthIouBroker(env);
            Issue const iou = broker.asset.raw().get<Issue>();
            Account const& other = authCase.holder == borrower_ ? lender_ : borrower_;

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // Zeroing the limit deletes the holder's line.
            auto const holderLine = keylet::trustLine(authCase.holder, iou);
            env(trust(authCase.holder, broker.asset(0), authCase.deleteFlags));
            env.close();
            BEAST_EXPECT(!env.le(holderLine));
            BEAST_EXPECT(env.le(keylet::trustLine(other, iou)));

            env(accept(borrower_, loanKeylet.key), Ter(tecNO_LINE));
            expectStillPending(env, loanKeylet);

            env(trust(authCase.holder, broker.asset(1'000'000)));
            env.close();
            BEAST_EXPECT(env.le(holderLine));
            env(accept(borrower_, loanKeylet.key), Ter(tecNO_AUTH));
            expectStillPending(env, loanKeylet);
            // Close before the issuer authorises the line. Otherwise the
            // rejected LoanAccept and the TrustSet share an open ledger and
            // are reordered canonically at close, letting the LoanAccept
            // succeed on replay.
            env.close();

            env(trust(issuer_, broker.asset(0), authCase.holder, tfSetfAuth));
            env.close();
            env(accept(borrower_, loanKeylet.key));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == broker.asset(200).value());
        }

        {
            testcase("Two-step: LoanAccept creating two MPTokens");

            // With an origination fee, and neither the borrower nor the broker
            // owner holding an MPToken, LoanAccept creates both MPTokens and
            // succeeds.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::MPT);
            auto const mptID = broker.asset.raw().get<MPTIssue>().getMptID();
            MPTTester mptt{env, issuer_, mptID};

            auto const borrowerMPToken = keylet::mptoken(mptID, borrower_);
            auto const lenderMPToken = keylet::mptoken(mptID, lender_);

            Number const originationFee = broker.asset(5).number();
            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(
                env,
                broker,
                lender_,
                borrower_,
                (env.now() + 1h).time_since_epoch().count(),
                kLoanOriginationFee(originationFee));
            env.close();

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

            env(accept(borrower_, loanKeylet.key));
            env.close();

            // Both holdings exist again and hold the disbursed amounts.
            if (!BEAST_EXPECT(env.le(borrowerMPToken) && env.le(lenderMPToken)))
                return;
            STAmount const netToBorrower{broker.asset, broker.asset(200).number() - originationFee};
            BEAST_EXPECT(env.balance(borrower_, broker.asset).value() == netToBorrower);
            BEAST_EXPECT(
                env.balance(lender_, broker.asset).value() ==
                STAmount(broker.asset, originationFee));

            auto const loan = env.le(loanKeylet);
            if (!BEAST_EXPECT(loan))
                return;
            BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
        }

        {
            testcase("Two-step: LoanSet with unauthorized broker owner (MPT)");

            // If the broker owner's MPToken authorization has been revoked
            // when LoanSet runs, the proposal is rejected with tecNO_AUTH and
            // no pending loan is created. The borrower stays authorised.
            Env env(*this, features_);

            env.fund(XRP(1'000'000), issuer_, noripple(lender_), borrower_);
            env.close();

            MPTTester asset(
                {.env = env,
                 .issuer = issuer_,
                 .holders = {lender_, borrower_},
                 .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
                 .authHolder = true});

            env(pay(issuer_, lender_, asset(2'000'000)));
            env.close();

            auto const broker = createVaultAndBroker(env, asset, lender_);

            // Zero the broker owner's MPT balance before revoking its
            // authorization.
            auto const lenderBalance = env.balance(lender_, broker.asset);
            env(pay(lender_, issuer_, lenderBalance));
            env.close();

            // Issuer revokes the broker owner's MPToken authorization.
            asset.authorize({.account = issuer_, .holder = lender_, .flags = tfMPTUnauthorize});
            env.close();

            auto const loanKeylet = nextLoanKeylet(env, broker);
            // A StartDate comfortably in the future.
            propose(
                env,
                broker,
                lender_,
                borrower_,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(tecNO_AUTH));

            // No pending loan was created.
            BEAST_EXPECT(!env.le(loanKeylet));
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

            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);
            auto const lenderOwners0 = env.ownerCount(lender_);
            auto const borrowerOwners0 = env.ownerCount(borrower_);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            // A StartDate comfortably in the future.
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0 + 1);

            // An unrelated account cannot delete the loan.
            env(del(evan_, loanKeylet.key), Ter(tecNO_PERMISSION));

            env(del(deleter, loanKeylet.key));
            env.close();

            // The loan is gone, the reserve is released, and the vault
            // bookkeeping is fully reversed.
            BEAST_EXPECT(!env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0);
            BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners0);

            auto const vault1 = readVault(env, broker);
            BEAST_EXPECT(vault1.available == vault0.available);
            BEAST_EXPECT(vault1.reserved == vault0.reserved);
            BEAST_EXPECT(vault1.total == vault0.total);

            // Broker bookkeeping is also fully reversed: DebtTotal and
            // OwnerCount return to their pre-proposal values, CoverAvailable
            // is untouched throughout.
            auto const broker1 = readBroker(env, broker);
            BEAST_EXPECT(broker1.debtTotal == broker0.debtTotal);
            BEAST_EXPECT(broker1.ownerCount == broker0.ownerCount);
            BEAST_EXPECT(broker1.coverAvailable == broker0.coverAvailable);
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

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // The loan is pending; the broker's OwnerCount is non-zero.
            if (auto const b = env.le(broker.brokerKeylet()); BEAST_EXPECT(b))
                BEAST_EXPECT(b->at(sfOwnerCount) != 0u);

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
            testcase("Two-step: LoanDelete of last pending loan forgives DebtTotal dust");

            // Deleting an active loan while a pending loan is outstanding does
            // not forgive residual DebtTotal. Deleting the pending loan
            // afterwards takes OwnerCount to zero and must forgive the
            // residual, leaving DebtTotal at zero and the broker deletable.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            // L1: proposed and accepted (borrower), then paid off in full.
            auto const l1Keylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender_, borrower_, startDate);
            env.close();
            env(accept(borrower_, l1Keylet.key));
            env.close();

            // L2: proposed for evan and left pending.
            auto const l2Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, evan_, startDate);
            env.close();

            // Pay L1 off. A generous upper bound (2x principal) clears
            // principal + interest.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 30s);
            env(pay(borrower_, l1Keylet.key, broker.asset(400), tfLoanFullPayment));
            env.close();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->at(sfPaymentRemaining) == 0);

            // Delete L1. OwnerCount stays at 1 and DebtTotal is exactly L2's
            // contribution.
            env(del(borrower_, l1Keylet.key));
            env.close();
            BEAST_EXPECT(!env.le(l1Keylet));
            auto const brokerL2Only = readBroker(env, broker);
            BEAST_EXPECT(brokerL2Only.ownerCount == 1);
            BEAST_EXPECT(brokerL2Only.debtTotal > beast::kZero);

            // Add a sub-drop residual to DebtTotal directly on the open ledger.
            // It must round to zero at the vault's scale and still be large
            // enough to survive addition to DebtTotal. Nothing closes until
            // the checks below are done, or the mutation is lost.
            Number const kResidual{1, -6};
            auto const mutated =
                env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
                    Sandbox sb(&view, TapNone);
                    auto b = sb.peek(broker.brokerKeylet());
                    if (!b)
                        return false;
                    b->at(sfDebtTotal) = brokerL2Only.debtTotal + kResidual;
                    sb.update(b);
                    sb.apply(view);
                    return true;
                });
            if (!BEAST_EXPECT(mutated))
                return;
            auto const mutatedDebtTotal = readBroker(env, broker).debtTotal;
            BEAST_EXPECT(mutatedDebtTotal == brokerL2Only.debtTotal + kResidual);
            // The residual must not have been lost to precision.
            BEAST_EXPECT(mutatedDebtTotal != brokerL2Only.debtTotal);

            // Deleting L2 takes OwnerCount to zero and must forgive the
            // residual.
            env(del(lender_, l2Keylet.key));
            BEAST_EXPECT(!env.le(l2Keylet));
            auto const brokerEmpty = readBroker(env, broker);
            BEAST_EXPECT(brokerEmpty.ownerCount == 0);
            BEAST_EXPECT(brokerEmpty.debtTotal == beast::kZero);

            // The broker can now be deleted.
            env(jtx::loan_broker::del(lender_, broker.brokerID));
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
            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);

            // Propose L1 (borrower) to establish a baseline delta.
            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const vault1 = readVault(env, broker);
            auto const broker1 = readBroker(env, broker);
            Number const l1DebtDelta = broker1.debtTotal - broker0.debtTotal;
            BEAST_EXPECT(vault1.reserved == vault0.reserved + principal);
            BEAST_EXPECT(broker1.ownerCount == broker0.ownerCount + 1);

            // Propose L2 (evan) on the same broker while L1 is still
            // pending. Each proposal contributes an equal delta.
            auto const l2Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, evan_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const vault2 = readVault(env, broker);
            auto const broker2 = readBroker(env, broker);
            BEAST_EXPECT(broker2.debtTotal - broker1.debtTotal == l1DebtDelta);
            BEAST_EXPECT(vault2.reserved == vault0.reserved + principal + principal);
            BEAST_EXPECT(broker2.ownerCount == broker0.ownerCount + 2);

            // Both loans exist and remain pending.
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->isFlag(lsfLoanPending));
            if (auto const l2 = env.le(l2Keylet); BEAST_EXPECT(l2))
                BEAST_EXPECT(l2->isFlag(lsfLoanPending));

            // Delete L1. L2's bookkeeping is untouched; broker state
            // reflects exactly the L2-only contribution.
            env(del(lender_, l1Keylet.key));
            env.close();
            BEAST_EXPECT(!env.le(l1Keylet));

            auto const vault3 = readVault(env, broker);
            auto const broker3 = readBroker(env, broker);
            BEAST_EXPECT(broker3.debtTotal == broker0.debtTotal + l1DebtDelta);
            BEAST_EXPECT(vault3.reserved == vault0.reserved + principal);
            BEAST_EXPECT(broker3.ownerCount == broker0.ownerCount + 1);
            if (auto const l2 = env.le(l2Keylet); BEAST_EXPECT(l2))
                BEAST_EXPECT(l2->isFlag(lsfLoanPending));
        }

        {
            testcase("Two-step: DebtMaximum constrains a second pending proposal");

            // A pending loan counts toward DebtMaximum. With DebtMaximum set
            // to L1's DebtTotal, a same-sized L2 is rejected with
            // tecLIMIT_EXCEEDED.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const brokerL1 = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerL1))
                return;
            Number const debtAfterL1 = brokerL1->at(sfDebtTotal);

            // Tighten DebtMaximum to exactly L1's DebtTotal.
            env(jtx::loan_broker::set(lender_, broker.vaultID),
                jtx::loan_broker::kLoanBrokerId(broker.brokerID),
                jtx::loan_broker::kDebtMaximum(debtAfterL1));
            env.close();

            // Second proposal exceeds the debt cap.
            propose(
                env,
                broker,
                lender_,
                evan_,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(tecLIMIT_EXCEEDED));
            env.close();

            // L1 remains pending; L2 was not created.
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->isFlag(lsfLoanPending));
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

            auto const brokerState0 = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerState0))
                return;
            Number const debtTotal0 = brokerState0->at(sfDebtTotal);
            std::uint32_t const brokerOwnerCount0 = brokerState0->at(sfOwnerCount);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            auto const lenderSeq = env.seq(lender_);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            env(batch::outer(lender_, lenderSeq, batchFee, tfAllOrNothing),
                batch::Inner(
                    env.json(
                        set(lender_, broker.brokerID, principal),
                        kBorrower(borrower_.id()),
                        kStartDate(startDate),
                        kInterestRate(interest_),
                        kPaymentTotal(payTotal_),
                        kPaymentInterval(payInterval_),
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
                if (auto const b = env.le(broker.brokerKeylet()); BEAST_EXPECT(b))
                {
                    BEAST_EXPECT(b->at(sfDebtTotal) > debtTotal0);
                    BEAST_EXPECT(b->at(sfOwnerCount) == brokerOwnerCount0 + 1);
                }
            }
            else
            {
                // No loan was created and broker bookkeeping is unchanged.
                BEAST_EXPECT(!env.le(loanKeylet));
                if (auto const b = env.le(broker.brokerKeylet()); BEAST_EXPECT(b))
                {
                    BEAST_EXPECT(b->at(sfDebtTotal) == debtTotal0);
                    BEAST_EXPECT(b->at(sfOwnerCount) == brokerOwnerCount0);
                }
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

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

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

            auto const vault0 = readVault(env, broker);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender_, borrower_, startDate);
            env.close();

            env(accept(borrower_, loanKeylet.key));
            env.close();

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
            auto const vault1 = readVault(env, broker);
            auto const broker1 = readBroker(env, broker);
            BEAST_EXPECT(vault1.reserved == beast::kZero);
            BEAST_EXPECT(vault1.available == vault1.total);
            BEAST_EXPECT(broker1.debtTotal == beast::kZero);
            // The vault ends with at least what it started with.
            BEAST_EXPECT(vault1.total >= vault0.total);
            BEAST_EXPECT(vault1.available >= vault0.available);
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
            propose(env, broker, lender_, borrower_, startDate);
            env.close();
            expectStillPending(env, loanKeylet);

            // Force the vault into the target phase on the open ledger. No
            // close between here and the LoanAccept, or the mutation is lost.
            std::uint32_t const parentClose =
                env.current()->parentCloseTime().time_since_epoch().count();
            auto const changed =
                env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) -> bool {
                    Sandbox sb(&view, TapNone);
                    auto v = sb.peek(broker.vaultKeylet());
                    if (!v)
                        return false;
                    if (scenario == VaultPhase::Subscription)
                    {
                        // parentClose < SubscriptionDate puts the vault in
                        // Subscription.
                        v->setFieldU32(sfSubscriptionDate, parentClose + 600);
                    }
                    else
                    {
                        // RedemptionDate < parentClose puts the vault in
                        // Redemption.
                        v->setFieldU32(sfRedemptionDate, parentClose - 1);
                    }
                    sb.update(v);
                    sb.apply(view);
                    return true;
                });
            if (!BEAST_EXPECT(changed))
                continue;

            // The open ledger reports the intended phase and StartDate is
            // still in the future.
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(getVaultPhase(*env.current(), v) == scenario);
            BEAST_EXPECT(parentClose < startDate);

            env(accept(borrower_, loanKeylet.key), Ter(expected));
            expectStillPending(env, loanKeylet);
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
            auto const brokerBefore = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerBefore))
                return;
            Number const cover0 = brokerBefore->at(sfCoverAvailable);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender_, borrower_, startDate);
            env.close();

            // The pending loan has raised DebtTotal above zero.
            auto const brokerAfter = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerAfter))
                return;
            Number const debtWithPending = brokerAfter->at(sfDebtTotal);
            Number const expectedMinCover = debtWithPending * coverMinRate;
            BEAST_EXPECT(debtWithPending > beast::kZero);

            // Attempt to claw back the entire cover deposit. The withdrawal is
            // capped at the headroom.
            env(jtx::loan_broker::coverClawback(issuer_),
                jtx::loan_broker::kLoanBrokerId(broker.brokerID),
                kAmount(broker.asset(defaults.coverDeposit)));
            env.close();

            auto const brokerClawed = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerClawed))
                return;
            Number const coverAfter = brokerClawed->at(sfCoverAvailable);
            // Cover went down, but not below the minimum.
            BEAST_EXPECT(coverAfter < cover0);
            BEAST_EXPECT(coverAfter >= expectedMinCover);

            // LoanAccept succeeds with cover at the minimum.
            env(accept(borrower_, loanKeylet.key));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
        }

        {
            testcase("Two-step: pending loan blocks VaultDelete");

            // While a loan is pending, VaultDelete is rejected with
            // tecHAS_OBLIGATIONS. Deleting the pending loan restores the
            // vault's pre-proposal accounting.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const vault0 = readVault(env, broker);
            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // AssetsReserved is non-zero and VaultDelete is rejected.
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(v->at(sfAssetsReserved) > beast::kZero);
            Vault const vault{env};
            env(vault.del({.owner = lender_, .id = broker.vaultID}), Ter(tecHAS_OBLIGATIONS));
            env.close();

            // Deleting the pending loan returns the vault to its pre-proposal
            // snapshot.
            env(del(lender_, loanKeylet.key));
            env.close();
            auto const vault1 = readVault(env, broker);
            BEAST_EXPECT(vault1.available == vault0.available);
            BEAST_EXPECT(vault1.reserved == beast::kZero);
            BEAST_EXPECT(vault1.total == vault0.total);
        }

        {
            testcase("Two-step: precision loss on fractional origination fee (XRP)");

            // An origination fee that cannot be represented in the Vault.Asset
            // is rejected with tecPRECISION_LOSS.
            Env env(*this, features_);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            // 1.5 drops is not representable as XRP.
            propose(
                env,
                broker,
                lender_,
                borrower_,
                (env.now() + 1h).time_since_epoch().count(),
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

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            // No env.close() here.
            env(accept(borrower_, loanKeylet.key));
            env.close();

            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->isFieldPresent(sfOwnerNode));
            }
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
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(getVaultVersion(v) == VaultVersion::Legacy);

            Number const principal = broker.asset(200).number();
            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);
            auto const lenderOwners0 = env.ownerCount(lender_);
            auto const borrowerOwners0 = env.ownerCount(borrower_);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());

            // Under Legacy accounting the broker debt carries the interest
            // from proposal, but the vault does not recognise it until the
            // loan is accepted.
            auto const vault1 = readVault(env, broker);
            auto const broker1 = readBroker(env, broker);
            BEAST_EXPECT(vault1.available == vault0.available - principal);
            BEAST_EXPECT(vault1.reserved == vault0.reserved + principal);
            BEAST_EXPECT(vault1.total == vault0.total);
            Number const interestDue = broker1.debtTotal - broker0.debtTotal - principal;
            BEAST_EXPECT(interestDue > beast::kZero);
            BEAST_EXPECT(broker1.ownerCount == broker0.ownerCount + 1);
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0 + 1);

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
            expectStillPending(env, loanKeylet);

            // The rejected acceptance changed nothing: the interest is still
            // only in DebtTotal, not AssetsTotal, and the owner reserve is
            // still on the broker owner.
            auto const vault2 = readVault(env, broker);
            BEAST_EXPECT(vault2.available == vault1.available);
            BEAST_EXPECT(vault2.reserved == vault1.reserved);
            BEAST_EXPECT(vault2.total == vault1.total);
            auto const broker2 = readBroker(env, broker);
            BEAST_EXPECT(broker2.debtTotal == broker1.debtTotal);
            BEAST_EXPECT(broker2.ownerCount == broker1.ownerCount);
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0 + 1);
            BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners0);

            // Deleting the pending loan reverses the proposal exactly. The
            // proposal never touched AssetsTotal, so neither does the delete.
            env(del(lender_, loanKeylet.key));
            BEAST_EXPECT(!env.le(loanKeylet));

            auto const vault3 = readVault(env, broker);
            BEAST_EXPECT(vault3.available == vault0.available);
            BEAST_EXPECT(vault3.reserved == vault0.reserved);
            BEAST_EXPECT(vault3.total == vault0.total);
            auto const broker3 = readBroker(env, broker);
            BEAST_EXPECT(broker3.debtTotal == broker0.debtTotal);
            BEAST_EXPECT(broker3.ownerCount == broker0.ownerCount);
            BEAST_EXPECT(broker3.coverAvailable == broker0.coverAvailable);
            BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners0);
            BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners0);
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
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(getVaultVersion(v) == VaultVersion::Legacy);

            Number const principal = broker.asset(200).number();
            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);

            // Propose, and read the interest the proposal carries off the
            // broker debt. The vault total is untouched by the proposal.
            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            expectStillPending(env, loanKeylet);
            auto const broker1 = readBroker(env, broker);
            Number const interestDue = broker1.debtTotal - broker0.debtTotal - principal;
            BEAST_EXPECT(interestDue > beast::kZero);
            BEAST_EXPECT(readVault(env, broker).total == vault0.total);

            // Leave room for one and a half loans' worth of interest: the
            // pending loan and one more immediately active loan each fit on
            // their own, but not both. The cap must sit on the asset grid.
            {
                Number assetsMaximum = vault0.total + interestDue + interestDue / 2;
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
                    kInterestRate(interest_),
                    kPaymentTotal(payTotal_),
                    kPaymentInterval(payInterval_),
                    Sig(sfCounterpartySignature, lender_),
                    Fee(env.current()->fees().base * 2));
                if (auto const second = env.le(secondKeylet); BEAST_EXPECT(second))
                    BEAST_EXPECT(!second->isFlag(lsfLoanPending));
                BEAST_EXPECT(readVault(env, broker).total == vault0.total + interestDue);
            }

            auto const vault1 = readVault(env, broker);
            auto const broker2 = readBroker(env, broker);
            auto const lenderOwners1 = env.ownerCount(lender_);
            auto const borrowerOwners1 = env.ownerCount(borrower_);

            if (fillHeadroom)
            {
                // Accepting now would push AssetsTotal past AssetsMaximum.
                // Nothing changes.
                env(accept(borrower_, loanKeylet.key), Ter(tecLIMIT_EXCEEDED));
                expectStillPending(env, loanKeylet);

                auto const vault2 = readVault(env, broker);
                BEAST_EXPECT(vault2.available == vault1.available);
                BEAST_EXPECT(vault2.reserved == vault1.reserved);
                BEAST_EXPECT(vault2.total == vault1.total);
                auto const broker3 = readBroker(env, broker);
                BEAST_EXPECT(broker3.debtTotal == broker2.debtTotal);
                BEAST_EXPECT(broker3.ownerCount == broker2.ownerCount);
                BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners1);
                BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners1);

                // Deleting the proposal leaves AssetsTotal where the active
                // loan put it: there is no interest to unwind.
                env(del(lender_, loanKeylet.key));
                BEAST_EXPECT(!env.le(loanKeylet));
                auto const vault3 = readVault(env, broker);
                BEAST_EXPECT(vault3.available == vault1.available + principal);
                BEAST_EXPECT(vault3.reserved == vault1.reserved - principal);
                BEAST_EXPECT(vault3.total == vault1.total);
            }
            else
            {
                // With the headroom intact the acceptance books the interest
                // into AssetsTotal and stays under AssetsMaximum.
                env(accept(borrower_, loanKeylet.key));
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                    BEAST_EXPECT(!loan->isFlag(lsfLoanPending));

                auto const vault2 = readVault(env, broker);
                BEAST_EXPECT(vault2.available == vault1.available);
                BEAST_EXPECT(vault2.reserved == vault1.reserved - principal);
                BEAST_EXPECT(vault2.total == vault1.total + interestDue);
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                    BEAST_EXPECT(vault2.total <= v->at(sfAssetsMaximum));
                auto const broker3 = readBroker(env, broker);
                BEAST_EXPECT(broker3.debtTotal == broker2.debtTotal);
                BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners1 - 1);
                BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners1 + 1);
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
                interest_,
                payInterval_,
                payTotal_,
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

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender_, borrower_, (env.now() + 1h).time_since_epoch().count());
            env.close();
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
                    kInterestRate(interest_),
                    kPaymentTotal(payTotal_),
                    kPaymentInterval(payInterval_),
                    Sig(sfCounterpartySignature, lender_),
                    Fee(env.current()->fees().base * 2));
                env.close();
                if (auto const second = env.le(secondKeylet); BEAST_EXPECT(second))
                    BEAST_EXPECT(!second->isFlag(lsfLoanPending));
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                {
                    BEAST_EXPECT(v->at(sfYieldUnrealized) == interestDue);
                    BEAST_EXPECT(vaultOpenZoneCapacity(v, kNumZero) == openLimit);
                }
            }

            auto const vault1 = readVault(env, broker);
            auto const broker1 = readBroker(env, broker);
            auto const lenderOwners1 = env.ownerCount(lender_);
            auto const borrowerOwners1 = env.ownerCount(borrower_);

            if (fillHeadroom)
            {
                // Accepting now would push the Open-zone capacity past the
                // ceiling. Nothing changes.
                env(accept(borrower_, loanKeylet.key), Ter(tecLIMIT_EXCEEDED));
                env.close();
                expectStillPending(env, loanKeylet);

                auto const vault2 = readVault(env, broker);
                BEAST_EXPECT(vault2.available == vault1.available);
                BEAST_EXPECT(vault2.reserved == vault1.reserved);
                BEAST_EXPECT(vault2.total == vault1.total);
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                    BEAST_EXPECT(v->at(sfYieldUnrealized) == interestDue);
                auto const broker2 = readBroker(env, broker);
                BEAST_EXPECT(broker2.debtTotal == broker1.debtTotal);
                BEAST_EXPECT(broker2.ownerCount == broker1.ownerCount);
                BEAST_EXPECT(env.ownerCount(lender_) == lenderOwners1);
                BEAST_EXPECT(env.ownerCount(borrower_) == borrowerOwners1);
            }
            else
            {
                // With the headroom intact the capacity lands exactly on the
                // ceiling, which the guard allows.
                env(accept(borrower_, loanKeylet.key));
                env.close();
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                    BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                {
                    BEAST_EXPECT(v->at(sfYieldUnrealized) == interestDue);
                    BEAST_EXPECT(vaultOpenZoneCapacity(v, kNumZero) == openLimit);
                }
                auto const vault2 = readVault(env, broker);
                BEAST_EXPECT(vault2.reserved == vault1.reserved - principal);
            }
        }
    }

    // Top-level dispatcher: gates on featureLendingProtocolV1_2 and delegates
    // to the amendment-disabled path or the individual enabled-feature groups.
    void
    testTwoStep(FeatureBitset const& featuresToTest)
    {
        features_ = featuresToTest;

        if ((features_ & featureLendingProtocolV1_2).none())
        {
            testTwoStepAmendmentDisabled();
            return;
        }

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
        // all_ excludes featureLendingProtocolV1_2, so the enabled run must opt
        // back in explicitly; without it both runs take the disabled path.
        testTwoStep(all_ | featureLendingProtocolV1_1);
        testTwoStep(all_ | featureLendingProtocolV1_1 | featureLendingProtocolV1_2);
    }
};

BEAST_DEFINE_TESTSUITE(LoanTwoStep, tx, xrpl);

}  // namespace xrpl::test
