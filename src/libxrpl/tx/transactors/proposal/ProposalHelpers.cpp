#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep (range-for over getFieldArray)
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <optional>

namespace xrpl::proposal {

bool
isProposalTx(STObject const& proposedTx)
{
    auto const isProposalType = [](std::uint16_t type) {
        return type == ttTRANSACTION_PROPOSAL_CREATE || type == ttTRANSACTION_PROPOSAL_CANCEL;
    };

    auto const type = proposedTx.getFieldU16(sfTransactionType);
    if (isProposalType(type))
        return true;

    if (type == ttBATCH && proposedTx.isFieldPresent(sfRawTransactions))
    {
        for (STObject const& inner : proposedTx.getFieldArray(sfRawTransactions))
        {
            if (!inner.isFieldPresent(sfTransactionType))
                continue;
            auto const innerType = inner.getFieldU16(sfTransactionType);
            if (isProposalType(innerType) || innerType == ttBATCH)
                return true;
        }
    }

    return false;
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
        view.seq() >= proposedTx.getFieldU32(sfLastLedgerSequence);
}

TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j)
{
    // view carries no null contract (a reference), but the two parameters are
    // bound to each other: sleProposal must be a live entry of this same
    // view, since the directory removal, owner-root peek, and erase below all
    // mutate that view assuming they see the entry's state.
    XRPL_ASSERT(
        sleProposal && sleProposal->getType() == ltTRANSACTION_PROPOSAL &&
            view.exists(Keylet{ltTRANSACTION_PROPOSAL, sleProposal->key()}),
        "xrpl::proposal::deleteProposal : valid proposal sle of this view");

    AccountID const owner = sleProposal->getAccountID(sfOwner);

    std::uint64_t const page{(*sleProposal)[sfOwnerNode]};
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

    // Release the reserve against the Owner or, if the entry carries a
    // reserve sponsor, against that sponsor.
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
payloadMatches(STObject const& proposedTx, STObject const& tx)
{
    XRPL_ASSERT(
        proposedTx.isFieldPresent(sfTransactionType) && tx.isFieldPresent(sfTransactionType),
        "xrpl::proposal::payloadMatches : transaction-shaped inputs");

    // The signing-payload serialization omits exactly the signature
    // containers a proposal lets evolve (TxnSignature, Signers, BatchSigners,
    // CounterpartySignature, SponsorSignature), so it is the field set fixed
    // at creation — except SigningPubKey, which signing payloads include but
    // which is also mutable here (stored empty, filled if the target signs
    // with its own key). Neutralize it on both sides before comparing.
    auto const payloadBytes = [](STObject const& obj) {
        STObject copy{obj};
        copy.setFieldVL(sfSigningPubKey, Slice{});
        Serializer s;
        copy.addWithoutSigningFields(s);
        return s.getData();
    };

    return payloadBytes(proposedTx) == payloadBytes(tx);
}

bool
mayConsumeReservedTicket(SLE const& sleProposal, STTx const& tx)
{
    XRPL_ASSERT(
        sleProposal.getType() == ltTRANSACTION_PROPOSAL,
        "xrpl::proposal::mayConsumeReservedTicket : a TransactionProposal entry");

    return payloadMatches(sleProposal.getFieldObject(sfProposedTransaction), tx);
}

}  // namespace xrpl::proposal
