#include <xrpl/tx/invariants/CosignerInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
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
#include <utility>

namespace xrpl {

namespace {

std::int64_t
fieldDelta(bool isDelete, SLE::const_ref before, SLE::const_ref after, SField const& field)
{
    auto const value = [&field](SLE::const_ref sle) -> std::int64_t {
        return sle ? sle->getFieldU32(field) : 0;
    };
    return (isDelete ? 0 : value(after)) - value(before);
}

void
removeSignatureFields(STObject& tx)
{
    for (auto const* field : std::array<SField const*, 6>{
             &sfSigningPubKey,
             &sfTxnSignature,
             &sfSigners,
             &sfCounterpartySignature,
             &sfSponsorSignature,
             &sfBatchSigners})
    {
        if (tx.isFieldPresent(*field))
            tx.makeFieldAbsent(*field);
    }
}

std::pair<STObject, STObject>
proposalObjectsWithoutBookkeeping(SLE const& before, SLE const& after)
{
    STObject beforeObject{static_cast<STObject const&>(before)};
    STObject afterObject{static_cast<STObject const&>(after)};

    for (auto const* field : std::array<SField const*, 2>{&sfPreviousTxnID, &sfPreviousTxnLgrSeq})
    {
        if (beforeObject.isFieldPresent(*field))
            beforeObject.makeFieldAbsent(*field);
        if (afterObject.isFieldPresent(*field))
            afterObject.makeFieldAbsent(*field);
    }

    return {std::move(beforeObject), std::move(afterObject)};
}

bool
onlySignatureFieldsChanged(SLE const& before, SLE const& after)
{
    auto [beforeObject, afterObject] = proposalObjectsWithoutBookkeeping(before, after);
    auto beforeTx = beforeObject.getFieldObject(sfProposedTransaction);
    auto afterTx = afterObject.getFieldObject(sfProposedTransaction);
    removeSignatureFields(beforeTx);
    removeSignatureFields(afterTx);
    beforeObject.setFieldObject(sfProposedTransaction, beforeTx);
    afterObject.setFieldObject(sfProposedTransaction, afterTx);

    return beforeObject == afterObject;
}

bool
onlySponsorChanged(SLE const& before, SLE const& after)
{
    auto [beforeObject, afterObject] = proposalObjectsWithoutBookkeeping(before, after);
    if (beforeObject.isFieldPresent(sfSponsor))
        beforeObject.makeFieldAbsent(sfSponsor);
    if (afterObject.isFieldPresent(sfSponsor))
        afterObject.makeFieldAbsent(sfSponsor);
    return beforeObject == afterObject;
}

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

// Every expected move happened, and nothing moved that was not expected. An
// account can appear in either map with a delta of zero (the fee payer's root,
// for one), so absence from the other map counts as zero.
template <class Map>
bool
deltasMatch(Map const& actual, Map const& expected)
{
    auto const deltaIn = [](Map const& map, AccountID const& account) -> std::int64_t {
        auto const iter = map.find(account);
        return iter == map.end() ? 0 : iter->second;
    };
    auto const happened = [&](auto const& item) {
        return deltaIn(actual, item.first) == item.second;
    };
    auto const wasExpected = [&](auto const& item) {
        return deltaIn(expected, item.first) == item.second;
    };
    return std::ranges::all_of(expected, happened) && std::ranges::all_of(actual, wasExpected);
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
ValidTransactionProposal::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // `after` is always present (on deletion it holds the final state), so it
    // alone determines the entry type. LedgerEntryTypesMatch owns malformed
    // type transitions; skip them rather than read one side with the other's
    // schema.
    if (!after || (before && before->getType() != after->getType()))
        return;

    auto const type = after->getType();

    if (type == ltACCOUNT_ROOT)
    {
        auto const account = after->getAccountID(sfAccount);
        ownerCountDelta_[account] += fieldDelta(isDelete, before, after, sfOwnerCount);
        sponsoredOwnerCountDelta_[account] +=
            fieldDelta(isDelete, before, after, sfSponsoredOwnerCount);
        sponsoringOwnerCountDelta_[account] +=
            fieldDelta(isDelete, before, after, sfSponsoringOwnerCount);
        return;
    }

    // A transaction that touches a proposal may also consume a Ticket: a
    // Create or Cancel paying with one of its own, or the proposed transaction
    // spending the very Ticket its proposal is keyed to. Account for that
    // independent owner-count change so the proposal's five- or ten-unit
    // reserve delta is still checked exactly, and remember which Tickets went
    // so finalize can tell a proposal deleted with its Ticket from one that
    // simply vanished.
    if (type == ltTICKET)
    {
        // A Ticket is only ever created or deleted, never modified in place.
        std::int64_t direction = 0;
        if (!before && !isDelete)
        {
            direction = 1;
        }
        else if (before && isDelete)
        {
            direction = -1;
        }

        auto const owner = after->getAccountID(sfAccount);
        expectedOwnerCountDelta_[owner] += direction;

        // ticketDelete releases a Ticket's reserve through
        // decreaseOwnerCountForObject, so a sponsored Ticket also moves its
        // owner's SponsoredOwnerCount and its sponsor's SponsoringOwnerCount.
        // No transaction sponsors a Ticket today; the accounting is in place
        // so that allowing one cannot fail every ticketed Create.
        if (after->isFieldPresent(sfSponsor))
        {
            expectedSponsoredOwnerCountDelta_[owner] += direction;
            expectedSponsoringOwnerCountDelta_[after->getAccountID(sfSponsor)] += direction;
        }

        if (isDelete)
            deletedTickets_.insert(after->key());
        return;
    }

    if (type != ltTRANSACTION_PROPOSAL)
        return;

    changes_.push_back({.isDelete = isDelete, .before = before, .after = after});

    if (!before)
    {
        ++created_;
    }
    else if (isDelete)
    {
        ++deleted_;
    }
    else
    {
        ++modified_;
    }

    // A live proposal's signer lists must stay submittable: within size
    // limits, sorted by account, and free of duplicates.
    if (!isDelete && !validSignerArrays(after->getFieldObject(sfProposedTransaction)))
        invalidSignerArrays_ = true;

    auto recordReserveState = [&](SLE::const_ref sle, std::int64_t direction) {
        auto const owner = sle->getAccountID(sfOwner);
        auto const reserve =
            proposal::proposalOwnerCount(sle->getFieldObject(sfProposedTransaction)) * direction;
        expectedOwnerCountDelta_[owner] += reserve;
        expectedSponsoredOwnerCountDelta_[owner] += sle->isFieldPresent(sfSponsor) ? reserve : 0;
        if (sle->isFieldPresent(sfSponsor))
            expectedSponsoringOwnerCountDelta_[sle->getAccountID(sfSponsor)] += reserve;
    };

    // Subtract the reserve the proposal held before, then add the reserve it
    // holds now. For a proposal with a reserve of 5:
    //   Create:                   +5                -> owner +5
    //   Delete:                   -5                -> owner -5
    //   Add a signature:          -5 then +5        -> owner  0
    //   Transfer sponsor A to B:  -5 on A, +5 on B  -> owner  0, A -5, B +5
    if (before)
        recordReserveState(before, -1);
    if (!isDelete)
        recordReserveState(after, 1);
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

    for (auto const& change : changes_)
    {
        if (!change.before || change.isDelete)
            continue;

        bool const allowed = tx.getTxnType() == ttSPONSORSHIP_TRANSFER
            ? onlySponsorChanged(*change.before, *change.after)
            : onlySignatureFieldsChanged(*change.before, *change.after);
        if (!allowed)
        {
            JLOG(j.fatal()) << "Invariant failed: TransactionProposal immutable fields changed.";
            return false;
        }
    }

    // A proposal leaves the ledger in two ways (XLS-0103 §4.5): a
    // TransactionProposalCancel deletes the proposal it names, and consuming
    // the Ticket a proposal is keyed to deletes the proposal with it, whether
    // the proposed transaction executed (or failed with a claimed-fee tec) or
    // AccountDelete swept the target's Tickets.
    std::uint32_t deletedWithTicket = 0;
    for (auto const& change : changes_)
    {
        if (change.isDelete && change.before &&
            deletedTickets_.contains(proposalTicketKey(*change.before)))
            ++deletedWithTicket;
    }

    // Owner counts move for many reasons unrelated to proposals: an offer, an
    // escrow, or simply paying with a ticket. The expected deltas model only a
    // proposal's reserve plus the Ticket the transaction itself consumed, so
    // they describe the ledger exactly only for transactions whose other
    // effects are known: Create, Cancel, and a SponsorshipTransfer reassigning
    // a proposal. A proposal deleted with its Ticket goes inside a transaction
    // of any type, with arbitrary effects of its own, so no exact comparison
    // is possible there and the reserve release rests on deleteProposal.
    bool const reserveMatches = changes_.empty() || deletedWithTicket > 0 ||
        (deltasMatch(ownerCountDelta_, expectedOwnerCountDelta_) &&
         deltasMatch(sponsoredOwnerCountDelta_, expectedSponsoredOwnerCountDelta_) &&
         deltasMatch(sponsoringOwnerCountDelta_, expectedSponsoringOwnerCountDelta_));
    if (!reserveMatches)
    {
        JLOG(j.fatal())
            << "Invariant failed: TransactionProposal reserve accounting is inconsistent.";
        return false;
    }

    bool const succeeded = isTesSuccess(result);
    bool effectsMatch = false;
    if (tx.getTxnType() == ttTRANSACTION_PROPOSAL_CREATE)
    {
        effectsMatch = created_ == (succeeded ? 1u : 0u) && modified_ == 0 && deleted_ == 0;
    }
    else if (tx.getTxnType() == ttTRANSACTION_PROPOSAL_CANCEL)
    {
        // A Cancel deletes the proposal it names and nothing else. It cannot
        // spend a reserved Ticket, so no proposal goes with its Ticket here.
        effectsMatch = created_ == 0 && modified_ == 0 && deletedWithTicket == 0 &&
            deleted_ == (succeeded ? 1u : 0u);
    }
    else
    {
        // Any other transaction deletes a proposal only together with its
        // Ticket, and modifies one only as a SponsorshipTransfer reassigning
        // the Sponsor. TransactionProposalSign is not part of this amendment
        // branch yet; extend this when it is implemented.
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

    return true;
}

}  // namespace xrpl
