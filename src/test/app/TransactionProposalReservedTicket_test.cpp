#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpld/app/misc/TxQ.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace xrpl::test {

// While a TransactionProposal exists, the Ticket it is keyed to is reserved
// (On-Chain Cosigner spec §4.2.1): a transaction may consume it only if its payload matches
// the stored ProposedTransaction, so unrelated target-account activity cannot
// invalidate the proposal while signatures are being collected. When the
// matching transaction does consume the ticket, the ledger deletes the
// now-stale proposal and releases the reserve it holds against its Owner
// (On-Chain Cosigner spec §4.5).
struct TransactionProposalReservedTicket_test : public beast::unit_test::Suite
{
    // Create a live proposal owned by proposer holding the given payload.
    static void
    makeProposal(jtx::Env& env, jtx::Account const& proposer, json::Value const& payload)
    {
        using namespace jtx;
        using namespace std::chrono_literals;

        Account const target = env.lookup(payload[jss::Account].asString());
        if (proposer.id() != target.id() && !env.le(keylet::signerList(target.id())))
            proposal::authorizeProposer(env, target, proposer);

        env(proposal::create(proposer, payload, proposal::expiration(env, 1000s)));
        env.close();
    }

    // Rebuild a transaction from a copy of it whose fields have been edited.
    // Editing an STTx in place would leave getTransactionID — and the
    // signature verdict the HashRouter caches under it — describing the
    // transaction before the edit. Going through the bytes rather than
    // STTx{STObject&&} is what makes this work on a copy of an already
    // templated transaction: applyTemplate refuses a defaulted field that is
    // explicitly present, which such a copy carries, and serializing omits it.
    static std::shared_ptr<STTx const>
    rebuilt(STObject const& tx)
    {
        Serializer s;
        tx.add(s);
        return std::make_shared<STTx const>(SerialIter{s.slice()});
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
        BEAST_EXPECT(ownerCount(env, target) == 3);

        // A different transaction type.
        env(noop(target), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        // The proposed transaction with one payload field off: the amount,
        // the destination, or the fee. Signature fields are the only
        // latitude a submitter has (On-Chain Cosigner spec §4.2.1).
        env(pay(target, bob, XRP(2)), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        env(pay(target, alice, XRP(1)), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
        env(pay(target, bob, XRP(1)),
            ticket::Use(ticketSeq),
            Fee(env.current()->fees().base * 2),
            Ter(terTICKET_RESERVED));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 3);

        // The reservation binds one ticket, not the account: its sequence
        // and its other tickets spend freely, and the proposal survives.
        env(noop(target));
        env(noop(target), ticket::Use(ticketSeq + 1));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, target) == 2);
    }

    // The proposal's own transaction consumes the reserved ticket through
    // the ordinary submission path (On-Chain Cosigner spec §6.5). How it is signed is
    // irrelevant to the match — signature fields are excluded — and applying
    // it auto-deletes the proposal and refunds the Owner (On-Chain Cosigner spec §4.5).
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
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, target) == 1);
        }

        // Multi-signed under the target's SignerList: the Signers array the
        // submission carries is a signature field too. The payload's Fee was
        // fixed at creation, so a proposer expecting multi-signed submission
        // must budget for the signatures then (On-Chain Cosigner spec §4.2.1).
        {
            env(signers(target, 2, {{alice, 1}, {bob, 1}, {carol, 1}}));
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
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
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
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
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
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, target) == 1);
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
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(targetTicketSeq))));
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
    // reserve a Batch proposal holds (On-Chain Cosigner spec §4.4).
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
        proposal::authorizeProposer(env, target, alice);

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
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // An inner Batch transaction validates its ticket through the same path
    // as a standalone one, so a reserved ticket blocks it too — an inner
    // transaction carries tfInnerBatchTxn and a zero fee, so it can never
    // match a stored payload, which may not carry that flag (On-Chain Cosigner spec
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
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

        // The standalone matching payment still goes through.
        env(pay(target, bob, XRP(1)), ticket::Use(ticketSeq));
        env.close();
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A proposed multi-account Batch is stored with no BatchSigners: the
    // participants' signatures are collected afterwards, and the submission
    // that completes it carries them. BatchSigners is one of the containers
    // payloadMatches excludes, which is exactly what lets the completed Batch
    // still match what was stored.
    void
    testMultiAccountBatchCompletes(FeatureBitset features)
    {
        testcase("proposed multi-account Batch completes with BatchSigners");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, target, bob, carol);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // One inner from each of two accounts, so the Batch is unsubmittable
        // without bob's BatchSigners entry. The second inner moves funds bob
        // holds to a third account, so each inner is separately observable.
        json::Value const payload = proposal::unsignedBatch(
            env,
            target,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
             proposal::innerTx(pay(bob, carol, XRP(2)), env.seq(bob))});
        makeProposal(env, alice, payload);

        auto const sle = proposal::entry(env, target, ticketSeq);
        BEAST_EXPECT(
            sle && !sle->getFieldObject(sfProposedTransaction).isFieldPresent(sfBatchSigners));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);

        // Submitted exactly as stored, the Batch cannot even preflight: a
        // multi-account Batch demands an entry per participant. So excluding
        // BatchSigners from the match is load-bearing rather than incidental —
        // without it this proposal could never be completed at all.
        env(payload, Ter(temBAD_SIGNER));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));

        auto const bobBefore = env.balance(bob);
        auto const carolBefore = env.balance(carol);
        env(payload, batch::Sig(bob));
        env.close();

        // Both inners applied: bob took XRP(1) from the target and paid
        // XRP(2) out, and inner transactions carry no fee of their own.
        BEAST_EXPECT(env.balance(carol) == carolBefore + XRP(2));
        BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1) - XRP(2));
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A proposed LoanSet stores no CounterpartySignature — that is collected
    // afterwards too — and the completed submission carrying one still
    // matches. The payload's Fee is fixed at creation, so it has to budget for
    // that signature up front: LoanSet charges an extra base fee per
    // counterparty signature, and nothing can raise the fee later.
    void
    testCounterpartySignatureMatches(FeatureBitset features)
    {
        testcase("proposed LoanSet completes with a CounterpartySignature");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};  // the borrower
        Account const lender{"lender"};  // the LoanSet's Counterparty
        env.fund(XRP(10000), alice, target, lender);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        json::Value tx = loan::set(target, uint256{1}, 1'000);
        tx[sfCounterparty.getJsonName()] = lender.human();
        tx[jss::Fee] = std::to_string((env.current()->fees().base * 2).drops());
        json::Value const payload = proposal::unsignedPayload(env, tx, ticketSeq);
        makeProposal(env, alice, payload);

        // The named LoanBroker does not exist, so this fails at preclaim —
        // which is the point: getting as far as preclaim proves the added
        // CounterpartySignature did not break the payload match, and a tec
        // consumes the ticket and cleans the proposal up all the same.
        env(payload, Sig(sfCounterpartySignature, lender), Ter(tecNO_ENTRY));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // The last of the co-signature containers: a payload may name a fee
    // sponsor at creation and collect that sponsor's signature later. Sponsor
    // and SponsorFlags are ordinary payload fields, fixed from the start;
    // only the SponsorSignature itself arrives at submission.
    void
    testSponsorSignatureMatches(FeatureBitset features)
    {
        testcase("proposed payload completes with a SponsorSignature");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const backer{"backer"};  // pays the payload's fee
        env.fund(XRP(10000), alice, target, bob, backer);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        json::Value tx = pay(target, bob, XRP(1));
        tx[sfSponsor.getJsonName()] = backer.human();
        tx[sfSponsorFlags.getJsonName()] = spfSponsorFee;
        json::Value const payload = proposal::unsignedPayload(env, tx, ticketSeq);
        makeProposal(env, alice, payload);

        auto const sle = proposal::entry(env, target, ticketSeq);
        BEAST_EXPECT(
            sle && !sle->getFieldObject(sfProposedTransaction).isFieldPresent(sfSponsorSignature));

        auto const fee = env.current()->fees().base;
        auto const bobBefore = env.balance(bob);
        auto const targetBefore = env.balance(target);
        auto const backerBefore = env.balance(backer);

        env(payload, Sig(sfSponsorSignature, backer));
        env.close();

        // The sponsor covered the fee, so the target parted with the payment
        // amount and nothing beyond it.
        BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
        BEAST_EXPECT(env.balance(target) == targetBefore - XRP(1));
        BEAST_EXPECT(env.balance(backer) == backerBefore - fee);
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A terminal proposal — expired, or past its payload's LastLedgerSequence
    // — still reserves its ticket. canConsumeTicket never consults
    // isTerminal: going terminal widens who may cancel a proposal, but it
    // releases nothing, because the ticket is reserved for as long as the
    // proposal exists (On-Chain Cosigner spec §4.2.1). Freeing the ticket
    // takes a Cancel, which anyone may now send.
    void
    testTerminalStillReservesTicket(FeatureBitset features)
    {
        testcase("a terminal proposal still reserves its ticket");

        using namespace jtx;
        using namespace std::chrono_literals;

        // An unrelated spend stays blocked after the proposal expires.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            // A far-future LastLedgerSequence keeps that arm of the terminal
            // rule inert, so the expiry alone makes the proposal terminal.
            json::Value payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            payload[sfLastLedgerSequence.getJsonName()] = env.current()->seq() + 1000;

            env(proposal::create(alice, payload, proposal::expiration(env, 100s)));
            env.close();

            env.close(env.now() + 200s);

            env(noop(target), ticket::Use(ticketSeq), Ter(terTICKET_RESERVED));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // The same indifference to isTerminal in the other direction: the
        // proposal's own payload still consumes the ticket after the proposal
        // expires. An expired proposal is closed to new signatures, not to
        // the transaction it already holds. A separate environment keeps the
        // held retry from the block above out of this one.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            json::Value const payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);

            env(proposal::create(alice, payload, proposal::expiration(env, 100s)));
            env.close();

            env.close(env.now() + 200s);

            auto const bobBefore = env.balance(bob);
            env(payload);
            env.close();

            BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
    }

    // A reserve-sponsored proposal cleaned up by its own payload consuming the
    // ticket releases the reserve against the sponsor. deleteProposal follows
    // the Sponsor recorded on the entry, so this path and the Cancel path land
    // in the same place.
    void
    testSponsoredReserveAutoDelete(FeatureBitset features)
    {
        testcase("auto-deleting a sponsored proposal releases its sponsor");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const backer{"backer"};  // sponsors alice's proposal reserve
        env.fund(XRP(10000), alice, target, bob, backer);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value const payload =
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);

        env(proposal::create(alice, payload, proposal::expiration(env, 1000s)),
            sponsor::As(backer, spfSponsorReserve),
            Sig(sfSponsorSignature, backer),
            proposal::verify::create());
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(sponsoringOwnerCount(env, backer) == proposal::kProposalOwnerCount);

        env(payload);
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, backer) == 0);
        BEAST_EXPECT(ownerCount(env, backer) == 0);
    }

    // Deleting the target account sweeps its Tickets without consuming them
    // as a sequence proxy, so the reservation does not block it. A proposal
    // keyed to a swept ticket can never execute and is cleaned up with it,
    // refunding the proposer (On-Chain Cosigner spec §4.5).
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

    // The sweep above, with more than one proposal and more than one Owner.
    // Each reserve has to return to the account that put it up, which means the
    // sweep cannot stop at the first proposal it reaches and cannot settle the
    // refunds against a single account. Two Owners also make the directory
    // separation concrete: the sweep walks the target's owner directory while
    // each deletion mutates a different one.
    void
    testTargetDeletionRefundsEachOwner(FeatureBitset features)
    {
        testcase("target deletion refunds each proposal's own owner");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};  // Owner of the proposal on the first ticket
        Account const bob{"bob"};      // Owner of the one on the second
        Account const target{"target"};
        Account const dest{"dest"};
        env.fund(XRP(10000), alice, bob, target, dest);
        env.close();

        // Both proposers on one SignerList. makeProposal's own authorization
        // step adds a list for the first proposer only and then finds one
        // present, so the second proposer would be unauthorized without this.
        env(signers(target, 1, {{alice, 1}, {bob, 1}}));
        env.close();

        std::uint32_t const firstTicket = proposal::createTicket(env, target, 2);
        std::uint32_t const secondTicket = firstTicket + 1;

        makeProposal(
            env, alice, proposal::unsignedPayload(env, pay(target, dest, XRP(1)), firstTicket));
        makeProposal(
            env, bob, proposal::unsignedPayload(env, pay(target, dest, XRP(2)), secondTicket));

        BEAST_EXPECT(proposal::entry(env, target, firstTicket));
        BEAST_EXPECT(proposal::entry(env, target, secondTicket));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, bob) == proposal::kProposalOwnerCount);

        incLgrSeqForAccDel(env, target);
        env(acctdelete(target, dest), Fee(env.current()->fees().increment));
        env.close();

        BEAST_EXPECT(!env.le(keylet::account(target.id())));
        BEAST_EXPECT(!proposal::entry(env, target, firstTicket));
        BEAST_EXPECT(!proposal::entry(env, target, secondTicket));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(firstTicket))));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(secondTicket))));

        // Each Owner got its own reserve back, and only its own.
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
    }

    // A TransactionProposal blocks its Owner's account deletion (On-Chain Cosigner spec
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

        // An account holding a blocker needs to be able to find out why its
        // deletion failed, so account_objects has to agree with the transactor
        // about what counts as one.
        json::Value params;
        params[jss::account] = alice.human();
        params[jss::deletion_blockers_only] = true;
        auto const jrr = env.rpc("json", "account_objects", to_string(params))[jss::result];
        BEAST_EXPECT(jrr[jss::account_objects].size() == 1);
        BEAST_EXPECT(
            jrr[jss::account_objects][0u][sfLedgerEntryType.getJsonName()] ==
            jss::TransactionProposal);
    }

    // A proposal reserves a ticket, not the target's signing authority: the
    // target stays free to rewrite its SignerList while signatures are being
    // collected. Signatures gathered against the old list then no longer
    // authorize the payload, and the payload's Fee was fixed at creation, so
    // it cannot buy more of them. None of these failures claims a fee or
    // applies anything, so the ticket stays reserved and the payload keeps
    // whatever signing routes the rewritten list still leaves it
    // (On-Chain Cosigner spec §4.2.1).
    void
    testSignerListChangedUnderProposal(FeatureBitset features)
    {
        testcase("target's SignerList changes under a stored proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        // A signer dropped from the list can no longer contribute.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(10000), alice, target, bob, carol);
            env.close();

            env(signers(target, 2, {{alice, 1}, {bob, 1}, {carol, 1}}));
            env.close();

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const msigFee = env.current()->fees().base * 3;
            json::Value proposedTx = pay(target, bob, XRP(1));
            proposedTx[jss::Fee] = std::to_string(msigFee.drops());
            json::Value const payload = proposal::unsignedPayload(env, proposedTx, ticketSeq);
            makeProposal(env, alice, payload);

            // The target drops carol after the proposal is stored.
            env(signers(target, 2, {{alice, 1}, {bob, 1}}));
            env.close();

            env(payload, Msig(bob, carol), Ter(tefBAD_SIGNATURE));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

            // Only the dropped signer is gone, not the route: the signers
            // still on the list meet the unchanged quorum, and because two
            // signatures is what the Fee budgeted for, the payload completes.
            env(payload, Msig(alice, bob));
            env.close();

            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // A quorum raised beyond what the fixed Fee can pay for. That closes
        // the multisign route the Fee was budgeted for, but not the proposal:
        // a SignerList does not revoke the master key, and signing singly
        // costs less than the Fee already covers.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(10000), alice, target, bob, carol);
            env.close();

            env(signers(target, 1, {{alice, 1}, {bob, 1}, {carol, 1}}));
            env.close();

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            auto const msigFee = env.current()->fees().base * 2;
            json::Value proposedTx = pay(target, bob, XRP(1));
            proposedTx[jss::Fee] = std::to_string(msigFee.drops());
            json::Value const payload = proposal::unsignedPayload(env, proposedTx, ticketSeq);
            makeProposal(env, alice, payload);

            env(signers(target, 3, {{alice, 1}, {bob, 1}, {carol, 1}}));
            env.close();

            // The single signature the Fee paid for no longer reaches quorum.
            env(payload, Msig(bob), Ter(tefBAD_QUORUM));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));

            // And the three signatures that would reach it cost more than the
            // Fee fixed at creation. They do satisfy the raised quorum, and
            // checkSign runs ahead of checkFee, so nothing is masked here: a
            // three-signer multisign is priced at four base fees against the
            // two the payload fixed, and checkFee is what rejects it.
            env(payload, Msig(alice, bob, carol), Ter(telINSUF_FEE_P));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);

            // What closed is the multisign route, not the proposal. Raising a
            // quorum does not revoke the master key, and one signature is
            // priced at a single base fee, well inside what the Fee covers.
            auto const bobBefore = env.balance(bob);
            env(payload, Sig(target));
            env.close();

            BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
    }

    // The signature-presence checks that TapProposal skips at creation time
    // are not skipped at submission (On-Chain Cosigner spec §5.3.1.2). Each is
    // a tem, so the submission claims no fee: the ticket keeps its
    // reservation and the proposal stays put.
    void
    testDeferredSignatureChecksFireOnSubmission(FeatureBitset features)
    {
        testcase("deferred signature-presence checks fire at submission");

        using namespace jtx;
        using namespace std::chrono_literals;

        // LoanSet's CounterpartySignature. testCounterpartySignatureMatches
        // covers the completed submission; this covers its absence, which is
        // what LoanSet::preflight stops checking under TapProposal.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};  // the borrower
            Account const lender{"lender"};
            env.fund(XRP(10000), alice, target, lender);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            json::Value tx = loan::set(target, uint256{1}, 1'000);
            tx[sfCounterparty.getJsonName()] = lender.human();
            json::Value const payload = proposal::unsignedPayload(env, tx, ticketSeq);
            makeProposal(env, alice, payload);

            env(payload, Ter(temBAD_SIGNER));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // An account-level SponsorshipTransfer's SponsorSignature. Only the
        // account-level form defers this: it needs no ObjectID, which is what
        // distinguishes it from object-level sponsorship.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const backer{"backer"};
            env.fund(XRP(10000), alice, target, backer);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            json::Value tx = sponsor::transfer(target, tfSponsorshipCreate);
            tx[sfSponsor.getJsonName()] = backer.human();
            tx[sfSponsorFlags.getJsonName()] = spfSponsorReserve;
            json::Value const payload = proposal::unsignedPayload(env, tx, ticketSeq);
            makeProposal(env, alice, payload);

            env(payload, Ter(temMALFORMED));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // Reassignment is the other way into that check: creating and
        // reassigning are gated by one account-level condition, so this pins
        // the flags the check answers for rather than a second code path.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const backer{"backer"};
            env.fund(XRP(10000), alice, target, backer);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            json::Value tx = sponsor::transfer(target, tfSponsorshipReassign);
            tx[sfSponsor.getJsonName()] = backer.human();
            tx[sfSponsorFlags.getJsonName()] = spfSponsorReserve;
            json::Value const payload = proposal::unsignedPayload(env, tx, ticketSeq);
            makeProposal(env, alice, payload);

            env(payload, Ter(temMALFORMED));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }
    }

    // What a tem submission costs the proposal. Creation cannot store a
    // payload that fails its own preflight, so the only payload rejected
    // unaltered is one whose check creation defers: here a multi-account
    // Batch, submitted without the BatchSigners entry it will eventually be
    // given. payloadMatches is asserted first, so the rejection cannot be read
    // as the test having submitted something else.
    void
    testTemSubmissionConsumesNothing(FeatureBitset features)
    {
        testcase("a tem submission consumes neither ticket nor proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, target, bob, carol);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value const payload = proposal::unsignedBatch(
            env,
            target,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
             proposal::innerTx(pay(bob, carol, XRP(2)), env.seq(bob))});
        makeProposal(env, alice, payload);

        auto const sle = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;

        // The transaction about to be rejected is the proposal's own payload.
        auto const jt = env.jt(payload);
        if (!BEAST_EXPECT(jt.stx))
            return;
        BEAST_EXPECT(
            xrpl::proposal::payloadMatches(sle->getFieldObject(sfProposedTransaction), *jt.stx));

        auto const targetBefore = env.balance(target);
        auto const bobBefore = env.balance(bob);
        auto const carolBefore = env.balance(carol);

        env(payload, Ter(temBAD_SIGNER));
        env.close();

        // A tem is rejected before it can claim a fee, so the target paid
        // nothing for the attempt and no inner transaction ran.
        BEAST_EXPECT(env.balance(target) == targetBefore);
        BEAST_EXPECT(env.balance(bob) == bobBefore);
        BEAST_EXPECT(env.balance(carol) == carolBefore);

        // The proposal is still stored, still holding its reserve, and the
        // ticket still exists.
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);

        // And the proposal still completes on that same ticket once the
        // signatures it was waiting for arrive, which is what makes the
        // rejection above a no-op rather than a loss.
        env(payload, batch::Sig(bob));
        env.close();

        BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1) - XRP(2));
        BEAST_EXPECT(env.balance(carol) == carolBefore + XRP(2));
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // Skipping Batch::preflightSigValidated at creation defers every shape of
    // BatchSigners error, not just an absent array.
    // testMultiAccountBatchCompletes covers the absent case; these are the
    // malformed ones, each rejected at submission with the ticket and the
    // proposal untouched.
    void
    testMalformedBatchSignersOnSubmission(FeatureBitset features)
    {
        testcase("malformed BatchSigners rejected at submission");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, target, bob, carol);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // Inners from three accounts, so the completed Batch needs an entry
        // from each of bob and carol.
        json::Value const payload = proposal::unsignedBatch(
            env,
            target,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
             proposal::innerTx(pay(bob, carol, XRP(2)), env.seq(bob)),
             proposal::innerTx(pay(carol, bob, XRP(1)), env.seq(carol))});
        makeProposal(env, alice, payload);

        auto const stillTicketReservedForProposal = [&]() {
            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);
        };

        // Batch signers are checked against the required signers positionally,
        // both sorted by account ID, so which malformation a given array trips
        // depends on this ordering. Pin it: if these fixed jtx accounts ever
        // sort differently, that should fail here rather than quietly move the
        // cases below onto a different branch. The outer account is included
        // because the first case below relies on it sorting ahead of bob.
        BEAST_EXPECT(target.id() < alice.id() && alice.id() < carol.id() && carol.id() < bob.id());

        // The outer account may not sign for itself.
        env(payload, batch::Sig(target, bob), Ter(temBAD_SIGNER));
        env.close();
        stillTicketReservedForProposal();

        // A duplicated signer. It has to be carol, the first required signer:
        // a duplicated bob would be rejected as a mismatch against carol
        // before the array's second entry is ever examined.
        env(payload, batch::Sig(carol, carol), Ter(temBAD_SIGNER));
        env.close();
        stillTicketReservedForProposal();

        // An unsorted pair. batch::Sig sorts what it is given, so the array
        // has to be swapped after the fact to express this. Swapping the two
        // required signers would not reach the sort check either, so the pair
        // is carol and alice: carol matches, then alice sorts below her.
        {
            auto jt = env.jt(payload, batch::Sig(alice, carol));
            auto const first = jt.jv[sfBatchSigners.jsonName][0u];
            auto const second = jt.jv[sfBatchSigners.jsonName][1u];
            jt.jv[sfBatchSigners.jsonName][0u] = second;
            jt.jv[sfBatchSigners.jsonName][1u] = first;

            env(jt.jv, Ter(temBAD_SIGNER));
            env.close();
            stillTicketReservedForProposal();
        }

        // Correctly formed, the same proposal still completes: the rejections
        // above are about the signers array, not the stored payload.
        env(payload, batch::Sig(bob, carol));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // Create only rejects a payload whose LastLedgerSequence has *already*
    // passed (tecEXPIRED). A bound that passes afterwards is a different
    // matter, and the boundary does not line up: the proposal's own terminal
    // rule triggers at view.seq() >= LastLedgerSequence, while
    // checkPriorTxAndLastLedger rejects only at view.seq() >
    // LastLedgerSequence. So there is exactly one ledger in which the proposal
    // is terminal — anyone may cancel it — and its payload still submits.
    void
    testPayloadLastLedgerSequencePasses(FeatureBitset features)
    {
        testcase("payload's LastLedgerSequence passes while the proposal waits");

        using namespace jtx;
        using namespace std::chrono_literals;

        // One ledger past the bound: the payload can no longer be submitted,
        // and being a tef it takes neither the ticket nor the proposal with
        // it. Cancel is the only exit left.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            std::uint32_t const lastLedgerSeq = env.current()->seq() + 5;
            json::Value payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            payload[sfLastLedgerSequence.getJsonName()] = lastLedgerSeq;
            makeProposal(env, alice, payload);

            while (env.current()->seq() <= lastLedgerSeq)
                env.close();

            env(payload, Ter(tefMAX_LEDGER));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // Exactly at the bound: terminal by the proposal's reckoning, still
        // submittable by the transactor's, so the payload applies and cleans
        // up as usual.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            std::uint32_t const lastLedgerSeq = env.current()->seq() + 5;
            json::Value payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            payload[sfLastLedgerSequence.getJsonName()] = lastLedgerSeq;
            makeProposal(env, alice, payload);

            while (env.current()->seq() < lastLedgerSeq)
                env.close();

            auto const bobBefore = env.balance(bob);
            env(payload);
            env.close();

            BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
    }

    // Matching the stored payload is not authority to submit it. SigningPubKey
    // and TxnSignature are excluded from the match, so the submission's key is
    // checked on its own terms — and the target may change which keys count
    // after the proposal is stored.
    void
    testSubmissionSigningAuthority(FeatureBitset features)
    {
        testcase("submission signed by an unauthorized or disabled key");

        using namespace jtx;
        using namespace std::chrono_literals;

        // A key with no authority over the target. The payload still matches;
        // it is the authority that is missing.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            json::Value const payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            makeProposal(env, alice, payload);

            env(payload, Sig(bob), Ter(tefBAD_AUTH));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // The target disables its master key after the proposal is stored. The
        // key that would have submitted the payload no longer can.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            json::Value const payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            makeProposal(env, alice, payload);

            // Disabling the master key requires another way in first.
            env(regkey(target, bob));
            env(fset(target, asfDisableMaster), Sig(target));
            env.close();
            // Signed with the master key the proposal was made under, the
            // payload is now refused. The key has to be named explicitly:
            // left to autofill, the submission would take the regular key
            // set above and succeed, proving nothing.
            env(payload, Sig(target), Ter(tefMASTER_DISABLED));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));

            // The regular key set above is a route the payload can still take,
            // so this proposal is stalled rather than dead.
            auto const bobBefore = env.balance(bob);
            env(payload, Sig(bob));
            env.close();

            BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
    }

    // The payload's Fee is chosen at creation and is part of what a submission
    // must match, so it can never be raised. A proposal that under-budgets the
    // signatures it will need is therefore permanently unsubmittable. Both
    // unsignedBatch's numSigners parameter and the LoanSet Fee multiplier
    // exist to avoid this; these are the cases that walk into it.
    void
    testFeeFixedAtCreationCannotCoverSignatures(FeatureBitset features)
    {
        testcase("fee fixed at creation cannot cover the signatures needed");

        using namespace jtx;
        using namespace std::chrono_literals;

        // A multi-account Batch that budgeted for no BatchSigners at all.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(10000), alice, target, bob, carol);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            json::Value const payload = proposal::unsignedBatch(
                env,
                target,
                ticketSeq,
                tfAllOrNothing,
                {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
                 proposal::innerTx(pay(bob, carol, XRP(2)), env.seq(bob))},
                0u);
            makeProposal(env, alice, payload);

            // Bare, it cannot preflight: the signer entry is still required.
            env(payload, Ter(temBAD_SIGNER));
            env.close();

            // With the entry it preflights, and then cannot pay for it.
            env(payload, batch::Sig(bob), Ter(telINSUF_FEE_P));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);
        }

        // A proposed LoanSet left at the single base fee, which cannot cover
        // the CounterpartySignature it is going to need. unsignedPayload
        // stamps the single base fee when the caller does not choose one,
        // while LoanSet prices each counterparty signer at one further base
        // fee, so the payload is short by exactly one.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};  // the borrower
            Account const lender{"lender"};
            env.fund(XRP(10000), alice, target, lender);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);

            json::Value tx = loan::set(target, uint256{1}, 1'000);
            tx[sfCounterparty.getJsonName()] = lender.human();
            json::Value const payload = proposal::unsignedPayload(env, tx, ticketSeq);
            makeProposal(env, alice, payload);

            // The shortfall is what the case turns on, so pin it — read off
            // the stored proposal rather than the JSON built above.
            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(
                sle->getFieldObject(sfProposedTransaction).getFieldAmount(sfFee).xrp() ==
                env.current()->fees().base);

            env(payload, Sig(sfCounterpartySignature, lender), Ter(telINSUF_FEE_P));
            env.close();

            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }
    }

    // SigningPubKey and TxnSignature are excluded from the payload match, so a
    // submission may carry whatever it likes in them and still match. Nothing
    // about holding a proposal excuses the content of those fields: a key that
    // is not a key is refused in preflight, and a well-formed signature that
    // does not verify in preflight2.
    //
    // These go through xrpl::apply because jtx would overwrite the signature
    // fields when it signs, and the submit path rejects a bad signature itself
    // before a transactor sees it. The OpenView is discarded either way, so
    // only the returned ter and applied flag are evidence here; the
    // reservation surviving a tem is covered by
    // testTemSubmissionConsumesNothing.
    void
    testSubmissionSignatureContent(FeatureBitset features)
    {
        testcase("submission carrying malformed signature content");

        using namespace jtx;
        using namespace std::chrono_literals;

        // A SigningPubKey that is not a public key at all.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            json::Value const payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            makeProposal(env, alice, payload);

            auto jt = env.jt(payload);
            if (!BEAST_EXPECT(jt.stx))
                return;

            STObject mutated{*jt.stx};
            std::string const badKey{"badkey"};
            mutated.at(sfSigningPubKey) = makeSlice(badKey);
            auto const stx = rebuilt(mutated);

            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto const result = xrpl::apply(env.app(), view, *stx, TapNone, j);
                BEAST_EXPECT(result.ter == temBAD_SIGNATURE);
                BEAST_EXPECT(!result.applied);
                return result.applied;
            });
        }

        // A signature that is structurally fine but was made over a different
        // key's identity: bob's signature presented under the target's
        // SigningPubKey. This is the case a malformed-key check cannot catch.
        {
            Env env{*this, features};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, target, bob);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            json::Value const payload =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
            makeProposal(env, alice, payload);

            auto jt = env.jt(payload);
            auto jtOther = env.jt(payload, Sig(bob));
            if (!BEAST_EXPECT(jt.stx && jtOther.stx))
                return;

            STObject mutated{*jt.stx};
            Blob const otherSig = jtOther.stx->getFieldVL(sfTxnSignature);
            mutated.at(sfTxnSignature) = makeSlice(otherSig);
            auto const stx = rebuilt(mutated);

            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto const result = xrpl::apply(env.app(), view, *stx, TapNone, j);
                BEAST_EXPECT(result.ter == temINVALID);
                BEAST_EXPECT(!result.applied);
                return result.applied;
            });
        }
    }

    // The other way a fixed Fee stops being enough: the Fee does not change,
    // the ledger's price does. This one is not fatal, and it is not
    // telINSUF_FEE_P either — the queue holds the payload at terQUEUED rather
    // than rejecting it, and applies it once the escalated fee drops. So an
    // expensive ledger delays a completed proposal; it does not strand one.
    void
    testOpenLedgerFeeEscalation(FeatureBitset features)
    {
        testcase("open-ledger fee escalation only delays the payload");

        using namespace jtx;
        using namespace std::chrono_literals;

        auto cfg = envconfig([](std::unique_ptr<Config> cfg) {
            auto& section = cfg->section(Sections::kTransactionQueue);
            // Price above two transactions per ledger, not the standalone default of 1000.
            section.set(Keys::kMinimumTxnInLedgerStandalone, "2");
            // Hold that target across closes, which would otherwise ratchet it upward.
            section.set(Keys::kNormalConsensusIncreasePercent, "0");
            return cfg;
        });
        Env env{*this, std::move(cfg), features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};

        // Funded one at a time: with a target of two transactions per ledger,
        // funding them together would itself escalate the fee.
        env.fund(XRP(10000), alice);
        env.close();
        env.fund(XRP(10000), target);
        env.close();
        env.fund(XRP(10000), bob);
        env.close();

        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // Name the Fee rather than letting fillFee supply it: the whole case
        // turns on the payload being priced at the base fee while the ledger
        // around it is not.
        auto const baseFee = env.current()->fees().base;
        json::Value proposedTx = pay(target, bob, XRP(1));
        proposedTx[jss::Fee] = std::to_string(baseFee.drops());
        json::Value const payload = proposal::unsignedPayload(env, proposedTx, ticketSeq);
        makeProposal(env, alice, payload);

        // Fill the open ledger past its target so the next transaction is
        // priced above the base fee the payload was created with.
        env(noop(alice));
        env(noop(alice));
        env(noop(alice));

        // Both sides of the shortfall, so terQUEUED below rests on asserted
        // facts rather than on the tuning above having worked: the stored
        // payload is priced at the base fee, and the open ledger now charges
        // more than that.
        auto const sleProposal = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sleProposal))
            return;
        BEAST_EXPECT(
            sleProposal->getFieldObject(sfProposedTransaction).getFieldAmount(sfFee).xrp() ==
            baseFee);
        BEAST_EXPECT(
            toDrops(env.app().getTxQ().getMetrics(*env.current()).openLedgerFeeLevel, baseFee) >
            baseFee);

        auto const bobBefore = env.balance(bob);
        env(payload, Ter(terQUEUED));

        // Still intact while queued: nothing has been applied yet.
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));

        // Closing the ledger returns the price to the base fee, and that is
        // what lets the queue apply a payload whose Fee was fixed there: it
        // goes in unchanged, resolving the proposal as any match would.
        env.close();

        BEAST_EXPECT(
            toDrops(env.app().getTxQ().getMetrics(*env.current()).openLedgerFeeLevel, baseFee) ==
            baseFee);

        BEAST_EXPECT(env.balance(bob) == bobBefore + XRP(1));
        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    void
    run() override
    {
        using namespace jtx;
        auto const all = testableAmendments();
        testUnrelatedSpendBlocked(all);
        testMatchingPayloadExecutes(all);
        testCancelFreesTicket(all);
        testMatchingTecStillCleansUp(all);
        testOtherAccountsSameNumberedTicket(all);
        testProposerIsTarget(all);
        testBatchProposal(all);
        testMultiAccountBatchCompletes(all);
        testCounterpartySignatureMatches(all);
        testSponsorSignatureMatches(all);
        testInnerBatchSpendBlocked(all);
        testTerminalStillReservesTicket(all);
        testSponsoredReserveAutoDelete(all);
        testTargetAccountDeleted(all);
        testTargetDeletionRefundsEachOwner(all);
        testProposalBlocksOwnerAccountDelete(all);
        testSignerListChangedUnderProposal(all);
        testDeferredSignatureChecksFireOnSubmission(all);
        testTemSubmissionConsumesNothing(all);
        testMalformedBatchSignersOnSubmission(all);
        testPayloadLastLedgerSequencePasses(all);
        testSubmissionSigningAuthority(all);
        testFeeFixedAtCreationCannotCoverSignatures(all);
        testSubmissionSignatureContent(all);
        testOpenLedgerFeeEscalation(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalReservedTicket, app, xrpl);

}  // namespace xrpl::test
