#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
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
#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace xrpl::test {

// Automatic cleanup of a TransactionProposal when the proposed transaction's
// TicketSequence is consumed (XLS-0103 §4.5): any transaction of the target
// account that spends the ticket makes the proposal permanently unexecutable,
// so applying that transaction deletes the proposal and releases the reserve
// it holds against its Owner.
struct TransactionProposalAutoDelete_test : public beast::unit_test::Suite
{
    // A TransactionProposalCreate carrying an unsigned proposed transaction.
    static json::Value
    proposalCreate(
        jtx::Account const& proposer,
        json::Value const& proposedTx,
        std::uint32_t expiration)
    {
        json::Value jv;
        jv[jss::TransactionType] = "TransactionProposalCreate";
        jv[jss::Account] = proposer.human();
        jv[sfProposedTransaction.getJsonName()] = proposedTx;
        jv[sfExpiration.getJsonName()] = expiration;
        return jv;
    }

    // A proposed transaction in the form the ledger stores it: unsigned,
    // ticket-based, with the fee the target account will pay fixed now.
    static json::Value
    unsignedPayload(
        jtx::Env const& env,
        jtx::Account const& target,
        jtx::Account const& dest,
        std::uint32_t ticketSeq)
    {
        json::Value tx = jtx::pay(target, dest, jtx::XRP(1));
        tx[jss::Sequence] = 0;
        tx[sfTicketSequence.getJsonName()] = ticketSeq;
        tx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
        tx[jss::SigningPubKey] = "";
        return tx;
    }

    // Consuming the proposed transaction's ticket deletes the proposal and
    // refunds the Owner's reserve, whether the ticket is spent on the
    // proposal's own transaction or on something unrelated. Consuming a
    // different ticket, or the target's live sequence, leaves it untouched.
    void
    testTicketSpendDeletesProposal(FeatureBitset features)
    {
        testcase("ticket spend deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};    // the proposer
        Account const target{"target"};  // the account the proposals are for
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 2));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq + 1), expiration));
        env.close();

        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq + 1)));
        // Each proposal reserves several owner increments; the target owns
        // only its two Tickets.
        BEAST_EXPECT(ownerCount(env, alice) == 2 * proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 2);

        // A sequence-based transaction of the target consumes no ticket, so
        // both proposals survive.
        env(noop(target));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq + 1)));

        // The proposal's own transaction runs: the target submits the very
        // payment the first proposal holds, spending its ticket. That is the
        // completed-proposal case of XLS-0103 §6.5 — execution goes through
        // the ordinary path and the consumed ticket auto-deletes the proposal.
        env(pay(target, bob, XRP(1)), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq + 1)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 1);

        // The target spends the second ticket on something unrelated to the
        // proposal. The proposal can then never execute, so it is deleted all
        // the same.
        env(noop(target), ticket::Use(ticketSeq + 1));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq + 1)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 0);

        // Directory integrity: deletion must have unlinked the proposals from
        // alice's owner directory, not just erased the entries. A dangling
        // directory entry would make this AccountDelete fail.
        incLgrSeqForAccDel(env, alice);
        env(acctdelete(alice, bob), Fee(env.current()->fees().increment));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(alice.id())));
    }

    // The proposal is keyed by target account and ticket, so another account
    // consuming its own ticket of the same numeric sequence must not touch it.
    void
    testOtherAccountsTicket(FeatureBitset features)
    {
        testcase("other account's ticket does not delete proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        // target and bob were funded together, so creating one ticket each in
        // the same ledger gives their tickets the same numeric sequence.
        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        std::uint32_t const bobTicketSeq = env.seq(bob) + 1;
        env(ticket::create(target, 1));
        env(ticket::create(bob, 1));
        env.close();
        BEAST_EXPECT(targetTicketSeq == bobTicketSeq);

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, targetTicketSeq), expiration));
        env.close();

        env(noop(bob), ticket::Use(bobTicketSeq));
        env.close();

        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), targetTicketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
    }

    // A ticket is consumed even when its transaction fails with a tec, and
    // once consumed the proposal can never execute, so a claimed-fee failure
    // cleans up the proposal exactly as a success does.
    void
    testTecResultStillDeletes(FeatureBitset features)
    {
        testcase("tec result still deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));

        // The payment fails but claims a fee, which consumes the ticket.
        env(pay(target, bob, XRP(1'000'000)), ticket::Use(ticketSeq), Ter(tecUNFUNDED_PAYMENT));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 0);
    }

    // A proposed Batch reserves more increments than an ordinary proposal;
    // deletion must release exactly what creation reserved.
    void
    testBatchProposalReserveRefund(FeatureBitset features)
    {
        testcase("batch proposal refunds its larger reserve");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        auto inner = [&](std::uint32_t seq) {
            json::Value tx = pay(target, bob, XRP(1));
            tx[jss::Sequence] = seq;
            tx[jss::Fee] = "0";
            tx[jss::Flags] = tfInnerBatchTxn;
            tx[jss::SigningPubKey] = "";
            return tx;
        };

        json::Value proposedTx;
        proposedTx[jss::TransactionType] = jss::Batch;
        proposedTx[jss::Account] = target.human();
        proposedTx[jss::Flags] = tfAllOrNothing;
        proposedTx[jss::Sequence] = 0;
        proposedTx[sfTicketSequence.getJsonName()] = ticketSeq;
        proposedTx[jss::Fee] = std::to_string(batch::calcBatchFee(env, 0, 2).drops());
        proposedTx[jss::SigningPubKey] = "";
        proposedTx[jss::RawTransactions][0u][jss::RawTransaction] = inner(env.seq(target));
        proposedTx[jss::RawTransactions][1u][jss::RawTransaction] = inner(env.seq(target) + 1);

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, proposedTx, expiration));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);

        env(noop(target), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // An inner Batch transaction consumes its ticket through the same path as
    // a standalone transaction, so it too cleans up a proposal keyed to it.
    void
    testInnerBatchTicketSpend(FeatureBitset features)
    {
        testcase("inner batch transaction ticket spend deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));

        // The target submits a Batch whose second inner transaction spends
        // the proposal's ticket.
        auto const seq = env.seq(target);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(target, seq, batchFee, tfAllOrNothing),
            batch::Inner(pay(target, bob, XRP(1)), seq + 1),
            batch::Inner(pay(target, bob, XRP(1)), 0, ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 0);
    }

    // Deleting the target account removes its Tickets, after which a proposal
    // keyed to one of them can never execute. The proposal is owned by the
    // proposer — never by the deleted account, for which it would have been a
    // deletion blocker — so it is cleaned up and the proposer refunded.
    void
    testTargetAccountDeleted(FeatureBitset features)
    {
        testcase("deleting the target account deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        // Expire far enough out that the proposal is still live when the
        // account becomes deletable.
        std::uint32_t const expiration = (env.now() + 3600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

        incLgrSeqForAccDel(env, target);
        env(acctdelete(target, bob), Fee(env.current()->fees().increment));
        env.close();

        BEAST_EXPECT(!env.le(keylet::account(target.id())));
        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // The proposer may be the target itself. Then the ticket bookkeeping and
    // the proposal's reserve release both land on the same account-root SLE
    // within one ticketDelete call, so this pins the aliasing case.
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

        std::uint32_t const ticketSeq = env.seq(alice) + 1;
        env(ticket::create(alice, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, alice, bob, ticketSeq), expiration));
        env.close();

        auto const sle = env.le(keylet::txProposal(alice.id(), ticketSeq));
        BEAST_EXPECT(sle && sle->getAccountID(sfOwner) == alice.id());
        // One Ticket plus the proposal's increments, all against alice.
        BEAST_EXPECT(ownerCount(env, alice) == 1 + proposal::kProposalOwnerCount);

        env(noop(alice), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(alice.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A TransactionProposal blocks its Owner's account deletion (XLS-0103
    // §4.6). Beyond the spec requirement, this blocker is what guarantees the
    // AccountDelete ticket sweep never deletes a proposal out of the very
    // owner directory it is iterating: any proposal reached through a swept
    // ticket is necessarily owned by an account other than the one being
    // deleted.
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

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 3600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        incLgrSeqForAccDel(env, alice);
        env(acctdelete(alice, bob), Fee(env.current()->fees().increment), Ter(tecHAS_OBLIGATIONS));
        env.close();

        BEAST_EXPECT(env.le(keylet::account(alice.id())));
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
    }

    // A tfAllOrNothing Batch whose inner transaction spends the proposal's
    // ticket but whose sibling fails is discarded entirely: the ticket
    // survives, so the proposal — still executable — must survive with it.
    void
    testBatchDiscardKeepsProposal(FeatureBitset features)
    {
        testcase("discarded batch keeps proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        // Inner #1 (ticket-based) would succeed; inner #2 fails, so
        // tfAllOrNothing discards every inner change, ticket included.
        auto const seq = env.seq(target);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(target, seq, batchFee, tfAllOrNothing),
            batch::Inner(pay(target, bob, XRP(1)), 0, ticketSeq),
            batch::Inner(pay(target, bob, XRP(1'000'000)), seq + 1));
        env.close();

        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        // The ticket survived the discard.
        BEAST_EXPECT(ownerCount(env, target) == 1);

        // The resurrected ticket still triggers cleanup when it is finally
        // consumed for real.
        env(noop(target), ticket::Use(ticketSeq));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // Under tfIndependent a failed inner transaction is still applied as a
    // claimed-fee result, durably consuming its ticket — so the proposal
    // must be deleted even though the inner transaction failed.
    void
    testBatchInnerTecStillDeletes(FeatureBitset features)
    {
        testcase("failed independent inner transaction still deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        auto const seq = env.seq(target);
        auto const batchFee = batch::calcBatchFee(env, 0, 2);
        env(batch::outer(target, seq, batchFee, tfIndependent),
            batch::Inner(pay(target, bob, XRP(1'000'000)), 0, ticketSeq),
            batch::Inner(pay(target, bob, XRP(1)), seq + 1));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 0);
    }

    // tfUntilFailure applies inner transactions up to the first failure:
    // tickets consumed before the break delete their proposals; tickets never
    // reached keep theirs.
    void
    testBatchPartialConsumption(FeatureBitset features)
    {
        testcase("partially applied batch deletes only consumed tickets' proposals");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 2));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq + 1), expiration));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 2 * proposal::kProposalOwnerCount);

        // Inner #1 consumes the first ticket, inner #2 fails and stops the
        // batch, inner #3 (second ticket) is never attempted.
        auto const seq = env.seq(target);
        auto const batchFee = batch::calcBatchFee(env, 0, 3);
        env(batch::outer(target, seq, batchFee, tfUntilFailure),
            batch::Inner(pay(target, bob, XRP(1)), 0, ticketSeq),
            batch::Inner(pay(target, bob, XRP(1'000'000)), seq + 1),
            batch::Inner(pay(target, bob, XRP(1)), 0, ticketSeq + 1));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq + 1)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        // The unreached ticket survives.
        BEAST_EXPECT(ownerCount(env, target) == 1);
    }

    // A delegate (sfDelegate) submits and pays for the transaction, but the
    // ticket consumed belongs to the delegating account — the hook must key
    // on the ticket owner, not on the submitter or fee payer.
    void
    testDelegatedTicketSpend(FeatureBitset features)
    {
        testcase("delegated transaction ticket spend deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const marty{"marty"};  // target's delegate
        env.fund(XRP(10000), alice, target, bob, marty);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env(delegate::set(target, marty, {"Payment"}));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        env(pay(target, bob, XRP(1)), delegate::As(marty), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        // The target's one remaining object is the Delegate entry itself.
        BEAST_EXPECT(ownerCount(env, target) == 1);
    }

    // A fee-sponsored transaction charges its fee to the sponsor while the
    // account's own ticket is consumed — the hook must follow the ticket.
    void
    testFeeSponsoredTicketSpend(FeatureBitset features)
    {
        testcase("fee-sponsored transaction ticket spend deletes proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const payer{"sponsor"};
        env.fund(XRP(10000), alice, target, bob, payer);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        env(pay(target, bob, XRP(1)),
            ticket::Use(ticketSeq),
            Fee(XRP(1)),
            sponsor::As(payer, spfSponsorFee),
            Sig(sfSponsorSignature, payer));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 0);
    }

    // A TicketCreate submitted using a ticket consumes it like any other
    // transaction, then mints fresh tickets; only the consumed ticket's
    // proposal goes away.
    void
    testTicketCreateViaTicket(FeatureBitset features)
    {
        testcase("ticket-funded TicketCreate deletes consumed ticket's proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        env(ticket::create(target, 1), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        // The freshly minted ticket is the target's only object.
        BEAST_EXPECT(ownerCount(env, target) == 1);
    }

    // A proposal that is already terminal (Expiration passed) is still
    // deleted when its ticket is consumed: the hook deliberately has no
    // terminal-state check, and XLS-0103 §4.5 makes automatic cleanup
    // unconditional.
    void
    testExpiredProposalStillDeleted(FeatureBitset features)
    {
        testcase("expired proposal still deleted on ticket spend");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 60s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        // Sail past the expiration: the proposal is terminal but stays in
        // ledger state (nothing cleans up on expiry by itself).
        env.close(env.now() + 120s);
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), ticketSeq)));

        env(noop(target), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // With more than 32 owned objects the proposer's directory spans multiple
    // pages, so deletion must honor the sfOwnerNode page hint stored at
    // creation rather than assume the root page.
    void
    testMultiPageOwnerDirectory(FeatureBitset features)
    {
        testcase("proposal on a non-root owner directory page");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        // Fill alice's owner directory past one page (32 entries) before the
        // proposal is created.
        env(ticket::create(alice, 40));
        env.close();

        std::uint32_t const expiration = (env.now() + 600s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        auto const sle = env.le(keylet::txProposal(target.id(), ticketSeq));
        BEAST_EXPECT(sle);
        if (!sle)
            return;
        // The shape under test: the proposal must have landed off the root
        // directory page.
        BEAST_EXPECT((*sle)[sfOwnerNode] != 0);
        BEAST_EXPECT(ownerCount(env, alice) == 40 + proposal::kProposalOwnerCount);

        env(noop(target), ticket::Use(ticketSeq));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), ticketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 40);
    }

    // No sponsored-proposal case: a TransactionProposal cannot carry a
    // reserve sponsor today — ttTRANSACTION_PROPOSAL_CREATE is not in the v1
    // reserve-sponsorship allow-list (isReserveSponsorAllowed) and
    // ltTRANSACTION_PROPOSAL is not transferable to a sponsor
    // (isLedgerEntrySupportedBySponsorship). deleteProposal releases the
    // reserve through decreaseOwnerCountForObject, so if those lists ever
    // grow, deletion follows the sfSponsor recorded on the entry.

    void
    run() override
    {
        using namespace jtx;
        testTicketSpendDeletesProposal(testableAmendments());
        testOtherAccountsTicket(testableAmendments());
        testTecResultStillDeletes(testableAmendments());
        testBatchProposalReserveRefund(testableAmendments());
        testInnerBatchTicketSpend(testableAmendments());
        testTargetAccountDeleted(testableAmendments());
        testProposerIsTarget(testableAmendments());
        testProposalBlocksOwnerAccountDelete(testableAmendments());
        testBatchDiscardKeepsProposal(testableAmendments());
        testBatchInnerTecStillDeletes(testableAmendments());
        testBatchPartialConsumption(testableAmendments());
        testDelegatedTicketSpend(testableAmendments());
        testFeeSponsoredTicketSpend(testableAmendments());
        testTicketCreateViaTicket(testableAmendments());
        testExpiredProposalStillDeleted(testableAmendments());
        testMultiPageOwnerDirectory(testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalAutoDelete, app, xrpl);

}  // namespace xrpl::test
