#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace xrpl::test {

struct TransactionProposalCancel_test : public beast::unit_test::Suite
{
    // Create one ticket for the target and a live proposal keyed to it,
    // returning the proposal's ID. The proposal is the proposer's and its
    // proposed transaction pays dest.
    uint256
    makeProposal(
        jtx::Env& env,
        jtx::Account const& proposer,
        jtx::Account const& target,
        jtx::Account const& dest,
        std::optional<json::Value> payloadOverride = std::nullopt)
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        json::Value const payload = payloadOverride
            ? *payloadOverride
            : proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq);

        if (proposer.id() != target.id() && !env.le(keylet::signerList(target.id())))
            proposal::authorizeProposer(env, target, proposer);

        env(proposal::create(proposer, payload, proposal::expiration(env, 1000s)));
        env.close();

        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
        return proposalID;
    }

    // Nothing about the transaction is available before the amendment is
    // active.
    void
    testDisabled(FeatureBitset features)
    {
        testcase("amendment disabled");

        using namespace jtx;

        Env env{*this, features - featureCosign};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        env(proposal::cancel(alice, uint256{1}), Ter(temDISABLED));
        env.close();
    }

    // Static rejections: a zero ProposalID identifies nothing, and the
    // transaction defines no flags of its own.
    void
    testMalformed(FeatureBitset features)
    {
        testcase("malformed transaction");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        env(proposal::cancel(alice, uint256{}), Ter(temMALFORMED));
        env.close();

        env(proposal::cancel(alice, uint256{1}), Txflags(0x00010000), Ter(temINVALID_FLAG));
        env.close();
    }

    // The named proposal must exist as a TransactionProposal entry:
    // (1) an ID that matches no object
    // (2) an entry of a different type
    // (3) An previously cancelled ProposedID
    // All the above cases will result in tecNO_ENTRY
    void
    testNoEntry(FeatureBitset features)
    {
        testcase("proposal does not exist");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        env.fund(XRP(10000), alice, target);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        // No such entry at all.
        env(proposal::cancel(alice, uint256{1}), Ter(tecNO_ENTRY));
        env.close();

        // The ID of a live ledger entry of a different type (the ticket the
        // proposal below will consume) does not name a proposal.
        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::cancel(
                alice, keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq)).key),
            Ter(tecNO_ENTRY));
        env.close();

        // A proposal that was already cancelled is gone: a second cancel —
        // even by its Owner — finds nothing.
        env(proposal::create(
            alice,
            proposal::unsignedPayload(env, pay(target, alice, XRP(1)), ticketSeq),
            proposal::expiration(env, 1000s)));
        env.close();
        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;

        env(proposal::cancel(alice, proposalID));
        env.close();
        env(proposal::cancel(alice, proposalID), Ter(tecNO_ENTRY));
        env.close();
    }

    // The Owner may cancel a live proposal. Cancellation deletes the entry,
    // releases the Owner's reserve, and frees the (target, ticket) slot — but
    // leaves the target's Ticket itself intact and usable.
    void
    testOwnerCancel(FeatureBitset features)
    {
        testcase("owner cancels live proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        uint256 const proposalID = makeProposal(env, alice, target, bob);
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

        env(proposal::cancel(alice, proposalID));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // The ticket the proposed transaction would have consumed still
        // belongs to the target.
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, target) == 2);

        // The (target, ticket) slot is free again: a new proposal for the
        // same pair is not a duplicate.
        env(proposal::create(
            alice,
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq),
            proposal::expiration(env, 1000s)));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

        // And once cancelled again, the target may spend the ticket on
        // something else entirely (which is also what positively revokes a
        // proposal: cancellation alone does not invalidate signatures an
        // observer may have copied, XLS-0103 §13.4).
        env(proposal::cancel(alice, proposalID));
        env.close();
        env(noop(target), ticket::Use(ticketSeq));
        env.close();
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
    }

    // The target account may refuse any proposal made for it, at any point in
    // its lifecycle, without owning the object: anyone can create a proposal
    // against any account and tie up one of its tickets (XLS-0103 §7.2).
    void
    testTargetCancel(FeatureBitset features)
    {
        testcase("target cancels live proposal");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        uint256 const proposalID = makeProposal(env, alice, target, bob);

        env(proposal::cancel(target, proposalID));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        // The reserve was the proposer's, and cancellation returns it to the
        // proposer even when someone else cancels.
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        // The target account still holds the SignerList plus one unused ticket.
        BEAST_EXPECT(ownerCount(env, target) == 2);
    }

    // When the proposed transaction is initiated by a Delegate, that delegate
    // is a target account too (XLS-0103 §7.2): the proposal seeks its
    // authorization, so it may refuse.
    void
    testDelegateCancel(FeatureBitset features)
    {
        testcase("proposed transaction's delegate cancels");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const carol{"carol"};  // the proposed transaction's Delegate
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, carol, bob);
        env.close();

        auto delegatedPayload = [&](std::uint32_t ticketSeq) {
            json::Value payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            payload[sfDelegate.getJsonName()] = carol.human();
            return payload;
        };

        // The Delegate named in the payload may cancel.
        {
            std::uint32_t const ticketSeq = env.seq(target) + 1;
            uint256 const proposalID =
                makeProposal(env, alice, target, bob, delegatedPayload(ticketSeq));

            env(proposal::cancel(carol, proposalID));
            env.close();

            BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // A Delegate widens, not replaces, the target: the proposed
        // transaction's Account may still cancel.
        {
            std::uint32_t const ticketSeq = env.seq(target) + 1;
            uint256 const proposalID =
                makeProposal(env, alice, target, bob, delegatedPayload(ticketSeq));

            env(proposal::cancel(target, proposalID));
            env.close();

            BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
    }

    // While the proposal is live, nobody else may cancel it — not even the
    // destination of the proposed payment (unlike a Check, whose destination
    // may cancel it).
    void
    testThirdPartyNoPermission(FeatureBitset features)
    {
        testcase("third party cannot cancel live proposal");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};  // destination of the proposed payment
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        uint256 const proposalID = makeProposal(env, alice, target, bob);

        env(proposal::cancel(bob, proposalID), Ter(tecNO_PERMISSION));
        env.close();

        // The proposal survives and the Owner's reserve is still held.
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
    }

    // Once the proposal's Expiration has passed it is terminal: it can never
    // complete, so anyone may delete it and release the Owner's reserve.
    void
    testExpiredPermissionlessCancel(FeatureBitset features)
    {
        testcase("anyone cancels expired proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // A LastLedgerSequence far beyond this test keeps that arm of the
        // terminal rule inert: only the Expiration arm may fire here.
        json::Value payload = proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
        payload[sfLastLedgerSequence.getJsonName()] = env.current()->seq() + 1000;

        env(proposal::create(alice, payload, proposal::expiration(env, 100s)));
        env.close();
        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;

        // Still live: a third party may not touch it.
        env(proposal::cancel(bob, proposalID), Ter(tecNO_PERMISSION));
        env.close();

        // Jump past the expiration. Whether the proposal is terminal is
        // judged against the parent ledger's close time, the same rule
        // Expiration follows everywhere.
        env.close(env.now() + 200s);

        env(proposal::cancel(bob, proposalID));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        // The target's unspent ticket survives its proposal.
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
    }

    // A proposal whose proposed transaction has run out of ledgers — the
    // ledger has reached its LastLedgerSequence — is just as terminal as an
    // expired one. The boundary matches the dead-on-arrival rule at
    // creation: a proposal with LastLedgerSequence at or below the current
    // ledger sequence cannot be created, and an existing one is terminal.
    void
    testLastLedgerSequenceTerminal(FeatureBitset features)
    {
        testcase("anyone cancels proposal past its LastLedgerSequence");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        std::uint32_t const lastLedgerSeq = env.current()->seq() + 2;
        json::Value payload = proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
        payload[sfLastLedgerSequence.getJsonName()] = lastLedgerSeq;

        env(proposal::create(alice, payload, proposal::expiration(env, 1000s)));
        env.close();
        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;

        // Close until the open ledger sits just before the bound: the
        // proposal is still live there.
        while (env.current()->seq() < lastLedgerSeq - 1)
            env.close();
        BEAST_EXPECT(env.current()->seq() == lastLedgerSeq - 1);
        env(proposal::cancel(bob, proposalID), Ter(tecNO_PERMISSION));
        env.close();

        // In the ledger whose sequence equals the bound, the proposal is
        // terminal.
        BEAST_EXPECT(env.current()->seq() == lastLedgerSeq);
        env(proposal::cancel(bob, proposalID));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // The Expiration boundary: a proposal is terminal already in the first
    // ledger whose parent close time equals its Expiration (the >= rule
    // Expiration follows everywhere), not merely after it.
    void
    testExpirationBoundary(FeatureBitset features)
    {
        testcase("expiration boundary is inclusive");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // Recorded close times land on multiples of the close time
        // resolution, so pick the expiration on that grid and later aim a
        // close at it exactly. Env::close(tp) sets the clock to
        // tp + resolution - 1s and the accept path rounds to the nearest
        // multiple, so a target of E - resolution + 1s closes at exactly E.
        auto const resolution = env.closed()->header().closeTimeResolution;
        NetClock::time_point const lastClose = env.current()->parentCloseTime();
        BEAST_EXPECT((lastClose.time_since_epoch() % resolution).count() == 0);

        NetClock::time_point const expiryTime = lastClose + 20 * resolution;
        std::uint32_t const expiration = expiryTime.time_since_epoch().count();

        env(proposal::create(
            alice,
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq),
            expiration));
        env.close();
        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;

        // One grid step before the expiration the proposal is still live,
        // even at the adjacent tick.
        env.close(expiryTime - 2 * resolution + 1s);
        BEAST_EXPECT(env.current()->parentCloseTime() == expiryTime - resolution);
        env(proposal::cancel(bob, proposalID), Ter(tecNO_PERMISSION));

        // The first ledger whose parent close time equals the Expiration is
        // terminal.
        env.close(expiryTime - resolution + 1s);
        BEAST_EXPECT(env.current()->parentCloseTime() == expiryTime);

        env(proposal::cancel(bob, proposalID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A proposal turning terminal only widens who may cancel: the Owner and
    // the target can always do it, before and after.
    void
    testOwnerCancelTerminal(FeatureBitset features)
    {
        testcase("owner cancels expired proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        env(proposal::create(
            alice,
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();
        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;

        env.close(env.now() + 200s);

        env(proposal::cancel(alice, proposalID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A proposed Batch reserves more increments than an ordinary proposal;
    // cancellation must release exactly that larger amount.
    void
    testBatchProposalCancel(FeatureBitset features)
    {
        testcase("cancel batch proposal releases its larger reserve");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        auto const seq = env.seq(target);
        json::Value const payload = proposal::unsignedBatch(
            env,
            target,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), seq),
             proposal::innerTx(pay(target, bob, XRP(1)), seq + 1)});

        env(proposal::create(alice, payload, proposal::expiration(env, 1000s)));
        env.close();

        uint256 const proposalID = keylet::txProposal(target.id(), ticketSeq).key;
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);

        env(proposal::cancel(target, proposalID));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // The Cancel transaction itself goes through the ordinary transaction
    // machinery: it may spend a ticket of its submitter, and a target whose
    // authority lives in a SignerList may cancel by multi-signing.
    void
    testCancelSubmissionForms(FeatureBitset features)
    {
        testcase("ticketed and multisigned cancels");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const daria{"daria"};
        env.fund(XRP(10000), alice, target, bob, carol, daria);
        env.close();

        // The target's authority comes from a SignerList. Alice is on it so
        // it may create the proposal this test then cancels.
        env(signers(target, 2, {{alice, 1}, {carol, 1}, {daria, 1}}));
        env.close();

        // Cancel spending a ticket of the canceller.
        {
            uint256 const proposalID = makeProposal(env, alice, target, bob);

            std::uint32_t const cancelTicket = proposal::createTicket(env, alice);

            env(proposal::cancel(alice, proposalID), ticket::Use(cancelTicket));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        }

        // Cancel multi-signed for the target by its signer quorum.
        {
            uint256 const proposalID = makeProposal(env, alice, target, bob);

            env(proposal::cancel(target, proposalID),
                Msig(carol, daria),
                Fee(env.current()->fees().base * 3));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        }
    }

    // A Cancel works as an inner Batch transaction, where each inner runs its
    // own preclaim at inner-apply time against the accumulated batch view.
    void
    testCancelInsideBatch(FeatureBitset features)
    {
        testcase("cancel as an inner batch transaction");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        // A multi-account batch: bob submits the outer Batch and the target
        // authorizes its inner Cancel through a BatchSigners entry.
        {
            uint256 const proposalID = makeProposal(env, alice, target, bob);

            auto const seq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            env(batch::outer(bob, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(bob, target, XRP(1)), seq + 1),
                batch::Inner(proposal::cancel(target, proposalID), env.seq(target)),
                batch::Sig(target));
            env.close();

            BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // Two inner Cancels of the same proposal: the second finds nothing at
        // its own preclaim. Under tfAllOrNothing that failure discards the
        // whole batch, so the proposal survives with the reserve still held.
        {
            uint256 const proposalID = makeProposal(env, alice, target, bob);

            auto const seq = env.seq(target);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(target, seq, batchFee, tfAllOrNothing),
                batch::Inner(proposal::cancel(target, proposalID), seq + 1),
                batch::Inner(proposal::cancel(target, proposalID), seq + 2));
            env.close();

            BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

            // Under tfIndependent the first inner's deletion stands and only
            // the second fails.
            auto const seq2 = env.seq(target);
            env(batch::outer(target, seq2, batchFee, tfIndependent),
                batch::Inner(proposal::cancel(target, proposalID), seq2 + 1),
                batch::Inner(proposal::cancel(target, proposalID), seq2 + 2));
            env.close();

            BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);

            // Pin the two inner results from the closed ledger: the second
            // Cancel failed at its own preclaim with tecNO_ENTRY. A tef from
            // deeper in the apply pipeline would have kept it out of the
            // ledger entirely.
            json::Value params;
            params[jss::ledger_index] = env.closed()->seq();
            params[jss::transactions] = true;
            params[jss::expand] = true;
            auto const jrr = env.rpc("json", "ledger", to_string(params));
            int success = 0;
            int noEntry = 0;
            for (auto const& txn : jrr[jss::result][jss::ledger][jss::transactions])
            {
                if (txn[jss::TransactionType] == jss::TransactionProposalCancel)
                {
                    auto const result =
                        txn[jss::metaData][sfTransactionResult.getJsonName()].asString();
                    success += (result == "tesSUCCESS") ? 1 : 0;
                    noEntry += (result == "tecNO_ENTRY") ? 1 : 0;
                }
            }
            BEAST_EXPECT(success == 1);
            BEAST_EXPECT(noEntry == 1);
        }
    }

    // The Cancel transaction itself may have its fee sponsored, but it is not
    // on the reserve-sponsorship allow-list (it creates nothing to reserve).
    void
    testSponsoredCancel(FeatureBitset features)
    {
        testcase("sponsored cancels");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const payer{"payer"};
        env.fund(XRP(10000), alice, target, bob, payer);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        uint256 const proposalID = makeProposal(env, alice, target, bob);

        // Reserve sponsorship is rejected for this transaction type.
        env(proposal::cancel(alice, proposalID),
            Fee(XRP(1)),
            sponsor::As(payer, spfSponsorReserve),
            Sig(sfSponsorSignature, payer),
            Ter(temINVALID_FLAG));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));

        // Fee sponsorship works: the sponsor pays, the canceller does not.
        auto const before = env.balance(alice);
        auto const payerBefore = env.balance(payer);
        env(proposal::cancel(alice, proposalID),
            Fee(XRP(1)),
            sponsor::As(payer, spfSponsorFee),
            Sig(sfSponsorSignature, payer));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(env.balance(alice) == before);
        BEAST_EXPECT(env.balance(payer) == payerBefore - XRP(1));
    }

    // The Cancel transaction is not delegable: a Cancel carrying sfDelegate is
    // rejected at preflight, and the permission cannot even be granted.
    void
    testDelegatedCancelRejected(FeatureBitset features)
    {
        testcase("cancel cannot be delegated");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, target, carol);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        uint256 const proposalID = makeProposal(env, alice, target, alice);

        env(delegate::set(target, carol, {"TransactionProposalCancel"}), Ter(temMALFORMED));
        env.close();

        env(proposal::cancel(target, proposalID), delegate::As(carol), Ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
    }

    // A required auxiliary co-signer of the proposed transaction — here a
    // LoanSet's explicit Counterparty — has its authorization sought by the
    // proposal, yet §7.2 grants it no cancellation rights: it is neither the
    // Owner nor the target.
    void
    testCounterpartyCannotCancel(FeatureBitset features)
    {
        testcase("proposed transaction's counterparty cannot cancel");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const borrower{"borrower"};
        Account const carol{"carol"};  // the LoanSet's Counterparty
        env.fund(XRP(10000), alice, borrower, carol);
        env.close();
        proposal::authorizeProposer(env, borrower, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);

        json::Value payload =
            proposal::unsignedPayload(env, loan::set(borrower, uint256{1}, 1'000), ticketSeq);
        payload[sfCounterparty.getJsonName()] = carol.human();

        env(proposal::create(alice, payload, proposal::expiration(env, 1000s)));
        env.close();
        uint256 const proposalID = keylet::txProposal(borrower.id(), ticketSeq).key;

        env(proposal::cancel(carol, proposalID), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));

        // The target itself may, of course initiate the Cancel transaction.
        env(proposal::cancel(borrower, proposalID));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(proposalID)));
    }

    // Cancel may not spend the ticket the proposal is keyed to. The
    // reservation is only for the proposed transaction (XLS-0103 §4.2.1);
    // a target that wants to revoke both the proposal and the ticket must
    // cancel first, then spend the ticket separately.
    void
    testCancelCannotSpendReservedTicket(FeatureBitset features)
    {
        testcase("cancel cannot consume the reserved ticket");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        // Standalone: the Cancel is held as a retry and neither object is
        // touched.
        {
            std::uint32_t const ticketSeq = env.seq(target) + 1;
            uint256 const proposalID = makeProposal(env, alice, target, bob);

            env(proposal::cancel(target, proposalID),
                ticket::Use(ticketSeq),
                Ter(terTICKET_RESERVED));
            env.close();

            BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
            BEAST_EXPECT(ownerCount(env, target) == 2);
        }

        // The same attempt as an inner Batch transaction: tfAllOrNothing
        // discards the whole batch, so the sibling payment does not land.
        {
            std::uint32_t const ticketSeq = env.seq(target) + 1;
            uint256 const proposalID = makeProposal(env, alice, target, bob);

            auto const seq = env.seq(target);
            auto const bobBefore = env.balance(bob);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(target, seq, batchFee, tfAllOrNothing),
                batch::Inner(proposal::cancel(target, proposalID), 0, ticketSeq),
                batch::Inner(pay(target, bob, XRP(1)), seq + 1));
            env.close();

            BEAST_EXPECT(env.le(keylet::txProposal(proposalID)));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(env.balance(bob) == bobBefore);
            BEAST_EXPECT(ownerCount(env, alice) == 2 * proposal::kProposalOwnerCount);
            BEAST_EXPECT(ownerCount(env, target) == 3);
        }
    }

    // A proposal must not nest proposal machinery: proposing a
    // TransactionProposalCancel is rejected outright. A signer-quorum target
    // that wants to refuse a proposal multi-signs a Cancel directly instead.
    void
    testCannotProposeCancel(FeatureBitset features)
    {
        testcase("a cancel cannot itself be proposed");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        env.fund(XRP(10000), alice, target);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        std::uint32_t const expiration = proposal::expiration(env, 1000s);

        // Directly proposed.
        {
            json::Value const payload =
                proposal::unsignedPayload(env, proposal::cancel(target, uint256{1}), ticketSeq);

            env(proposal::create(alice, payload, expiration), Ter(temINVALID));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        }

        // Wrapped inside a proposed Batch: nesting must not be evadable by
        // one level of indirection (On-Chain Cosigner spec §5.3.1). Each
        // wrapped transaction is well-formed for its type, so the rejection
        // can only come from the nesting rule itself. The sibling inner uses
        // a sequence distinct from the wrapped transaction's, so the Batch is
        // not rejected as redundant either.
        auto rejectWrapped = [&](json::Value const& wrapped, TER expected) {
            json::Value const payload = proposal::unsignedBatch(
                env,
                target,
                ticketSeq,
                tfAllOrNothing,
                {proposal::innerTx(pay(target, alice, XRP(1)), env.seq(target) + 1), wrapped});

            env(proposal::create(alice, payload, expiration), Ter(expected));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        };

        // A well-formed inner TransactionProposalCancel.
        rejectWrapped(
            proposal::innerTx(proposal::cancel(target, uint256{1}), env.seq(target)), temINVALID);

        // A well-formed inner TransactionProposalCreate.
        rejectWrapped(
            proposal::innerTx(
                proposal::create(
                    target,
                    proposal::unsignedPayload(env, pay(target, alice, XRP(1)), 1),
                    expiration),
                env.seq(target)),
            temINVALID);

        // An inner Batch is rejected at STTx construction (a Batch cannot
        // contain a Batch), so Create surfaces temMALFORMED rather than the
        // nesting rule's temINVALID.
        {
            json::Value nested;
            nested[jss::TransactionType] = jss::Batch;
            nested[jss::Account] = target.human();
            nested[jss::Flags] = tfAllOrNothing;
            nested[jss::RawTransactions][0u][jss::RawTransaction] =
                proposal::innerTx(pay(target, alice, XRP(1)), env.seq(target));
            nested[jss::RawTransactions][1u][jss::RawTransaction] =
                proposal::innerTx(proposal::cancel(target, uint256{1}), env.seq(target));

            rejectWrapped(proposal::innerTx(std::move(nested), env.seq(target)), temMALFORMED);
        }
    }

    void
    run() override
    {
        using namespace jtx;

        FeatureBitset const all{testableAmendments()};

        // Preflight
        testDisabled(all);
        testMalformed(all);
        testDelegatedCancelRejected(all);
        testCannotProposeCancel(all);

        // Preclaim
        testNoEntry(all);
        testThirdPartyNoPermission(all);
        testExpiredPermissionlessCancel(all);
        testExpirationBoundary(all);
        testLastLedgerSequenceTerminal(all);
        testCounterpartyCannotCancel(all);
        testCancelCannotSpendReservedTicket(all);

        // Apply
        testOwnerCancel(all);
        testTargetCancel(all);
        testDelegateCancel(all);
        testOwnerCancelTerminal(all);
        testBatchProposalCancel(all);
        testCancelSubmissionForms(all);
        testCancelInsideBatch(all);
        testSponsoredCancel(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalCancel, app, xrpl);

}  // namespace xrpl::test
