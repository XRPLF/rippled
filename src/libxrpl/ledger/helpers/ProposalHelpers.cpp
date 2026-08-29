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
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace xrpl::proposal {

namespace {

// Proposals do not nest, so a proposed transaction may carry none of these
// types, neither as its own payload nor as a Batch inner transaction.
constexpr auto kProposalTxTypes = std::to_array<TxType>({
    ttTRANSACTION_PROPOSAL_CREATE,
    ttTRANSACTION_PROPOSAL_CANCEL,
});

}  // namespace

bool
isValidProposalTxnType(STObject const& proposedTx)
{
    auto const type = proposedTx.getFieldU16(sfTransactionType);

    if (std::ranges::find(kProposalTxTypes, type) != kProposalTxTypes.end())
        return false;

    if (isPseudoTx(proposedTx))
        return false;

    if (proposedTx.isFieldPresent(sfFlags) &&
        (proposedTx.getFieldU32(sfFlags) & tfInnerBatchTxn) != 0u)
        return false;

    if (type == ttBATCH && proposedTx.isFieldPresent(sfRawTransactions))
    {
        STArray const& innerTxns = proposedTx.getFieldArray(sfRawTransactions);
        for (STObject const& inner : innerTxns)
        {
            // The only production caller reaches this walk through STTx
            // construction, which rejects a typeless inner first, so this is
            // re-checked rather than relied upon.
            if (!inner.isFieldPresent(sfTransactionType))
                return false;

            auto const innerType = inner.getFieldU16(sfTransactionType);

            // A nested Batch is rejected alongside nested proposals and
            // pseudo-transactions: STTx construction forbids inner Batches, so
            // a proposal must not be able to store one either.
            if (std::ranges::find(kProposalTxTypes, innerType) != kProposalTxTypes.end() ||
                innerType == ttBATCH || isPseudoTx(inner))
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
        view.seq() >= proposedTx.getFieldU32(sfLastLedgerSequence);
}

TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j)
{
    if (!sleProposal || sleProposal->getType() != ltTRANSACTION_PROPOSAL ||
        !view.exists(Keylet{ltTRANSACTION_PROPOSAL, sleProposal->key()}))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Invalid TransactionProposal deletion.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

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
canConsumeTicket(ReadView const& view, STTx const& tx)
{
    auto const seqProx = tx.getSeqProxy();
    if (!seqProx.isTicket())
    {
        // The caller only reaches here for a Ticket-based transaction.
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::proposal::canConsumeTicket : tx spends a Sequence");
        return false;
        // LCOV_EXCL_STOP
    }

    auto const sleProposal =
        view.read(keylet::txProposal(tx.getAccountID(sfAccount), seqProx.value()));
    if (!sleProposal)
        return true;

    return payloadMatches(sleProposal->getFieldObject(sfProposedTransaction), tx);
}

}  // namespace xrpl::proposal
