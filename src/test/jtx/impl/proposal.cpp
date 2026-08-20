#include <test/jtx/proposal.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/batch.h>
#include <test/jtx/ticket.h>
#include <test/jtx/utility.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <map>
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

namespace {

// The keys an account's owner directory lists, each with the page it sits on.
// The pages are read directly, so a key with nothing behind it is still seen.
std::map<uint256, std::uint64_t>
ownerDirKeys(ReadView const& view, AccountID const& account)
{
    std::map<uint256, std::uint64_t> keys;

    Keylet const root = keylet::ownerDir(account);
    std::uint64_t page = 0;
    for (auto sle = view.read(root); sle;)
    {
        for (auto const& key : sle->getFieldV256(sfIndexes))
            keys.emplace(key, page);

        page = sle->getFieldU64(sfIndexNext);
        if (page == 0)
            break;
        sle = view.read(keylet::page(root, page));
    }

    return keys;
}

// Whether two reads of a ledger entry found it unchanged, or found nothing both
// times. Entries that are there are compared whole.
bool
unchanged(SLE::const_pointer const& before, SLE::const_pointer const& after)
{
    if (!before || !after)
        return !before && !after;

    return before->key() == after->key() &&
        static_cast<STObject const&>(*before) == static_cast<STObject const&>(*after);
}

// Whether a TransactionProposal entry may carry the field when it is created.
// A fresh proposal has gathered no signatures, so it carries nothing else.
bool
isCreationField(SField const& field)
{
    return field == sfLedgerEntryType || field == sfFlags || field == sfOwner ||
        field == sfProposedTransaction || field == sfExpiration || field == sfOwnerNode ||
        field == sfPreviousTxnID || field == sfPreviousTxnLgrSeq;
}

}  // namespace

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
    auto const parsedTarget = parseBase58<AccountID>(proposedTx[jss::Account].asString());

    // A payload naming no usable target is a malformed case a test is making
    // on purpose, and has no ledger effect to measure.
    if (!parsedTarget)
        return;

    AccountID const target = *parsedTarget;
    Account const proposer = env.lookup(jt.jv[jss::Account].asString());
    std::uint32_t const ticketSeq = proposedTx[sfTicketSequence.jsonName].asUInt();
    std::uint32_t const expiration = jt.jv[sfExpiration.jsonName].asUInt();
    std::uint32_t const cost = proposedTx[jss::TransactionType].asString() == jss::Batch.cStr()
        ? kBatchProposalOwnerCount
        : kProposalOwnerCount;

    // Every entry the transaction could touch, read whole, so what follows can
    // say that nothing moved rather than that the fields we named did not.
    ReadView const& view = *env.current();
    Keylet const proposalKeylet = keylet::txProposal(target, ticketSeq);
    std::uint32_t const ownerCountBefore = env.ownerCount(proposer);
    SLE::const_pointer const proposalBefore = view.read(proposalKeylet);
    SLE::const_pointer const targetBefore = view.read(keylet::account(target));
    SLE::const_pointer const ticketBefore = view.read(keylet::ticket(target, ticketSeq));
    std::map<uint256, std::uint64_t> const proposerDirBefore = ownerDirKeys(view, proposer.id());
    std::map<uint256, std::uint64_t> const targetDirBefore = ownerDirKeys(view, target);

    jt.require.emplace_back([=](Env& applied) {
        auto& test = applied.test;
        ReadView const& view = *applied.current();

        bool const created = isTesSuccess(applied.ter());

        // A proposal holds owner-reserve increments against its proposer, more
        // of them for a proposed Batch than for anything else.
        test.expect(
            applied.ownerCount(proposer) == ownerCountBefore + (created ? cost : 0),
            "proposal reserve");

        // Nothing of the target's moves, whatever the outcome: the proposal is
        // the proposer's object and the ticket is left for the proposed
        // transaction. A proposer proposing for its own account is exempt.
        if (target != proposer.id())
        {
            test.expect(
                unchanged(targetBefore, view.read(keylet::account(target))),
                "proposal target account");
            test.expect(
                unchanged(ticketBefore, view.read(keylet::ticket(target, ticketSeq))),
                "proposal target ticket");
            test.expect(ownerDirKeys(view, target) == targetDirBefore, "proposal target directory");
        }

        auto const sleProposal = view.read(proposalKeylet);

        if (!created)
        {
            // A create that did not succeed leaves the proposal as it found
            // it, down to the last field.
            test.expect(unchanged(proposalBefore, sleProposal), "proposal unchanged");

            // Nor did the directory gain a listing for an entry that does not
            // exist.
            test.expect(
                ownerDirKeys(view, proposer.id()) == proposerDirBefore, "proposal owner directory");
            return;
        }

        // A successful create must have created the entry, not overwritten one
        // that was already there. The checks below read the entry after the
        // write, so they pass either way; this is what rules an overwrite out.
        if (!test.expect(!proposalBefore, "proposal is new") ||
            !test.expect(sleProposal, "proposal entry"))
            return;

        // What is on the ledger is what was submitted: a proposal is only worth
        // collecting signatures against if the transaction it stores is the one
        // proposed, so the payload is compared whole.
        test.expect(sleProposal->getAccountID(sfOwner) == proposer.id(), "proposal owner");
        test.expect(sleProposal->getFieldU32(sfExpiration) == expiration, "proposal expiration");

        STObject const& stored = sleProposal->getFieldObject(sfProposedTransaction);
        test.expect(stored == parse(proposedTx), "proposal payload");

        // Unsigned canonical form, keyed by the target and ticket the payload
        // names (On-Chain Cosigner spec §6.1).
        test.expect(
            xrpl::proposal::hasEmptySigningPubKey(stored) &&
                !xrpl::proposal::hasSignatureField(stored),
            "proposal payload unsigned");
        test.expect(
            stored.getAccountID(sfAccount) == target &&
                stored.getFieldU32(sfTicketSequence) == ticketSeq &&
                stored.getFieldU32(sfSequence) == 0,
            "proposal payload target");

        // Nothing beyond the fields a fresh proposal is created with. An
        // STObject carries a placeholder for each optional field its format
        // allows, so each field is asked whether it is really present.
        test.expect(sleProposal->getFieldU32(sfFlags) == 0, "proposal flags");
        for (auto const& field : *sleProposal)
        {
            if (field.getSType() != STI_NOTPRESENT)
            {
                test.expect(
                    isCreationField(field.getFName()),
                    "proposal field " + field.getFName().getName());
            }
        }

        // The proposal is listed in the proposer's directory on the page its
        // OwnerNode names, and nothing else listed moved.
        auto expectedDir = proposerDirBefore;
        expectedDir.emplace(sleProposal->key(), sleProposal->getFieldU64(sfOwnerNode));
        test.expect(ownerDirKeys(view, proposer.id()) == expectedDir, "proposal owner directory");
    });
}

}  // namespace xrpl::test::jtx::proposal
