#include <xrpl/tx/invariants/CosignerInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace xrpl {

namespace {

bool
validSignerArray(STArray const& signers, std::size_t limit, SField const& elementName)
{
    if (signers.size() > limit)
        return false;

    std::optional<AccountID> previous;
    for (auto const& signer : signers)
    {
        if (signer.getFName() != elementName || !signer.isFieldPresent(sfAccount))
            return false;

        auto const account = signer.getAccountID(sfAccount);
        if (previous && account <= *previous)
            return false;
        previous = account;
    }
    return true;
}

bool
validNestedSigners(STObject const& signature)
{
    return !signature.isFieldPresent(sfSigners) ||
        validSignerArray(signature.getFieldArray(sfSigners), STTx::kMaxMultiSigners, sfSigner);
}

bool
validSignerArrays(STObject const& proposedTx)
{
    if (!validNestedSigners(proposedTx))
        return false;

    for (auto const* field :
         std::array<SField const*, 2>{&sfCounterpartySignature, &sfSponsorSignature})
    {
        if (proposedTx.isFieldPresent(*field) &&
            !validNestedSigners(proposedTx.getFieldObject(*field)))
            return false;
    }

    if (!proposedTx.isFieldPresent(sfBatchSigners))
        return true;

    auto const& batchSigners = proposedTx.getFieldArray(sfBatchSigners);
    if (!validSignerArray(batchSigners, kMaxBatchSigners, sfBatchSigner))
        return false;

    return std::ranges::all_of(
        batchSigners, [](auto const& batchSigner) { return validNestedSigners(batchSigner); });
}

// The Ticket a proposal is keyed to: the one its proposed transaction spends.
uint256
proposalTicketKey(SLE const& proposal)
{
    auto const& proposedTx = proposal.getFieldObject(sfProposedTransaction);
    return keylet::ticket(
               proposedTx.getAccountID(sfAccount),
               SeqProxy::rawTicket(proposedTx.getFieldU32(sfTicketSequence)))
        .key;
}

}  // namespace

void
ValidTransactionProposal::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    // `after` is always present (on deletion it holds the final state), so it
    // alone determines the entry type. LedgerEntryTypesMatch owns malformed
    // type transitions; skip them rather than read one side with the other's
    // schema.
    if (!after || (before && before->getType() != after->getType()))
        return;

    auto const type = after->getType();

    // Consuming a Ticket deletes the proposal keyed to it (XLS-0103 §4.5).
    // Remember each Ticket that went, so finalize can tell a proposal deleted
    // with its Ticket from one that simply vanished, and the proposal each
    // Ticket was keyed to, so finalize can check that none stayed behind.
    if (type == ltTICKET)
    {
        if (isDelete)
        {
            deletedTickets_.insert(after->key());
            proposalsOfDeletedTickets_.push_back(
                keylet::txProposal(
                    after->getAccountID(sfAccount), after->getFieldU32(sfTicketSequence)));
        }
        return;
    }

    if (type != ltTRANSACTION_PROPOSAL)
        return;

    if (!before)
    {
        ++created_;
    }
    else if (isDelete)
    {
        ++deleted_;
        ticketsOfDeletedProposals_.push_back(proposalTicketKey(*before));
    }
    else
    {
        ++modified_;
    }

    // A live proposal's signer lists must stay submittable: within size
    // limits, sorted by account, and free of duplicates.
    if (!isDelete && !validSignerArrays(after->getFieldObject(sfProposedTransaction)))
        invalidSignerArrays_ = true;
}

bool
ValidTransactionProposal::finalize(
    STTx const& tx,
    TER result,
    XRPAmount,
    ReadView const& view,
    beast::Journal const& j) const
{
    if (!view.rules().enabled(featureCosign))
        return true;

    if (invalidSignerArrays_)
    {
        JLOG(j.fatal()) << "Invariant failed: TransactionProposal signer arrays are not canonical.";
        return false;
    }

    // A proposal leaves the ledger in two ways (XLS-0103 §4.5): a
    // TransactionProposalCancel deletes the proposal it names, and consuming
    // the Ticket a proposal is keyed to deletes the proposal with it, whether
    // the proposed transaction executed (or failed with a claimed-fee tec) or
    // AccountDelete swept the target's Tickets.
    std::uint32_t deletedWithTicket = 0;
    for (auto const& ticketKey : ticketsOfDeletedProposals_)
    {
        if (deletedTickets_.contains(ticketKey))
            ++deletedWithTicket;
    }

    bool const succeeded = isTesSuccess(result);
    bool effectsMatch = false;
    if (tx.getTxnType() == ttTRANSACTION_PROPOSAL_CREATE)
    {
        effectsMatch = created_ == (succeeded ? 1u : 0u) && modified_ == 0 && deleted_ == 0;
    }
    else if (tx.getTxnType() == ttTRANSACTION_PROPOSAL_CANCEL)
    {
        // A Cancel deletes one proposal and nothing else. It cannot
        // spend a reserved Ticket, so no proposal goes with its Ticket here.
        effectsMatch = created_ == 0 && modified_ == 0 && deletedWithTicket == 0 &&
            deleted_ == (succeeded ? 1u : 0u);
    }
    else
    {
        // Any other transaction deletes a proposal only together with its
        // Ticket, and modifies at most one, only as a successful
        // SponsorshipTransfer.
        // TODO: extend this when TransactionProposalSign is implemented.
        std::uint32_t const modifiable =
            tx.getTxnType() == ttSPONSORSHIP_TRANSFER && succeeded ? 1 : 0;
        effectsMatch = created_ == 0 && modified_ <= modifiable && deleted_ == deletedWithTicket;
    }

    if (!effectsMatch)
    {
        JLOG(j.fatal())
            << "Invariant failed: TransactionProposal changes do not match transaction result.";
        return false;
    }

    // The other direction of the deletion rule: ticketDelete deletes the
    // proposal keyed to a Ticket along with it (XLS-0103 §4.5), so no
    // proposal may outlive its Ticket, orphaned on a key that can no longer
    // be spent.
    bool const orphaned = std::ranges::any_of(
        proposalsOfDeletedTickets_, [&view](Keylet const& keylet) { return view.exists(keylet); });
    if (orphaned)
    {
        JLOG(j.fatal())
            << "Invariant failed: TransactionProposal outlived the Ticket it is keyed to.";
        return false;
    }

    return true;
}

}  // namespace xrpl
