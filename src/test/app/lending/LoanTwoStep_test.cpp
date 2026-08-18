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
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace xrpl::test {

class LoanTwoStep_test : public LoanTestBase
{
private:
    // Exercises the two-step (LendingProtocolV1_1) flow, where the LoanBroker
    // owner proposes a pending Loan (LoanSet with a Borrower and StartDate) that
    // the Borrower later accepts (LoanAccept) or that either party cancels
    // (LoanDelete). Requires the LendingProtocolV1_1 amendment.
    void
    testTwoStep(FeatureBitset features)
    {
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;

        Account const issuer{"issuer"};  // Issues the IOU / MPT assets
        Account const lender{"lender"};  // Vault + LoanBroker owner
        Account const borrower{"borrower"};
        Account const evan{"evan"};  // unrelated third party

        // Loan terms shared across the scenarios. The principal is derived
        // from the broker's asset, so it adapts to XRP, IOU and MPT.
        auto const interest = TenthBips32{50'000};
        std::uint32_t const payTotal = 10;
        std::uint32_t const payInterval = 200;

        auto const assetTypeName = [](AssetType t) -> char const* {
            switch (t)
            {
                case AssetType::XRP:
                    return "XRP";
                case AssetType::IOU:
                    return "IOU";
                case AssetType::MPT:
                    return "MPT";
            }
            return "?";
        };

        // Build a funded environment with a Vault + LoanBroker owned by
        // `lender`, using the requested asset type, and return the broker.
        auto const makeBroker = [&](Env& env, AssetType assetType) -> BrokerInfo {
            env.fund(XRP(100'000'000), noripple(lender));
            env.fund(XRP(1'000'000), borrower, evan);
            if (assetType != AssetType::XRP)
                env.fund(XRP(1'000'000), issuer);
            env.close();
            BrokerParameters const params{};
            auto const asset = createAsset(env, assetType, params, issuer, lender, borrower);
            env.close();
            if (!asset.native())
                env(pay(issuer, lender, asset(params.vaultDeposit + params.coverDeposit)));
            env.close();
            return createVaultAndBroker(env, asset, lender, params);
        };

        // The keylet of the next loan the broker will create.
        auto const nextLoanKeylet = [&](Env& env, BrokerInfo const& broker) -> Keylet {
            auto const brokerSle = env.le(broker.brokerKeylet());
            return keylet::loan(
                broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        };

        // Snapshot of the vault's asset accounting.
        struct VaultAmounts
        {
            Number available;
            Number reserved;
            Number total;
        };
        auto const readVault = [&](Env& env, BrokerInfo const& broker) -> VaultAmounts {
            auto const v = env.le(broker.vaultKeylet());
            return {
                .available = v->at(sfAssetsAvailable),
                .reserved = v->at(sfAssetsReserved),
                .total = v->at(sfAssetsTotal)};
        };

        // Snapshot of the LoanBroker's own bookkeeping.
        struct BrokerAmounts
        {
            Number debtTotal;
            Number coverAvailable;
            std::uint32_t ownerCount{};
        };
        auto const readBroker = [&](Env& env, BrokerInfo const& broker) -> BrokerAmounts {
            auto const b = env.le(broker.brokerKeylet());
            return {
                .debtTotal = b->at(sfDebtTotal),
                .coverAvailable = b->at(sfCoverAvailable),
                .ownerCount = b->at(sfOwnerCount)};
        };

        // Submit a valid two-step proposal from `proposer` on behalf of
        // `theBorrower`, with the supplied StartDate and any extra functors.
        auto const propose = [&](Env& env,
                                 BrokerInfo const& broker,
                                 Account const& proposer,
                                 Account const& theBorrower,
                                 std::uint32_t startDate,
                                 auto const&... extra) {
            env(set(proposer, broker.brokerID, broker.asset(200).number()),
                kBorrower(theBorrower),
                kStartDate(startDate),
                kInterestRate(interest),
                kPaymentTotal(payTotal),
                kPaymentInterval(payInterval),
                extra...);
        };

        // Per spec 4.3, a failed LoanAccept must leave the pending Loan
        // intact so the borrower can rectify the issue and retry until the
        // StartDate expires.
        auto const expectStillPending = [this](Env& env, Keylet const& k) {
            if (auto const loan = env.le(k); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
        };

        auto const featureEnabled = (features & featureLendingProtocolV1_1).any();

        if (!featureEnabled)
        {
            testcase("Two-step: rejected as before");

            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);
            // A StartDate comfortably in the future. With the amendment
            // disabled, the Borrower/StartDate fields are gated off in
            // checkExtraFeatures, so the tx is rejected with temDISABLED.
            propose(
                env,
                broker,
                lender,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(temDISABLED));

            // XLS-66 spec 3.8.5.2.1: CounterpartySignature is not present
            // (temBAD_SIGNER). With V1.1 disabled, the immediate flow still
            // requires a CounterpartySignature; no Batch inner, no Borrower.
            env(set(lender, broker.brokerID, broker.asset(200).number()), Ter(temBAD_SIGNER));

            // XLS-66 amendment gate: LoanAccept is introduced by
            // featureLendingProtocolV1_1, so with the amendment disabled the
            // transaction type itself is rejected (temDISABLED).
            env(accept(borrower, keylet::loan(broker.brokerID, SeqProxy::rawSequence(1)).key),
                Ter(temDISABLED));

            // Rest of the tests are not applicable
            return;
        }

        for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: propose then accept (" << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);
            Number const principal = broker.asset(200).number();

            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);
            auto const lenderOwners0 = env.ownerCount(lender);
            auto const borrowerOwners0 = env.ownerCount(borrower);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            // A StartDate comfortably in the future.
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // The proposal creates a pending Loan, linked only into the broker
            // pseudo-account's directory.
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->at(sfBorrower) == borrower.id());
                BEAST_EXPECT(loan->isFieldPresent(sfLoanBrokerNode));
                BEAST_EXPECT(!loan->isFieldPresent(sfOwnerNode));
            }

            // The owner reserve is charged to the broker owner, not the
            // borrower.
            BEAST_EXPECT(env.ownerCount(lender) == lenderOwners0 + 1);
            BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwners0);

            // Vault bookkeeping: Available -= P, Reserved += P, Total +=
            // InterestDue.
            auto const vault1 = readVault(env, broker);
            BEAST_EXPECT(vault1.available == vault0.available - principal);
            BEAST_EXPECT(vault1.reserved == vault0.reserved + principal);
            BEAST_EXPECT(vault1.total > vault0.total);
            Number const interestDue = vault1.total - vault0.total;

            // Broker bookkeeping: DebtTotal += P + InterestDue, OwnerCount +=
            // 1, CoverAvailable is untouched by the proposal.
            auto const broker1 = readBroker(env, broker);
            BEAST_EXPECT(broker1.debtTotal == broker0.debtTotal + principal + interestDue);
            BEAST_EXPECT(broker1.ownerCount == broker0.ownerCount + 1);
            BEAST_EXPECT(broker1.coverAvailable == broker0.coverAvailable);

            // Capture pre-acceptance balances to verify disbursement.
            auto const vaultPseudo = [&]() {
                auto const v = env.le(broker.vaultKeylet());
                return Account("vault pseudo-account", v->at(sfAccount));
            }();
            STAmount const pseudoBal0 = env.balance(vaultPseudo, broker.asset).value();
            STAmount const borrowerBal0 = env.balance(borrower, broker.asset).value();

            env(accept(borrower, loanKeylet.key));
            env.close();

            // The loan is now active and linked into the borrower's directory.
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->isFieldPresent(sfLoanBrokerNode));
                BEAST_EXPECT(loan->isFieldPresent(sfOwnerNode));
            }

            // The reserve is swapped from the broker owner to the borrower.
            BEAST_EXPECT(env.ownerCount(lender) == lenderOwners0);
            BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwners0 + 1);

            // Reserved principal is released; Available and Total are unchanged
            // from the proposal.
            auto const vault2 = readVault(env, broker);
            BEAST_EXPECT(vault2.reserved == vault0.reserved);
            BEAST_EXPECT(vault2.available == vault0.available - principal);
            BEAST_EXPECT(vault2.total == vault0.total + interestDue);

            // Broker bookkeeping: acceptance leaves DebtTotal, OwnerCount, and
            // CoverAvailable unchanged from the pending snapshot.
            auto const broker2 = readBroker(env, broker);
            BEAST_EXPECT(broker2.debtTotal == broker1.debtTotal);
            BEAST_EXPECT(broker2.ownerCount == broker1.ownerCount);
            BEAST_EXPECT(broker2.coverAvailable == broker1.coverAvailable);

            // The principal is disbursed from the vault pseudo-account to the
            // borrower (origination fee is zero, so the borrower receives it
            // all, less the transaction fee it paid).
            BEAST_EXPECT(
                env.balance(vaultPseudo, broker.asset).value() ==
                pseudoBal0 - broker.asset(200).value());
            BEAST_EXPECT(env.balance(borrower, broker.asset).value() > borrowerBal0);
        }

        // Exercise a proposal with a non-zero origination fee, then verify at
        // acceptance that the principal leaves the vault pseudo-account, the
        // borrower receives the net, and the broker owner receives the fee.
        // XRP is excluded because the borrower's LoanAccept fee would perturb
        // the exact borrower balance assertion.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: propose then accept with origination fee ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);
            Number const principal = broker.asset(200).number();
            Number const originationFee = broker.asset(5).number();

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(
                env,
                broker,
                lender,
                borrower,
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
            STAmount const borrowerBal0 = env.balance(borrower, broker.asset).value();
            STAmount const lenderBal0 = env.balance(lender, broker.asset).value();

            env(accept(borrower, loanKeylet.key));
            env.close();

            STAmount const netToBorrower{broker.asset, principal - originationFee};
            STAmount const feeToOwner{broker.asset, originationFee};

            // The full principal leaves the vault pseudo-account.
            BEAST_EXPECT(
                env.balance(vaultPseudo, broker.asset).value() ==
                pseudoBal0 - broker.asset(200).value());
            // The borrower receives the principal net of the origination fee.
            BEAST_EXPECT(
                env.balance(borrower, broker.asset).value() == borrowerBal0 + netToBorrower);
            // The broker owner receives the origination fee.
            BEAST_EXPECT(env.balance(lender, broker.asset).value() == lenderBal0 + feeToOwner);
        }

        {
            testcase("Two-step: accepted loan behaves as a normal loan");

            // Once accepted, a two-step loan is indistinguishable from a
            // one-step loan for the rest of its lifecycle: it can be
            // impaired, unimpaired, paid, and finally deleted.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender, borrower, startDate);
            env.close();

            env(accept(borrower, loanKeylet.key));
            env.close();

            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(loan->at(sfPaymentRemaining) == payTotal);
            }

            // LoanManage: impair then unimpair.
            env(manage(lender, loanKeylet.key, tfLoanImpair));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanImpaired));

            env(manage(lender, loanKeylet.key, tfLoanUnimpair));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(!loan->isFlag(lsfLoanImpaired));

            // LoanPay: a regular periodic payment succeeds, then the borrower
            // clears the remainder with tfLoanFullPayment.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 1h);
            env(pay(borrower, loanKeylet.key, broker.asset(30)));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->at(sfPaymentRemaining) < payTotal);

            // A generous upper bound (2x principal) clears principal + interest.
            env(pay(borrower, loanKeylet.key, broker.asset(400), tfLoanFullPayment));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->at(sfPaymentRemaining) == 0);

            // LoanDelete succeeds once the loan is fully paid.
            env(del(borrower, loanKeylet.key));
            env.close();
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Two-step: LoanPay on accepted loan while another loan is pending");

            // Regression: LoanPay::doApply's vault-balance invariant used to
            // assert AssetsAvailable == pseudo_balance, ignoring
            // AssetsReserved. Whenever a pending loan bumped AssetsReserved,
            // any LoanPay on an accepted loan would fire the debug assertion.
            // The correct invariant is
            //   pseudo_balance == AssetsAvailable + AssetsReserved,
            // and this test locks that in.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            // L1: accepted (borrower) — disburses principal, drains
            // AssetsReserved back to 0.
            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();
            env(accept(borrower, l1Keylet.key));
            env.close();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(!l1->isFlag(lsfLoanPending));

            // L2: still pending (evan) — leaves AssetsReserved > 0.
            propose(env, broker, lender, evan, (env.now() + 1h).time_since_epoch().count());
            env.close();
            if (auto const v = env.le(broker.vaultKeylet()); BEAST_EXPECT(v))
                BEAST_EXPECT(v->at(sfAssetsReserved) > beast::kZero);

            // A payment on L1 must succeed with L2 still pending. Before the
            // fix, LoanPay's debug invariant tripped here.
            env(pay(borrower, l1Keylet.key, broker.asset(30)));
            env.close();
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->at(sfPaymentRemaining) < payTotal);
        }

        {
            testcase("Two-step: proposal failures");

            Env env(*this, features);
            auto const epoch = env.now();
            auto const broker = makeBroker(env, AssetType::XRP);

            // XLS-66 spec 3.8.5.3.1: Account != LoanBroker.Owner (tecNO_PERMISSION).
            // A StartDate comfortably in the future.
            propose(
                env,
                broker,
                evan,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(tecNO_PERMISSION));

            // XLS-66 flow: two-step preclaim rejects a past StartDate (tecEXPIRED).
            std::uint32_t const pastDate = epoch.time_since_epoch().count();
            propose(env, broker, lender, borrower, pastDate, Ter(tecEXPIRED));

            // XLS-66 flow: no CounterpartySignature, no Borrower, not a Batch
            // inner: matches neither one-step nor two-step (temINVALID with
            // V1.1 enabled; temBAD_SIGNER without, exercised earlier).
            env(set(lender, broker.brokerID, broker.asset(200).number()), Ter(temINVALID));

            // XLS-66 flow: Borrower without StartDate is not a valid two-step
            // proposal (temINVALID).
            env(set(lender, broker.brokerID, broker.asset(200).number()),
                kBorrower(borrower),
                Ter(temINVALID));

            // XLS-66 flow: StartDate without Borrower is not a valid two-step
            // proposal (temINVALID).
            env(set(lender, broker.brokerID, broker.asset(200).number()),
                kStartDate((env.now() + 1h).time_since_epoch().count()),
                Ter(temINVALID));

            // XLS-66 flow: Borrower + Counterparty is ambiguous (temINVALID).
            env(set(lender, broker.brokerID, broker.asset(200).number()),
                kBorrower(borrower),
                kStartDate((env.now() + 1h).time_since_epoch().count()),
                kCounterparty(borrower),
                Ter(temINVALID));

            // XLS-66 flow: Borrower + CounterpartySignature is ambiguous
            // (temINVALID).
            env(set(lender, broker.brokerID, broker.asset(200).number()),
                kBorrower(borrower),
                kStartDate((env.now() + 1h).time_since_epoch().count()),
                Sig(sfCounterpartySignature, borrower),
                Ter(temINVALID));
        }

        {
            testcase("Two-step: LoanAccept validation");

            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            // XLS-66 spec 3.9.3.1.1: LoanID is zero (temINVALID).
            env(accept(borrower, uint256{}), Ter(temINVALID));

            // XLS-66 spec 3.9.3.2.1: Loan with the specified LoanID does not
            // exist (tecNO_ENTRY).
            env(accept(borrower, keylet::loan(broker.brokerID, SeqProxy::rawSequence(999)).key),
                Ter(tecNO_ENTRY));

            auto const loanKeylet = nextLoanKeylet(env, broker);
            // A StartDate comfortably in the future.
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // XLS-66 spec 3.9.3.2.3: Account submitting the tx is not the
            // Loan.Borrower (tecNO_PERMISSION).
            env(accept(evan, loanKeylet.key), Ter(tecNO_PERMISSION));
            env(accept(lender, loanKeylet.key), Ter(tecNO_PERMISSION));
            expectStillPending(env, loanKeylet);

            // The borrower accepts successfully.
            env(accept(borrower, loanKeylet.key));
            env.close();

            // XLS-66 spec 3.9.3.2.2: Loan does not have lsfLoanPending set
            // (tecNO_PERMISSION). Here, the loan was already accepted and is
            // no longer pending.
            env(accept(borrower, loanKeylet.key), Ter(tecNO_PERMISSION));
        }

        {
            testcase("Two-step: pending loan rejects other transactions");

            // While a loan is pending acceptance it may only be accepted
            // (LoanAccept) or cancelled (LoanDelete, covered separately). Every
            // other loan transaction must reject it.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // The loan is pending.
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));

            // LoanManage can not impair, unimpair, or default a pending loan.
            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecNO_PERMISSION));

            // LoanPay can not pay a pending loan, even from the borrower.
            env(pay(borrower, loanKeylet.key, broker.asset(50)), Ter(tecNO_PERMISSION));
            env(pay(borrower, loanKeylet.key, broker.asset(50), tfLoanFullPayment),
                Ter(tecNO_PERMISSION));

            // The borrower can still accept the pending loan.
            env(accept(borrower, loanKeylet.key));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(!loan->isFlag(lsfLoanPending));
        }

        // LoanManage::preclaim rejects pending loans before it inspects the
        // payment schedule. Guard that ordering by advancing the ledger past
        // NextPaymentDueDate + GracePeriod on a still-pending loan: the tx
        // must still return tecNO_PERMISSION, never tecTOO_SOON or success.
        for (auto const assetType : {AssetType::XRP, AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: pending loan rejects LoanManage after due date ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender, borrower, startDate);
            env.close();

            // Advance past StartDate + PaymentInterval + GracePeriod. payInterval
            // is 200s and the default GracePeriod is 60s, so +2h from StartDate
            // is comfortably past both.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 2h);

            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                BEAST_EXPECT(
                    env.now() >
                    NetClock::time_point{NetClock::duration{
                        loan->at(sfNextPaymentDueDate) + loan->at(sfGracePeriod)}});
            }

            env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tecNO_PERMISSION));
        }

        {
            testcase("Two-step: LoanAccept after expiry");

            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender, borrower, startDate);
            env.close();

            // Advance the ledger beyond the StartDate.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 1h);

            env(accept(borrower, loanKeylet.key), Ter(tecEXPIRED));
            expectStillPending(env, loanKeylet);
        }

        {
            testcase("Two-step: LoanSet StartDate expiry boundary");

            // XLS-66 flow: hasExpired uses Inclusive comparison
            // (parentCloseTime() >= StartDate counts as expired), so the
            // exact-equal case is on the expired side of the boundary.
            // Lock that in for the two-step LoanSet preclaim check.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            Number const principal = broker.asset(200).number();
            auto const parentClose =
                env.current()->parentCloseTime().time_since_epoch().count();

            // StartDate == parentCloseTime is inclusive-expired.
            env(set(lender, broker.brokerID, principal),
                kBorrower(borrower),
                kStartDate(parentClose),
                kInterestRate(interest),
                kPaymentTotal(payTotal),
                kPaymentInterval(payInterval),
                Ter(tecEXPIRED));

            // StartDate == parentCloseTime + 1 is just above the boundary
            // and must succeed.
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(set(lender, broker.brokerID, principal),
                kBorrower(borrower),
                kStartDate(parentClose + 1),
                kInterestRate(interest),
                kPaymentTotal(payTotal),
                kPaymentInterval(payInterval));
            env.close();
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                BEAST_EXPECT(loan->isFlag(lsfLoanPending));
        }

        {
            testcase("Two-step: StartDate near kMaxTime triggers overflow guard");

            // XLS-66 flow: the two-step flow is the first place where
            // LoanSet::preclaim sees a fully caller-controlled StartDate
            // (getStartDate returns tx[sfStartDate] for two-step, not the
            // ledger's own close time). Push StartDate near kMaxTime and
            // verify the schedule-overflow guard still triggers tecKILLED
            // through this newly-external input path. Mirrors the one-step
            // overflow suite in LoanPay_test.cpp:540-618.
            using timeType = decltype(sfNextPaymentDueDate)::type::value_type;
            static_assert(std::is_same_v<timeType, std::uint32_t>);
            constexpr timeType kMaxTime = std::numeric_limits<timeType>::max();
            static_assert(kMaxTime == 4'294'967'295);

            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);
            Number const principal = broker.asset(200).number();

            // PaymentInterval alone exceeds kMaxTime - StartDate.
            env(set(lender, broker.brokerID, principal),
                kBorrower(borrower),
                kStartDate(kMaxTime - (payInterval - 1)),
                kInterestRate(interest),
                kPaymentTotal(payTotal),
                kPaymentInterval(payInterval),
                Ter(tecKILLED));

            // Interval fits but interval * total exceeds the remaining
            // time available for the schedule.
            env(set(lender, broker.brokerID, principal),
                kBorrower(borrower),
                kStartDate(kMaxTime - (payInterval * payTotal / 2)),
                kInterestRate(interest),
                kPaymentTotal(payTotal),
                kPaymentInterval(payInterval),
                Ter(tecKILLED));
        }

        {
            testcase("Two-step: LoanDelete of pending loan after StartDate expired");

            // A pending loan whose StartDate has passed can no longer be
            // accepted (LoanAccept returns tecEXPIRED), but it can still be
            // cleaned up with LoanDelete, releasing the reserve and reversing
            // the vault bookkeeping.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const vault0 = readVault(env, broker);
            auto const lenderOwners0 = env.ownerCount(lender);
            auto const borrowerOwners0 = env.ownerCount(borrower);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
            propose(env, broker, lender, borrower, startDate);
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender) == lenderOwners0 + 1);

            // Advance the ledger beyond the StartDate.
            env.close(NetClock::time_point{NetClock::duration{startDate}} + 1h);

            // The proposal has expired, so it can no longer be accepted.
            env(accept(borrower, loanKeylet.key), Ter(tecEXPIRED));
            expectStillPending(env, loanKeylet);

            // But it can still be deleted.
            env(del(lender, loanKeylet.key));
            env.close();

            // The loan is gone, the reserve is released, and the vault
            // bookkeeping is fully reversed.
            BEAST_EXPECT(!env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender) == lenderOwners0);
            BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwners0);

            auto const vault1 = readVault(env, broker);
            BEAST_EXPECT(vault1.available == vault0.available);
            BEAST_EXPECT(vault1.reserved == vault0.reserved);
            BEAST_EXPECT(vault1.total == vault0.total);
        }

        {
            testcase("Two-step: LoanSet with insufficient reserve");

            // XLS-66 spec 3.8.5.3.2: LoanBroker.Owner does not have
            // sufficient reserve for the Loan object (tecINSUFFICIENT_RESERVE).
            // Use an IOU so the lender's XRP balance is only relevant to
            // the owner reserve for the Loan object created by LoanSet.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::IOU);

            // Drain the lender's XRP down to its current reserve, leaving
            // nothing to cover the additional owner reserve for the Loan
            // object that LoanSet creates on the LoanBroker owner.
            auto const amt =
                env.balance(lender) - accountReserve(*env.current(), lender.id(), env.journal);
            env(pay(lender, issuer, amt));
            env.close();

            propose(
                env,
                broker,
                lender,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(tecINSUFFICIENT_RESERVE));
        }

        // XLS-66 spec 3.8.5.3.4 → 3.8.5.2.9: Vault pseudo-account is frozen
        // for the asset (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // The issuer freezes the trust line (IOU) or locks the MPToken (MPT)
        // on the vault pseudo-account before LoanSet is submitted. The
        // proposal must be rejected by checkLoanFreeze in preclaim, and no
        // pending Loan is created. XRP cannot be frozen, so it is excluded.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanSet with frozen vault pseudo-account ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);
            auto const loanKeylet = nextLoanKeylet(env, broker);

            auto const vaultPseudo = [&]() {
                auto const v = env.le(broker.vaultKeylet());
                return Account("vault pseudo-account", v->at(sfAccount));
            }();

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, vaultPseudo[iouCurrency_](0), tfSetFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = vaultPseudo, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            propose(
                env,
                broker,
                lender,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(expected));
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        // XLS-66 spec 3.8.5.3.4 → 3.8.5.2.10: LoanBroker pseudo-account is
        // deep frozen for the asset (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Same as above, but for the LoanBroker pseudo-account (deep freeze).
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanSet with deep frozen broker pseudo-account ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);
            auto const loanKeylet = nextLoanKeylet(env, broker);

            auto const brokerPseudo = [&]() {
                auto const b = env.le(broker.brokerKeylet());
                return Account("broker pseudo-account", b->at(sfAccount));
            }();

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, brokerPseudo[iouCurrency_](0), tfSetFreeze | tfSetDeepFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = brokerPseudo, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            propose(
                env,
                broker,
                lender,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(expected));
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        // XLS-66 spec 3.8.5.3.4 → 3.8.5.2.11: Borrower is frozen for the
        // asset (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Same as above, but for the Borrower.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanSet with frozen borrower (" << assetTypeName(assetType)
                     << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);
            auto const loanKeylet = nextLoanKeylet(env, broker);

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, borrower[iouCurrency_](0), tfSetFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = borrower, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            propose(
                env,
                broker,
                lender,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(expected));
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        // XLS-66 spec 3.8.5.3.4 → 3.8.5.2.12: LoanBroker.Owner is deep frozen
        // for the asset (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Same as above, but for the LoanBroker owner (deep freeze).
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanSet with deep frozen broker owner ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);
            auto const loanKeylet = nextLoanKeylet(env, broker);

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, lender[iouCurrency_](0), tfSetFreeze | tfSetDeepFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = lender, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            propose(
                env,
                broker,
                lender,
                borrower,
                (env.now() + 1h).time_since_epoch().count(),
                Ter(expected));
            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Two-step: LoanAccept with insufficient reserve");

            // XLS-66 spec 3.9.3.2.5: Borrower does not have sufficient reserve
            // for the Loan object (tecINSUFFICIENT_RESERVE).
            // Use an IOU so the borrower's XRP balance is only relevant to
            // the owner reserve, not to receiving the loan asset.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::IOU);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // Drain the borrower's XRP down to its current reserve, leaving
            // nothing to cover the additional owner reserve for the Loan
            // object that acceptance transfers to the borrower.
            auto const amt =
                env.balance(borrower) - accountReserve(*env.current(), borrower.id(), env.journal);
            env(pay(borrower, issuer, amt));
            env.close();

            env(accept(borrower, loanKeylet.key), Ter(tecINSUFFICIENT_RESERVE));
            expectStillPending(env, loanKeylet);
        }

        // XLS-66 spec 3.9.3.2.6: Vault pseudo-account is frozen for the asset
        // (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Between the LoanSet proposal and the LoanAccept, the issuer
        // freezes the trust line (IOU) or locks the MPToken (MPT) on the
        // vault pseudo-account, which is about to disburse the principal.
        // Acceptance must be rejected. XRP cannot be frozen, so it is
        // excluded.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanAccept with frozen vault pseudo-account ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const vaultPseudo = [&]() {
                auto const v = env.le(broker.vaultKeylet());
                return Account("vault pseudo-account", v->at(sfAccount));
            }();

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, vaultPseudo[iouCurrency_](0), tfSetFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = vaultPseudo, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            env(accept(borrower, loanKeylet.key), Ter(expected));
            expectStillPending(env, loanKeylet);
        }

        // XLS-66 spec 3.9.3.2.7: LoanBroker pseudo-account is deep frozen for
        // the asset (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Between the LoanSet proposal and the LoanAccept, the issuer deep
        // freezes the trust line (IOU) or locks the MPToken (MPT) on the
        // LoanBroker pseudo-account, which is the fallback recipient of
        // LoanPay fees. Acceptance must be rejected. XRP cannot be frozen,
        // so it is excluded.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanAccept with deep frozen broker pseudo-account ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const brokerPseudo = [&]() {
                auto const b = env.le(broker.brokerKeylet());
                return Account("broker pseudo-account", b->at(sfAccount));
            }();

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, brokerPseudo[iouCurrency_](0), tfSetFreeze | tfSetDeepFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = brokerPseudo, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            env(accept(borrower, loanKeylet.key), Ter(expected));
            expectStillPending(env, loanKeylet);
        }

        // XLS-66 spec 3.9.3.2.8: Borrower is frozen for the asset
        // (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Between the LoanSet proposal and the LoanAccept, the issuer
        // freezes the trust line (IOU) or locks the MPToken (MPT) on the
        // borrower, who is about to receive the principal. Acceptance must
        // be rejected. XRP cannot be frozen, so it is excluded.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanAccept with frozen borrower (" << assetTypeName(assetType)
                     << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, borrower[iouCurrency_](0), tfSetFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = borrower, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            env(accept(borrower, loanKeylet.key), Ter(expected));
            expectStillPending(env, loanKeylet);
        }

        // XLS-66 spec 3.9.3.2.9: LoanBroker.Owner is deep frozen for the
        // asset (tecFROZEN for IOUs, tecLOCKED for MPTs).
        // Between the LoanSet proposal and the LoanAccept, the issuer deep
        // freezes the trust line (IOU) or locks the MPToken (MPT) on the
        // LoanBroker owner, who receives the origination fee. Acceptance
        // must be rejected. XRP cannot be frozen, so it is excluded.
        for (auto const assetType : {AssetType::IOU, AssetType::MPT})
        {
            testcase << "Two-step: LoanAccept with deep frozen broker owner ("
                     << assetTypeName(assetType) << ")";

            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            TER expected = tesSUCCESS;
            if (assetType == AssetType::IOU)
            {
                env(trust(issuer, lender[iouCurrency_](0), tfSetFreeze | tfSetDeepFreeze));
                env.close();
                expected = TER{tecFROZEN};
            }
            else
            {
                MPTTester mptt{env, issuer, broker.asset.raw().get<MPTIssue>().getMptID()};
                mptt.set({.account = issuer, .holder = lender, .flags = tfMPTLock});
                env.close();
                expected = TER{tecLOCKED};
            }

            env(accept(borrower, loanKeylet.key), Ter(expected));
            expectStillPending(env, loanKeylet);
        }

        {
            testcase("Two-step: LoanAccept when a holding cannot be added");

            // XLS-66 spec 3.9.3.2.10: cannot add asset holding for the
            // Vault.Asset (tecNO_PERMISSION / terNO_RIPPLE for IOU with
            // asfDefaultRipple cleared).
            // Between the LoanSet proposal and the LoanAccept, the IOU
            // issuer clears asfDefaultRipple, so a fresh holding for the
            // vault asset can no longer be established. Acceptance must be
            // rejected by the canAddHolding check in checkLoanFreeze. Only
            // the IOU path is reachable: for MPT, MPTCanTransfer is required
            // to create the vault/broker and MPT flags are immutable.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::IOU);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            env(fclear(issuer, asfDefaultRipple));
            env.close();

            env(accept(borrower, loanKeylet.key), Ter(terNO_RIPPLE));
            expectStillPending(env, loanKeylet);
        }

        {
            testcase("Two-step: LoanAccept with unauthorized borrower (MPT)");

            // XLS-66 spec 3.9.3.2.11: Borrower is not authorized for the
            // asset (tecNO_AUTH).
            // The MPT requires holder authorization. The borrower is
            // authorized at LoanSet proposal time so the proposal succeeds,
            // then the issuer revokes the borrower's MPToken authorization
            // before LoanAccept. Disbursement in doApply fails the
            // requireAuth(StrongAuth) check. Only the MPT path is
            // reachable: XRP has no authorization concept, and IOU trust
            // line authorization cannot be revoked once granted.
            Env env(*this, features);

            env.fund(XRP(1'000'000), issuer, noripple(lender), borrower);
            env.close();

            MPTTester asset(
                {.env = env,
                 .issuer = issuer,
                 .holders = {lender, borrower},
                 .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
                 .authHolder = true});

            env(pay(issuer, lender, asset(2'000'000)));
            env.close();

            auto const broker = createVaultAndBroker(env, asset, lender);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // Issuer revokes the borrower's MPToken authorization.
            asset.authorize({.account = issuer, .holder = borrower, .flags = tfMPTUnauthorize});
            env.close();

            env(accept(borrower, loanKeylet.key), Ter(tecNO_AUTH));
            expectStillPending(env, loanKeylet);
        }

        {
            testcase("Two-step: LoanAccept with unauthorized broker owner (MPT)");

            // XLS-66 spec 3.9.3.2.12: LoanBroker.Owner is not authorized for
            // the asset (tecNO_AUTH).
            // Same rationale as the unauthorized-borrower case, but this
            // time the issuer revokes the broker owner's MPToken
            // authorization between proposal and accept. disburseLoan's
            // requireAuth(brokerOwner, StrongAuth) check fails.
            Env env(*this, features);

            env.fund(XRP(1'000'000), issuer, noripple(lender), borrower);
            env.close();

            MPTTester asset(
                {.env = env,
                 .issuer = issuer,
                 .holders = {lender, borrower},
                 .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
                 .authHolder = true});

            env(pay(issuer, lender, asset(2'000'000)));
            env.close();

            auto const broker = createVaultAndBroker(env, asset, lender);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // Issuer revokes the broker owner's MPToken authorization.
            asset.authorize({.account = issuer, .holder = lender, .flags = tfMPTUnauthorize});
            env.close();

            env(accept(borrower, loanKeylet.key), Ter(tecNO_AUTH));
            expectStillPending(env, loanKeylet);
        }

        // Deleting a pending loan reverses the proposal-time bookkeeping and
        // releases the broker owner's reserve. It can be done by either the
        // broker owner or the borrower.
        auto const testDeletePending = [&](AssetType assetType, Account const& deleter) {
            Env env(*this, features);
            auto const broker = makeBroker(env, assetType);

            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);
            auto const lenderOwners0 = env.ownerCount(lender);
            auto const borrowerOwners0 = env.ownerCount(borrower);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            // A StartDate comfortably in the future.
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender) == lenderOwners0 + 1);

            // An unrelated account cannot delete the loan.
            env(del(evan, loanKeylet.key), Ter(tecNO_PERMISSION));

            env(del(deleter, loanKeylet.key));
            env.close();

            // The loan is gone, the reserve is released, and the vault
            // bookkeeping is fully reversed.
            BEAST_EXPECT(!env.le(loanKeylet));
            BEAST_EXPECT(env.ownerCount(lender) == lenderOwners0);
            BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwners0);

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
            testDeletePending(assetType, lender);

            testcase << "Two-step: LoanDelete of pending loan by borrower ("
                     << assetTypeName(assetType) << ")";
            testDeletePending(assetType, borrower);
        }

        {
            testcase("Two-step: LoanBrokerDelete blocked by pending loan");

            // XLS-66 spec 3.4.3.2.3: LoanBroker.OwnerCount != 0 (has
            // outstanding loans) → tecHAS_OBLIGATIONS. A pending loan bumps
            // the LoanBroker's OwnerCount, so LoanBrokerDelete must fail
            // while the pending loan is outstanding, just as it does for an
            // active (accepted) loan. Once the pending loan is deleted, the
            // broker can be deleted too.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower, (env.now() + 1h).time_since_epoch().count());
            env.close();

            // The loan is pending; the broker's OwnerCount is non-zero.
            if (auto const b = env.le(broker.brokerKeylet()); BEAST_EXPECT(b))
                BEAST_EXPECT(b->at(sfOwnerCount) != 0u);

            env(jtx::loan_broker::del(lender, broker.brokerID), Ter(tecHAS_OBLIGATIONS));
            env.close();

            // Broker and loan are both still present.
            BEAST_EXPECT(env.le(broker.brokerKeylet()));
            BEAST_EXPECT(env.le(loanKeylet));

            // Delete the pending loan, then the broker can be deleted.
            env(del(lender, loanKeylet.key));
            env.close();
            env(jtx::loan_broker::del(lender, broker.brokerID));
            env.close();
            BEAST_EXPECT(!env.le(broker.brokerKeylet()));
        }

        {
            testcase("Two-step: two pending loans coexist on the same broker");

            // XLS-66 flow: two pending proposals from the same broker each
            // contribute independently to DebtTotal, AssetsReserved, and
            // OwnerCount. Deleting one pending loan must leave the other's
            // bookkeeping untouched.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            Number const principal = broker.asset(200).number();
            auto const vault0 = readVault(env, broker);
            auto const broker0 = readBroker(env, broker);

            // Propose L1 (borrower) to establish a baseline delta.
            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower,
                    (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const vault1 = readVault(env, broker);
            auto const broker1 = readBroker(env, broker);
            Number const l1DebtDelta = broker1.debtTotal - broker0.debtTotal;
            BEAST_EXPECT(vault1.reserved == vault0.reserved + principal);
            BEAST_EXPECT(broker1.ownerCount == broker0.ownerCount + 1);

            // Propose L2 (evan) on the same broker while L1 is still
            // pending. Each proposal contributes an equal delta.
            auto const l2Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, evan,
                    (env.now() + 1h).time_since_epoch().count());
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
            env(del(lender, l1Keylet.key));
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

            // XLS-66 spec 3.8.5.3.4 → 3.8.5.2.19: a first pending loan's
            // DebtTotal contribution counts toward the LoanBroker's debt
            // cap. Set DebtMaximum to L1's DebtTotal so a same-sized L2
            // fails with tecLIMIT_EXCEEDED.
            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            auto const l1Keylet = nextLoanKeylet(env, broker);
            propose(env, broker, lender, borrower,
                    (env.now() + 1h).time_since_epoch().count());
            env.close();

            auto const brokerL1 = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerL1))
                return;
            Number const debtAfterL1 = brokerL1->at(sfDebtTotal);

            // Tighten DebtMaximum to exactly L1's DebtTotal.
            env(jtx::loan_broker::set(lender, broker.vaultID),
                jtx::loan_broker::kLoanBrokerId(broker.brokerID),
                jtx::loan_broker::kDebtMaximum(debtAfterL1));
            env.close();

            // Second proposal exceeds the debt cap.
            propose(env, broker, lender, evan,
                    (env.now() + 1h).time_since_epoch().count(),
                    Ter(tecLIMIT_EXCEEDED));
            env.close();

            // L1 remains pending; L2 was not created.
            if (auto const l1 = env.le(l1Keylet); BEAST_EXPECT(l1))
                BEAST_EXPECT(l1->isFlag(lsfLoanPending));
        }

        // XLS-66 flow (Batch + V1.1) two-step: a Batch containing an inner
        // LoanSet with Borrower + StartDate (no Counterparty, no
        // CounterpartySignature) is the analogue of the immediate-flow
        // batch-success path (LoanLifecycle_test.cpp "Batch Bypass
        // Counterparty"). The outer batch is signed by the LoanBroker.Owner
        // (lender); no additional batch signer is required since two-step
        // has no counterparty consent step. Gated on lendingBatchEnabled to
        // match the existing pattern: while ttLOAN_SET is on
        // Batch::kDisabledTxTypes, the batch fails with temINVALID_INNER_BATCH;
        // once the disabled-list is updated, it must create a pending loan.
        {
            bool const lendingBatchEnabled =
                !std::ranges::any_of(Batch::kDisabledTxTypes, [](auto const& disabled) {
                    return disabled == ttLOAN_SET;
                });

            testcase(
                lendingBatchEnabled
                    ? "Two-step: Batch inner LoanSet creates a pending loan"
                    : "Two-step: Batch inner LoanSet rejected while ttLOAN_SET is disabled");

            Env env(*this, features);
            auto const broker = makeBroker(env, AssetType::XRP);

            Number const principal = broker.asset(200).number();
            std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();

            auto const brokerState0 = env.le(broker.brokerKeylet());
            if (!BEAST_EXPECT(brokerState0))
                return;
            Number const debtTotal0 = brokerState0->at(sfDebtTotal);
            std::uint32_t const brokerOwnerCount0 = brokerState0->at(sfOwnerCount);

            auto const loanKeylet = nextLoanKeylet(env, broker);
            auto const lenderSeq = env.seq(lender);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);

            env(batch::outer(lender, lenderSeq, batchFee, tfAllOrNothing),
                batch::Inner(
                    env.json(
                        set(lender, broker.brokerID, principal),
                        kBorrower(borrower.id()),
                        kStartDate(startDate),
                        kInterestRate(interest),
                        kPaymentTotal(payTotal),
                        kPaymentInterval(payInterval),
                        Sig(kNone),
                        Fee(kNone),
                        Seq(kNone)),
                    lenderSeq + 1),
                batch::Inner(pay(lender, borrower, XRP(1)), lenderSeq + 2),
                Ter(lendingBatchEnabled ? TER(tesSUCCESS) : TER(temINVALID_INNER_BATCH)));
            env.close();

            if (lendingBatchEnabled)
            {
                if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                {
                    BEAST_EXPECT(loan->isFlag(lsfLoanPending));
                    BEAST_EXPECT(loan->at(sfBorrower) == borrower.id());
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
                // The batch was rejected up front; no loan was created and
                // broker bookkeeping is unchanged.
                BEAST_EXPECT(!env.le(loanKeylet));
                if (auto const b = env.le(broker.brokerKeylet()); BEAST_EXPECT(b))
                {
                    BEAST_EXPECT(b->at(sfDebtTotal) == debtTotal0);
                    BEAST_EXPECT(b->at(sfOwnerCount) == brokerOwnerCount0);
                }
            }
        }
    }

public:
    void
    run() override
    {
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2, featureLendingProtocolV1_1},
                 all_))
            testTwoStep(features);
    }
};

BEAST_DEFINE_TESTSUITE(LoanTwoStep, tx, xrpl);

}  // namespace xrpl::test
