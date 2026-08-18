#include <test/jtx/proposal.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/batch.h>
#include <test/jtx/ticket.h>
#include <test/jtx/utility.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test::jtx::proposal {

json::Value
create(Account const& proposer, json::Value const& proposedTx, std::uint32_t expiration)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::TransactionProposalCreate;
    jv[jss::Account] = proposer.human();
    jv[sfProposedTransaction.jsonName] = proposedTx;
    jv[sfExpiration.jsonName] = expiration;
    return jv;
}

json::Value
unsignedPayload(Env const& env, json::Value tx, std::uint32_t ticketSeq)
{
    // Unsigned canonical form: an empty SigningPubKey and no signature fields
    // at all. Signatures may only ever arrive through TransactionProposalSign.
    tx[jss::SigningPubKey] = "";

    // Ticket-based rather than sequence-based. Sequence is a required common
    // field, so "no Sequence" is expressed as a Sequence of 0.
    tx[jss::Sequence] = 0;
    tx[sfTicketSequence.jsonName] = ticketSeq;

    // The target account pays this fee when the completed transaction is
    // submitted, so it is fixed now. A fee already chosen by the caller stands.
    fillFee(tx, *env.current());

    return tx;
}

json::Value
innerTx(json::Value tx, std::uint32_t seq)
{
    return batch::Inner{std::move(tx), seq}.getTxn();
}

json::Value
unsignedBatch(
    Env const& env,
    Account const& target,
    std::uint32_t ticketSeq,
    std::uint32_t flags,
    std::vector<json::Value> const& inners,
    std::optional<std::uint32_t> numSigners)
{
    // Each inner account other than the outer one will contribute one
    // BatchSigners entry once the signatures are collected, and the outer fee
    // has to cover them from the start (Batch::calculateBaseFee).
    std::uint32_t const signers = numSigners ? *numSigners : [&]() {
        std::set<std::string> participants;
        for (auto const& inner : inners)
        {
            if (auto const account = inner[jss::Account].asString(); account != target.human())
                participants.insert(account);
        }
        return static_cast<std::uint32_t>(participants.size());
    }();

    json::Value jv = batch::outer(
        target,
        0,
        batch::calcBatchFee(env, signers, static_cast<std::uint32_t>(inners.size())),
        flags);

    json::Value& rawTransactions = jv[jss::RawTransactions];
    for (auto const& inner : inners)
        rawTransactions[rawTransactions.size()][jss::RawTransaction] = inner;

    return unsignedPayload(env, std::move(jv), ticketSeq);
}

std::uint32_t
createTicket(Env& env, Account const& account, std::uint32_t count)
{
    // The tickets a TicketCreate makes are numbered from the sequence that
    // follows the one it consumes.
    std::uint32_t const firstTicketSeq = env.seq(account) + 1;
    env(ticket::create(account, count));
    env.close();
    return firstTicketSeq;
}

std::uint32_t
expiration(Env& env, NetClock::duration delta)
{
    return (env.now() + delta).time_since_epoch().count();
}

SLE::const_pointer
entry(Env const& env, AccountID const& target, std::uint32_t ticketSeq)
{
    return env.le(keylet::txProposal(target, ticketSeq));
}

SLE::const_pointer
entry(Env const& env, Account const& target, std::uint32_t ticketSeq)
{
    return entry(env, target.id(), ticketSeq);
}

void
verify::Create::operator()(Env& env, JTx& jt) const
{
    // Only a TransactionProposalCreate carries the fields read below, and a
    // condition that quietly checks nothing is worse than none at all.
    if (jt.jv[jss::TransactionType].asString() != jss::TransactionProposalCreate.cStr())
        Throw<std::logic_error>("proposal::verify::create: not a TransactionProposalCreate");

    // Funclets run before the transaction is applied, so everything read here
    // is the state the effects are measured against.
    json::Value const& proposedTx = jt.jv[sfProposedTransaction.jsonName];
    auto const target = parseBase58<AccountID>(proposedTx[jss::Account].asString());

    // A payload naming no usable target is a malformed case a test is making
    // on purpose, and has no ledger effect to measure.
    if (!target)
        return;

    Account const proposer = env.lookup(jt.jv[jss::Account].asString());
    std::uint32_t const ticketSeq = proposedTx[sfTicketSequence.jsonName].asUInt();
    std::uint32_t const expiration = jt.jv[sfExpiration.jsonName].asUInt();
    std::uint32_t const cost = proposedTx[jss::TransactionType].asString() == jss::Batch.cStr()
        ? kBatchProposalOwnerCount
        : kProposalOwnerCount;

    std::uint32_t const ownerCountBefore = env.ownerCount(proposer);
    bool const existedBefore = static_cast<bool>(entry(env, *target, ticketSeq));

    jt.require.emplace_back([proposer,
                             target = *target,
                             ticketSeq,
                             expiration,
                             cost,
                             ownerCountBefore,
                             existedBefore,
                             submitted = proposedTx](Env& applied) {
        bool const created = applied.ter() == tesSUCCESS;
        applied.test.expect(
            applied.ownerCount(proposer) == ownerCountBefore + (created ? cost : 0),
            "proposal reserve");

        auto const sle = entry(applied, target, ticketSeq);
        if (!applied.test.expect(
                static_cast<bool>(sle) == (existedBefore || created), "proposal entry") ||
            !created)
            return;

        // What is on the ledger is what was submitted. A proposal is only
        // worth collecting signatures against if the transaction it stores is
        // the one that was proposed, so the payload is compared whole rather
        // than field by field.
        applied.test.expect(sle->getAccountID(sfOwner) == proposer.id(), "proposal owner");
        applied.test.expect(sle->getFieldU32(sfExpiration) == expiration, "proposal expiration");
        applied.test.expect(
            sle->getFieldObject(sfProposedTransaction) == parse(submitted), "proposal payload");
    });
}

}  // namespace xrpl::test::jtx::proposal
