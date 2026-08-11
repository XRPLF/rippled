#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace xrpl::test {

// While a TransactionProposal exists, the Ticket it is keyed to is reserved
// (XLS-0103 §4.2.1): a transaction may consume it only if its payload matches
// the stored ProposedTransaction, so unrelated target-account activity cannot
// invalidate the proposal while signatures are being collected. When the
// matching transaction does consume the ticket, the ledger deletes the
// now-stale proposal and releases the reserve it holds against its Owner
// (XLS-0103 §4.5).
struct TransactionProposalReservedTicket_test : public beast::unit_test::Suite
{
    // Create a live proposal owned by proposer holding the given payload.
    static void
    makeProposal(jtx::Env& env, jtx::Account const& proposer, json::Value const& payload)
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        env(proposal::create(proposer, payload, proposal::expiration(env, 1000s)));
        env.close();
    }

    // A transaction that is not the proposed transaction cannot consume the
    // reserved ticket: it fails with terTICKET_RESERVED and neither the
    // ticket nor the proposal is touched. The target's sequence, and its
    // other tickets, stay freely spendable.
    void
    testUnrelatedSpendBlocked(FeatureBitset features)
    {
        testcase("unrelated spend of a reserved ticket is blocked");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};    // the proposer
        Account const target{"target"};  // the account the proposal is for
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target, 2);
        json::Value const payload =
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
        makeProposal(env, alice, payload);

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 2);

        // A different transaction type.
        env(noop(target), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        // The proposed transaction with one payload field off: the amount,
        // the destination, or the fee. Signature fields are the only
        // latitude a submitter has (XLS-0103 §4.2.1).
        env(pay(target, bob, XRP(2)), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        env(pay(target, alice, XRP(1)), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        env(pay(target, bob, XRP(1)),
            ticket::Use(ticketSeq),
            Fee(env.current()->fees().base * 2),
            Ter(terTICKET_RESERVED));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 2);

        // The reservation binds one ticket, not the account: its sequence
        // and its other tickets spend freely, and the proposal survives.
        env(noop(target));
        env(noop(target), ticket::Use(ticketSeq + 1));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, target) == 1);
    }

    // The proposal's own transaction consumes the reserved ticket through
    // the ordinary submission path (XLS-0103 §6.5). How it is signed is
    // irrelevant to the match — signature fields are excluded — and applying
    // it auto-deletes the proposal and refunds the Owner (XLS-0103 §4.5).
    void
    testMatchingPayloadExecutes(FeatureBitset features)
    {
        testcase("matching payload consumes the ticket and cleans up");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, target, bob, carol);
        env.close();

        // Single-signed with the target's own key: the stored payload has an
        // empty SigningPubKey, the submission a populated one plus a
        // TxnSignature. Both are signature fields, so they still match.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            makeProposal(
                env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

            auto const bobBefore = env.balance(bob);
            env(pay(target, bob, XRP(1)), ticket::Use(ticketSeq));
            env.close();

            BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), ticketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, target) == 0);
        }

        // Multi-signed under the target's SignerList: the Signers array the
        // submission carries is a signature field too. The payload's Fee was
        // fixed at creation, so a proposer expecting multi-signed submission
        // must budget for the signatures then (XLS-0103 §4.2.1).
        {
            env(signers(target, 2, {{bob, 1}, {carol, 1}}));
            env.close();

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const msigFee = env.current()->fees().base * 3;
            json::Value proposedTx = pay(target, bob, XRP(1));
            proposedTx[jss::Fee] = std::to_string(msigFee.drops());
            makeProposal(env, alice, proposal::unsignedPayload(env, proposedTx, ticketSeq));

            auto const bobBefore = env.balance(bob);
            env(pay(target, bob, XRP(1)), ticket::Use(ticketSeq), Msig(bob, carol), Fee(msigFee));
            env.close();

            BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            // The consumed ticket's count is released; the SignerList set up
            // above remains the target's only owned object.
            BEAST_EXPECT(ownerCount(env, target) == 1);
        }
    }

    // Cancelling the proposal removes the reservation. terTICKET_RESERVED is
    // a retry: the very transaction the reservation blocked is held and
    // applies on its own once a Cancel frees the ticket.
    void
    testCancelFreesTicket(FeatureBitset features)
    {
        testcase("cancelling the proposal frees the ticket");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        // After a Cancel, the ticket spends freely.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            makeProposal(
                env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

            env(proposal::cancel(alice, keylet::txProposal(target.id(), ticketSeq).key));
            env.close();

            env(noop(target), ticket::Use(ticketSeq));
            env.close();
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), ticketSeq)));
        }

        // A blocked transaction is held for retry, and consumes the ticket
        // by itself once the proposal is gone. Two closes: the held
        // transaction and the Cancel land in the same ledger in salted
        // canonical order, so the retry may run before the Cancel once more.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            makeProposal(
                env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

            env(noop(target), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));

            env(proposal::cancel(alice, keylet::txProposal(target.id(), ticketSeq).key));
            env.close();
            env.close();

            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), ticketSeq)));
        }
    }

    // A ticket is consumed even when its transaction fails with a tec. The
    // reservation lets only the matching payload get that far, and once the
    // ticket is durably gone the proposal is cleaned up all the same.
    void
    testMatchingTecStillCleansUp(FeatureBitset features)
    {
        testcase("matching payload failing with tec still cleans up");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        // A proposed payment the target cannot fund. Creation only runs the
        // payload's stateless checks, so the proposal stores it happily.
        makeProposal(
            env,
            alice,
            proposal::unsignedPayload(env, pay(target, bob, XRP(1'000'000)), ticketSeq));

        env(pay(target, bob, XRP(1'000'000)), ticket::Use(ticketSeq), Ter(tecUNFUNDED_PAYMENT));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 0);
    }

    // The reservation is keyed by target account and ticket, so another
    // account spending its own ticket of the same numeric sequence is
    // neither blocked nor does it touch the proposal.
    void
    testOtherAccountsSameNumberedTicket(FeatureBitset features)
    {
        testcase("other account's same-numbered ticket is unaffected");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        // target and bob were funded together, so creating one ticket each
        // in the same ledger gives their tickets the same numeric sequence.
        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        std::uint32_t const bobTicketSeq = env.seq(bob) + 1;
        env(ticket::create(target, 1));
        env(ticket::create(bob, 1));
        env.close();
        BEAST_EXPECT(targetTicketSeq == bobTicketSeq);

        makeProposal(
            env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq));

        env(noop(bob), ticket::Use(bobTicketSeq));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, targetTicketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), targetTicketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
    }

    // The proposer may be the target itself. The reservation still binds it
    // — its own unrelated spend is blocked — and the matching execution
    // lands the ticket bookkeeping and the reserve release on the same
    // account-root SLE within one ticketDelete call, pinning that aliasing.
    void
    testProposerIsTarget(FeatureBitset features)
    {
        testcase("proposer is the target account");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, alice);
        makeProposal(
            env, alice, proposal::unsignedPayload(env, pay(alice, bob, XRP(1)), ticketSeq));

        auto const sle = proposal::entry(env, alice, ticketSeq);
        BEAST_EXPECT(sle && sle->getAccountID(sfOwner) == alice.id());
        // One Ticket plus the proposal's increments, all against alice.
        BEAST_EXPECT(ownerCount(env, alice) == 1 + proposal::kProposalOwnerCount);

        env(noop(alice), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        env.close();
        BEAST_EXPECT(proposal::entry(env, alice, ticketSeq));

        env(pay(alice, bob, XRP(1)), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, alice, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A proposed Batch: the reservation covers its outer ticket, only the
    // identical Batch consumes it, and its deletion must release the larger
    // reserve a Batch proposal holds (XLS-0103 §4.4).
    void
    testBatchProposal(FeatureBitset features)
    {
        testcase("proposed Batch: only the identical Batch spends the ticket");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // Build the payload once; the same JSON is stored in the proposal
        // and later submitted, differing only by the signature env() adds.
        auto const innerSeq = env.seq(target);
        json::Value const payload = proposal::unsignedBatch(
            env,
            target,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), innerSeq),
             proposal::innerTx(pay(target, bob, XRP(1)), innerSeq + 1)});
        makeProposal(env, alice, payload);
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);

        // A different Batch spending the reserved outer ticket is blocked.
        env(batch::outer(target, 0, batch::calcBatchFee(env, 0, 2), tfAllOrNothing),
            batch::Inner(pay(target, bob, XRP(5)), innerSeq),
            batch::Inner(pay(target, bob, XRP(5)), innerSeq + 1),
            ticket::Use(ticketSeq),
            Ter(terTICKET_RESERVED));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));

        // The proposed Batch itself executes: outer ticket consumed, both
        // inner payments applied, proposal deleted, larger reserve released.
        auto const bobBefore = env.balance(bob);
        env(payload);
        env.close();

        BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(2));
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // An inner Batch transaction validates its ticket through the same path
    // as a standalone one, so a reserved ticket blocks it too — an inner
    // transaction carries tfInnerBatchTxn and a zero fee, so it can never
    // match a stored payload, which may not carry that flag (XLS-0103
    // §4.2.1). Under tfAllOrNothing the whole Batch is discarded.
    void
    testInnerBatchSpendBlocked(FeatureBitset features)
    {
        testcase("inner batch transaction cannot spend a reserved ticket");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value const payload =
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
        makeProposal(env, alice, payload);

        // The second inner transaction spends the reserved ticket — even
        // carrying the very payment the proposal holds, its flag and fee
        // make it a different payload, so it fails and tfAllOrNothing
        // discards its sibling too.
        auto const bobBefore = env.balance(bob);
        auto const seq = env.seq(target);
        env(batch::outer(target, seq, batch::calcBatchFee(env, 0, 2), tfAllOrNothing),
            batch::Inner(pay(target, bob, XRP(1)), seq + 1),
            batch::Inner(pay(target, bob, XRP(1)), 0, ticketSeq));
        env.close();

        BEAST_EXPECT(env.balance(bob) == bobBefore);
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

        // The standalone matching payment still goes through.
        env(pay(target, bob, XRP(1)), ticket::Use(ticketSeq));
        env.close();
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // Deleting the target account sweeps its Tickets without consuming them
    // as a sequence proxy, so the reservation does not block it. A proposal
    // keyed to a swept ticket can never execute and is cleaned up with it,
    // refunding the proposer (XLS-0103 §4.5).
    void
    testTargetAccountDeleted(FeatureBitset features)
    {
        testcase("deleting the target account deletes the proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        makeProposal(
            env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

        incLgrSeqForAccDel(env, target);
        env(acctdelete(target, bob), Fee(env.current()->fees().increment));
        env.close();

        BEAST_EXPECT(!env.le(keylet::account(target.id())));
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A TransactionProposal blocks its Owner's account deletion (XLS-0103
    // §4.5). Beyond the spec requirement, this blocker is what guarantees
    // the AccountDelete ticket sweep never deletes a proposal out of the
    // very owner directory it is iterating: any proposal reached through a
    // swept ticket is necessarily owned by an account other than the one
    // being deleted.
    void
    testProposalBlocksOwnerAccountDelete(FeatureBitset features)
    {
        testcase("proposal blocks its owner's account deletion");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};  // proposer, tries to delete itself
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        makeProposal(
            env, alice, proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq));

        incLgrSeqForAccDel(env, alice);
        env(acctdelete(alice, bob), Fee(env.current()->fees().increment), Ter(tecHAS_OBLIGATIONS));
        env.close();

        BEAST_EXPECT(env.le(keylet::account(alice.id())));
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
    }

    void
    run() override
    {
        using namespace jtx;
        testUnrelatedSpendBlocked(testableAmendments());
        testMatchingPayloadExecutes(testableAmendments());
        testCancelFreesTicket(testableAmendments());
        testMatchingTecStillCleansUp(testableAmendments());
        testOtherAccountsSameNumberedTicket(testableAmendments());
        testProposerIsTarget(testableAmendments());
        testBatchProposal(testableAmendments());
        testInnerBatchSpendBlocked(testableAmendments());
        testTargetAccountDeleted(testableAmendments());
        testProposalBlocksOwnerAccountDelete(testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalReservedTicket, app, xrpl);

}  // namespace xrpl::test
