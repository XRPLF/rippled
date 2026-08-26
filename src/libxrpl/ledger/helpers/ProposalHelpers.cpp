#include <xrpl/ledger/helpers/ProposalHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <optional>
#include <utility>

namespace xrpl::proposal {
namespace {

STObject
innerTxn(STObject const& wrapper)
{
    if (wrapper.isFieldPresent(sfTransactionType))
        return wrapper;
    return wrapper.getFieldObject(sfRawTransaction);
}

bool
isOuterSigningFor(STObject const& proposedTx, AccountID const& signingFor)
{
    if (signingFor == proposedTx.getAccountID(sfAccount))
        return true;
    return proposedTx.isFieldPresent(sfDelegate) &&
        signingFor == proposedTx.getAccountID(sfDelegate);
}

STObject*
findBatchSigner(STArray& batchSigners, AccountID const& signingFor)
{
    auto const it = std::ranges::find_if(batchSigners, [&](auto const& entry) {
        return entry.getAccountID(sfAccount) == signingFor;
    });
    return it == batchSigners.end() ? nullptr : &*it;
}

void
sortByAccount(STArray& entries)
{
    std::ranges::sort(entries, [](auto const& lhs, auto const& rhs) {
        return lhs.getAccountID(sfAccount) < rhs.getAccountID(sfAccount);
    });
}

bool
accountPresent(STArray const& signers, AccountID const& account)
{
    return std::ranges::any_of(
        signers, [&](auto const& entry) { return entry.getAccountID(sfAccount) == account; });
}

STObject
makeSignerEntry(STObject const& proposalSignature)
{
    auto entry = STObject::makeInnerObject(sfSigner);
    entry.setAccountID(sfAccount, proposalSignature.getAccountID(sfAccount));
    entry.setFieldVL(sfSigningPubKey, proposalSignature.getFieldVL(sfSigningPubKey));
    entry.setFieldVL(sfTxnSignature, proposalSignature.getFieldVL(sfTxnSignature));
    return entry;
}

TER
recordIntoSigners(STObject& slot, STObject const& proposalSignature, bool const singleSign)
{
    auto const hasSigners = slot.isFieldPresent(sfSigners);
    auto const existingKey =
        slot.isFieldPresent(sfSigningPubKey) ? slot.getFieldVL(sfSigningPubKey) : Blob{};
    auto const hasSingle = !existingKey.empty() && slot.isFieldPresent(sfTxnSignature);

    if (singleSign)
    {
        if (hasSigners)
            return tecNO_PERMISSION;
        if (hasSingle)
            return tecDUPLICATE;
        slot.setFieldVL(sfSigningPubKey, proposalSignature.getFieldVL(sfSigningPubKey));
        slot.setFieldVL(sfTxnSignature, proposalSignature.getFieldVL(sfTxnSignature));
        return tesSUCCESS;
    }

    if (hasSingle)
        return tecNO_PERMISSION;

    auto signers = hasSigners ? slot.getFieldArray(sfSigners) : STArray{sfSigners};
    auto const signerID = proposalSignature.getAccountID(sfAccount);
    if (accountPresent(signers, signerID))
        return tecDUPLICATE;
    if (signers.size() >= STTx::kMaxMultiSigners)
        return tecOVERSIZE;

    signers.push_back(makeSignerEntry(proposalSignature));
    sortByAccount(signers);
    slot.setFieldArray(sfSigners, signers);
    if (!slot.isFieldPresent(sfSigningPubKey))
        slot.setFieldVL(sfSigningPubKey, Slice{});
    return tesSUCCESS;
}

}  // namespace

bool
isValidProposal(STObject const& proposedTx)
{
    if (isProposalTx(proposedTx))
        return false;

    if (isPseudoTx(proposedTx))
        return false;

    if (proposedTx.isFieldPresent(sfFlags) &&
        (proposedTx.getFieldU32(sfFlags) & tfInnerBatchTxn) != 0u)
        return false;

    if (proposedTx.getFieldU16(sfTransactionType) == ttBATCH &&
        proposedTx.isFieldPresent(sfRawTransactions))
    {
        auto const& innerTxns = proposedTx.getFieldArray(sfRawTransactions);
        for (auto const& inner : innerTxns)
        {
            auto const tx = innerTxn(inner);
            if (isProposalTx(tx) || isPseudoTx(tx))
                return false;
        }
    }

    return true;
}

bool
isTerminal(
    ReadView const& view,
    std::optional<std::uint32_t> expiration,
    STObject const& proposedTx)
{
    if (hasExpired(view, expiration))
        return true;

    return proposedTx.isFieldPresent(sfLastLedgerSequence) &&
        view.seq() > proposedTx.getFieldU32(sfLastLedgerSequence);
}

TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j)
{
    XRPL_ASSERT(
        sleProposal && sleProposal->getType() == ltTRANSACTION_PROPOSAL &&
            view.exists(Keylet{ltTRANSACTION_PROPOSAL, sleProposal->key()}),
        "xrpl::proposal::deleteProposal : valid proposal sle of this view");

    auto const owner = sleProposal->getAccountID(sfOwner);

    auto const page = sleProposal->getFieldU64(sfOwnerNode);
    if (!view.dirRemove(keylet::ownerDir(owner), page, sleProposal->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete TransactionProposal from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(owner));
    if (!sleOwner)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Could not find TransactionProposal owner account root.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    decreaseOwnerCountForObject(
        view,
        sleOwner,
        sleProposal,
        proposalOwnerCount(sleProposal->getFieldObject(sfProposedTransaction)),
        j);

    view.erase(sleProposal);
    return tesSUCCESS;
}

bool
isRequiredSigningFor(STObject const& proposedTx, AccountID const& signingFor)
{
    if (isOuterSigningFor(proposedTx, signingFor))
        return true;

    if (proposedTx.getFieldU16(sfTransactionType) != ttBATCH ||
        !proposedTx.isFieldPresent(sfRawTransactions))
        return false;

    auto const outer = proposedTx.getAccountID(sfAccount);
    for (auto const& inner : proposedTx.getFieldArray(sfRawTransactions))
    {
        auto const tx = innerTxn(inner);
        if (tx.isFieldPresent(sfAccount) && tx.getAccountID(sfAccount) == signingFor &&
            signingFor != outer)
            return true;
        if (tx.isFieldPresent(sfDelegate) && tx.getAccountID(sfDelegate) == signingFor &&
            signingFor != outer)
            return true;
    }
    return false;
}

std::optional<Serializer>
signingData(
    STObject const& proposedTx,
    AccountID const& signingFor,
    AccountID const& signerAccount,
    Slice const& signingPubKey)
{
    auto const singleSign = signerAccount == signingFor;
    auto const forOuter = isOuterSigningFor(proposedTx, signingFor);

    // Hash the same canonical STTx the ordinary submit path will verify, not
    // the untyped nested object stored on the proposal.
    try
    {
        STTx stx{STObject{proposedTx}};

        if (stx.getTxnType() == ttBATCH && !forOuter)
        {
            Serializer msg;
            serializeBatch(
                msg,
                stx.getAccountID(sfAccount),
                stx.getSeqProxy().value(),
                stx.getFlags(),
                stx.getBatchTransactionIDs());
            if (singleSign)
            {
                finishMultiSigningData(signingFor, msg);
            }
            else
            {
                msg.addBitString(signingFor);
                finishMultiSigningData(signerAccount, msg);
            }
            return msg;
        }

        if (singleSign)
        {
            stx.setFieldVL(sfSigningPubKey, signingPubKey);
            Serializer s;
            s.add32(HashPrefix::TxSign);
            stx.addWithoutSigningFields(s);
            return s;
        }

        return buildMultiSigningData(stx, signerAccount);
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

TER
recordContribution(
    STObject& proposedTx,
    AccountID const& signingFor,
    STObject const& proposalSignature)
{
    auto const singleSign = proposalSignature.getAccountID(sfAccount) == signingFor;
    auto const forOuter = isOuterSigningFor(proposedTx, signingFor);

    if (forOuter)
        return recordIntoSigners(proposedTx, proposalSignature, singleSign);

    auto batchSigners = proposedTx.isFieldPresent(sfBatchSigners)
        ? proposedTx.getFieldArray(sfBatchSigners)
        : STArray{sfBatchSigners};

    if (auto* existing = findBatchSigner(batchSigners, signingFor))
    {
        if (auto const ret = recordIntoSigners(*existing, proposalSignature, singleSign);
            !isTesSuccess(ret))
            return ret;
        sortByAccount(batchSigners);
        proposedTx.setFieldArray(sfBatchSigners, batchSigners);
        return tesSUCCESS;
    }

    if (batchSigners.size() >= kMaxBatchSigners)
        return tecOVERSIZE;

    auto entry = STObject::makeInnerObject(sfBatchSigner);
    entry.setAccountID(sfAccount, signingFor);
    if (auto const ret = recordIntoSigners(entry, proposalSignature, singleSign);
        !isTesSuccess(ret))
        return ret;
    batchSigners.push_back(std::move(entry));
    sortByAccount(batchSigners);
    proposedTx.setFieldArray(sfBatchSigners, batchSigners);
    return tesSUCCESS;
}

}  // namespace xrpl::proposal
