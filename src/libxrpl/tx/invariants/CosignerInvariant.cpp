#include <xrpl/tx/invariants/CosignerInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
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

template <class Map>
bool
deltasMatch(Map const& actual, Map const& expected)
{
    return std::ranges::all_of(expected, [&actual](auto const& item) {
        auto const& [account, expectedDelta] = item;
        auto const iter = actual.find(account);
        auto const actualDelta = iter == actual.end() ? 0 : iter->second;
        return actualDelta == expectedDelta;
    });
}

}  // namespace

void
ValidTransactionProposal::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // LedgerEntryTypesMatch owns malformed type transitions. Avoid interpreting
    // either side using the other side's schema.
    if (before && after && before->getType() != after->getType())
        return;

    if ((before && before->getType() == ltACCOUNT_ROOT) ||
        (after && after->getType() == ltACCOUNT_ROOT))
    {
        auto const& accountSle = after ? after : before;
        auto const account = accountSle->getAccountID(sfAccount);
        ownerCountDelta_[account] += fieldDelta(isDelete, before, after, sfOwnerCount);
        sponsoredOwnerCountDelta_[account] +=
            fieldDelta(isDelete, before, after, sfSponsoredOwnerCount);
        sponsoringOwnerCountDelta_[account] +=
            fieldDelta(isDelete, before, after, sfSponsoringOwnerCount);
    }

    // A TransactionProposalCreate may itself consume a different Ticket.
    // Account for that independent owner-count change so the proposal's
    // five- or ten-unit reserve delta is still checked exactly.
    if ((before && before->getType() == ltTICKET) || (after && after->getType() == ltTICKET))
    {
        auto const& ticket = before ? before : after;
        auto const owner = ticket->getAccountID(sfAccount);
        if (!before && !isDelete)
        {
            expectedOwnerCountDelta_[owner] += 1;
        }
        else if (before && isDelete)
        {
            expectedOwnerCountDelta_[owner] -= 1;
        }
    }

    bool const proposalBefore = before && before->getType() == ltTRANSACTION_PROPOSAL;
    bool const proposalAfter = after && after->getType() == ltTRANSACTION_PROPOSAL;
    if (!proposalBefore && !proposalAfter)
        return;

    changes_.push_back({.isDelete = isDelete, .before = before, .after = after});

    if (!proposalBefore)
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

    if (!isDelete && proposalAfter &&
        !validSignerArrays(after->getFieldObject(sfProposedTransaction)))
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

    if (proposalBefore)
        recordReserveState(before, -1);
    if (!isDelete && proposalAfter)
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
    bool valid = true;

    bool immutableFieldsChanged = false;
    for (auto const& change : changes_)
    {
        if (!change.before || change.isDelete)
            continue;

        bool const allowed = tx.getTxnType() == ttSPONSORSHIP_TRANSFER
            ? onlySponsorChanged(*change.before, *change.after)
            : onlySignatureFieldsChanged(*change.before, *change.after);
        immutableFieldsChanged |= !allowed;
    }

    if (immutableFieldsChanged)
    {
        JLOG(j.fatal()) << "Invariant failed: TransactionProposal immutable fields changed.";
        valid = false;
    }

    if (invalidSignerArrays_)
    {
        JLOG(j.fatal()) << "Invariant failed: TransactionProposal signer arrays are not canonical.";
        valid = false;
    }

    // Owner counts move for many reasons unrelated to proposals: an offer, an
    // escrow, or simply paying with a ticket. The expected deltas model only a
    // proposal's reserve plus the ticket the transaction itself consumed, so
    // they describe the ledger accurately only once a proposal is involved.
    bool const reserveMatches = changes_.empty() ||
        (deltasMatch(ownerCountDelta_, expectedOwnerCountDelta_) &&
         deltasMatch(sponsoredOwnerCountDelta_, expectedSponsoredOwnerCountDelta_) &&
         deltasMatch(sponsoringOwnerCountDelta_, expectedSponsoringOwnerCountDelta_));
    if (!reserveMatches)
    {
        JLOG(j.fatal())
            << "Invariant failed: TransactionProposal reserve accounting is inconsistent.";
        valid = false;
    }

    bool effectsMatch = false;
    if (tx.getTxnType() == ttTRANSACTION_PROPOSAL_CREATE)
    {
        effectsMatch = isTesSuccess(result) ? created_ == 1 && modified_ == 0 && deleted_ == 0
                                            : created_ == 0 && modified_ == 0 && deleted_ == 0;
    }
    else if (tx.getTxnType() == ttSPONSORSHIP_TRANSFER)
    {
        effectsMatch = isTesSuccess(result) ? created_ == 0 && modified_ <= 1 && deleted_ == 0
                                            : created_ == 0 && modified_ == 0 && deleted_ == 0;
    }
    else
    {
        // TransactionProposalSign, TransactionProposalCancel, and automatic
        // ticket cleanup are not part of this amendment branch yet. Extend
        // this whitelist when each lifecycle operation is implemented.
        effectsMatch = created_ == 0 && modified_ == 0 && deleted_ == 0;
    }

    if (!effectsMatch)
    {
        JLOG(j.fatal())
            << "Invariant failed: TransactionProposal changes do not match transaction result.";
        valid = false;
    }

    return valid || !view.rules().enabled(featureCosign);
}

}  // namespace xrpl
