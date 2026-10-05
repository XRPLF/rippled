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

    // A TransactionProposalCreate may itself consume a different Ticket.
    // Account for that independent owner-count change so the proposal's
    // five- or ten-unit reserve delta is still checked exactly.
    if (type == ltTICKET)
    {
        auto const owner = after->getAccountID(sfAccount);
        if (!before && !isDelete)
        {
            expectedOwnerCountDelta_[owner] += 1;
        }
        else if (before && isDelete)
        {
            expectedOwnerCountDelta_[owner] -= 1;
        }
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
        return false;
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
        return false;
    }

    return true;
}

}  // namespace xrpl
