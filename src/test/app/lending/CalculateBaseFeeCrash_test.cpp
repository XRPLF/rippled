#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/tx/applySteps.h>

#include <memory>
#include <string>

namespace xrpl::test {

// Runs a transaction whose calculateBaseFee throws through the real
// transaction-processing path: NetworkOPs -> JobQueue thread -> TxQ.
//
// That thread has no exception handler, so before calculateBaseFee was made
// exception-safe the exception escaped and the server terminated. To see that,
// revert the fix and run this suite: the process aborts inside rendezvous().
//
// LoanPay is used only because it is the one transactor whose fee calculation
// can throw (Number overflow on a huge Amount, with fixCleanup3_1_3 off).
class CalculateBaseFeeCrash_test : public LoanTestBase
{
    void
    testThrowingFeeCalculationOnJobQueueThread()
    {
        testcase("calculateBaseFee throwing on a JobQueue thread does not crash");

        using namespace jtx;
        using namespace loan;
        using namespace lending;

        Env env(*this, all_ - fixCleanup3_1_3);
        Account const issuer{"issuer"};
        Account const alice{"alice"};

        env.fund(XRP(10'000), issuer, alice);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();

        // The amount must be an IOU: XRP is capped too low to overflow.
        PrettyAsset const usd{issuer["USD"]};
        env(trust(alice, usd(10'000'000)));
        env.close();
        env(pay(issuer, alice, usd(2'000'000)));
        env.close();

        auto const broker = createVaultAndBroker(env, usd, alice);
        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));

        env(set(alice, broker.brokerID, usd(1'000).value()),
            Sig(sfCounterpartySignature, alice),
            kPaymentTotal(50),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        // With fewer payments left than this, calculateBaseFee returns early
        // and never reaches the arithmetic that overflows.
        BEAST_EXPECT(loanSle->at(sfPaymentRemaining) > kLoanPaymentsPerFeeIncrement);

        // Sign the transaction without submitting it.
        auto const jt = env.jt(pay(alice, loanKeylet.key, usd(Number(1, 70))));
        std::string reason;
        auto transaction = std::make_shared<Transaction>(jt.stx, reason, env.app());

        // bLocal = false is the path a peer-relayed transaction takes: it is
        // queued and applied later on a JobQueue thread. Wait for that job.
        env.app().getOPs().processTransaction(transaction, false, false, NetworkOPs::FailHard::No);
        env.app().getJobQueue().rendezvous();

        // Still alive. The transaction was rejected and nothing changed.
        BEAST_EXPECT(!transaction->getApplying());
        BEAST_EXPECT(transaction->getResult() == tefEXCEPTION);
        auto const after = env.le(loanKeylet);
        if (BEAST_EXPECT(after))
        {
            BEAST_EXPECT(after->at(sfPaymentRemaining) == loanSle->at(sfPaymentRemaining));
        }
    }

    void
    testNegativeAmountDoesNotCrash()
    {
        testcase("calculateBaseFee throwing on negative Amount does not crash");

        using namespace jtx;
        using namespace loan;
        using namespace lending;

        // With fixCleanup3_1_3 enabled, the positive-overpayment overflow is capped.
        // Disable fixCleanup3_4_0 so LoanPay::calculateBaseFee still executes the
        // legacy arithmetic on a negative Amount and throws (regression coverage
        // for calculateBaseFee's exception-safety wrapper).
        Env env(*this, all_ - fixCleanup3_4_0);
        Account const issuer{"issuer"};
        Account const alice{"alice"};

        env.fund(XRP(10'000), issuer, alice);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();

        // The amount must be an IOU: XRP is capped too low to overflow.
        PrettyAsset const usd{issuer["USD"]};
        env(trust(alice, usd(10'000'000)));
        env.close();
        env(pay(issuer, alice, usd(2'000'000)));
        env.close();

        auto const broker = createVaultAndBroker(env, usd, alice);
        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));

        env(set(alice, broker.brokerID, usd(1'000).value()),
            Sig(sfCounterpartySignature, alice),
            kPaymentTotal(50),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        // With fewer payments left than this, calculateBaseFee returns early
        // and never reaches the arithmetic that overflows.
        BEAST_EXPECT(loanSle->at(sfPaymentRemaining) > kLoanPaymentsPerFeeIncrement);

        // Negative Amount, huge magnitude. preflight rejects it with
        // temBAD_AMOUNT, but NetworkOPsImp::apply still calls
        // getTxRequiredFeeAndSeq -> calculateBaseFee for it on the JobQueue
        // thread, which throws. Before the fix, that unguarded throw
        // terminated the server; revert the fix and this suite aborts inside
        // rendezvous().
        auto const jt = env.jt(pay(alice, loanKeylet.key, usd(Number(-1, 70))));
        std::string reason;
        auto transaction = std::make_shared<Transaction>(jt.stx, reason, env.app());

        // bLocal = false is the peer-relayed path: queued and applied later on
        // a JobQueue thread. Wait for that job.
        env.app().getOPs().processTransaction(transaction, false, false, NetworkOPs::FailHard::No);
        env.app().getJobQueue().rendezvous();

        // Still alive. The transaction was rejected at preflight and the loan
        // is unchanged; the post-apply fee lookup's throw was swallowed.
        BEAST_EXPECT(!transaction->getApplying());
        BEAST_EXPECT(transaction->getResult() == temBAD_AMOUNT);
        auto const after = env.le(loanKeylet);
        if (BEAST_EXPECT(after))
        {
            BEAST_EXPECT(after->at(sfPaymentRemaining) == loanSle->at(sfPaymentRemaining));
        }
    }

    void
    testNegativeAmountFeeGuard(bool fixEnabled)
    {
        testcase(
            std::string("negative Amount fee guard, fixCleanup3_4_0 ") +
            (fixEnabled ? "enabled" : "disabled"));

        using namespace jtx;
        using namespace loan;
        using namespace lending;

        Env env(*this, fixEnabled ? all_ : all_ - fixCleanup3_4_0);
        Account const issuer{"issuer"};
        Account const alice{"alice"};

        env.fund(XRP(10'000), issuer, alice);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();

        // The amount must be an IOU: XRP is capped too low to overflow.
        PrettyAsset const usd{issuer["USD"]};
        env(trust(alice, usd(10'000'000)));
        env.close();
        env(pay(issuer, alice, usd(2'000'000)));
        env.close();

        auto const broker = createVaultAndBroker(env, usd, alice);
        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle))
            return;
        auto const loanKeylet =
            keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));

        env(set(alice, broker.brokerID, usd(1'000).value()),
            Sig(sfCounterpartySignature, alice),
            kPaymentTotal(50),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        // With fewer payments left than this, calculateBaseFee returns early
        // and never reaches the arithmetic that overflows.
        BEAST_EXPECT(loanSle->at(sfPaymentRemaining) > kLoanPaymentsPerFeeIncrement);

        // Sign the transaction without submitting it.
        auto const jt = env.jt(pay(alice, loanKeylet.key, usd(Number(-1, 70))));

        // normalCost is Transactor::calculateBaseFee: one base fee, since a
        // singly-signed LoanPay carries neither sfSigners nor sfSponsorSignature.
        auto const normalCost = env.current()->fees().base;

        // The guarded branch, checked directly.
        auto const computed = calculateBaseFee(*env.current(), *jt.stx);
        if (fixEnabled)
        {
            if (BEAST_EXPECT(computed.has_value()))
                BEAST_EXPECT(*computed == normalCost);
        }
        else
        {
            // The int64 cast overflows; invokeCalculateBaseFee turns the
            // exception into tefEXCEPTION.
            if (BEAST_EXPECT(!computed.has_value()))
                BEAST_EXPECT(computed.error() == tefEXCEPTION);
        }

        // The same difference as observed through the real processing path.
        // bLocal = false is the peer-relayed path: queued and applied later on
        // a JobQueue thread. preflight rejects the transaction with
        // temBAD_AMOUNT either way, but NetworkOPsImp::apply still calls
        // getTxQ().getTxRequiredFeeAndSeq() -> calculateBaseFee for it.
        std::string reason;
        auto transaction = std::make_shared<Transaction>(jt.stx, reason, env.app());
        env.app().getOPs().processTransaction(transaction, false, false, NetworkOPs::FailHard::No);
        env.app().getJobQueue().rendezvous();

        BEAST_EXPECT(!transaction->getApplying());
        BEAST_EXPECT(transaction->getResult() == temBAD_AMOUNT);

        auto const state = transaction->getCurrentLedgerState();
        if (fixEnabled)
        {
            // getTxRequiredFeeAndSeq succeeded, so the required fee was
            // recorded. An unloaded open ledger scales at exactly kBaseLevel,
            // so the reported minimum is normalCost.
            if (BEAST_EXPECT(state.has_value()))
            {
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                BEAST_EXPECT(state->minFeeRequired == normalCost);
            }
        }
        else
        {
            // getTxRequiredFeeAndSeq returned tefEXCEPTION, so
            // setCurrentLedgerState was never called. There is no fee to
            // report at all, rather than a reported fee of zero.
            BEAST_EXPECT(!state.has_value());
        }

        // The loan is untouched in both configurations.
        auto const after = env.le(loanKeylet);
        if (BEAST_EXPECT(after))
        {
            BEAST_EXPECT(after->at(sfPaymentRemaining) == loanSle->at(sfPaymentRemaining));
        }
    }

public:
    void
    run() override
    {
        testThrowingFeeCalculationOnJobQueueThread();
        testNegativeAmountDoesNotCrash();
        testNegativeAmountFeeGuard(false);
        testNegativeAmountFeeGuard(true);
    }
};

BEAST_DEFINE_TESTSUITE(CalculateBaseFeeCrash, tx, xrpl);

}  // namespace xrpl::test
