#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>

namespace xrpl {

/**
 * Whether the proposed transaction is itself a proposal transaction, which
 * would nest one proposal inside another.
 *
 * TODO: cover ttTRANSACTION_PROPOSAL_SIGN and ttTRANSACTION_PROPOSAL_CANCEL
 * once those transactions exist.
 */
inline bool
isProposalTx(STObject const& proposedTx)
{
    return proposedTx.getFieldU16(sfTransactionType) == ttTRANSACTION_PROPOSAL_CREATE;
}

/**
 * Whether the proposed transaction carries any signature field.
 *
 * A proposal is stored in unsigned canonical form; signatures may only ever
 * arrive through TransactionProposalSign. Shared by the create-time check and
 * the invariant that guards the stored entry, so the two cannot drift apart.
 */
inline bool
hasSignatureField(STObject const& proposedTx)
{
    return proposedTx.isFieldPresent(sfTxnSignature) || proposedTx.isFieldPresent(sfSigners) ||
        proposedTx.isFieldPresent(sfBatchSigners) ||
        proposedTx.isFieldPresent(sfCounterpartySignature) ||
        proposedTx.isFieldPresent(sfSponsorSignature);
}

/**
 * Whether the proposed transaction's SigningPubKey is present and empty, as
 * unsigned canonical form requires. An absent field is not the same as an
 * empty one, and a populated one means the payload was already signed.
 */
inline bool
hasEmptySigningPubKey(STObject const& proposedTx)
{
    return proposedTx.isFieldPresent(sfSigningPubKey) &&
        proposedTx.getFieldVL(sfSigningPubKey).empty();
}

/**
 * Owner-reserve increments held by a proposal of an ordinary transaction.
 */
constexpr std::uint32_t kProposalOwnerCount = 5;

/**
 * Owner-reserve increments held by a proposal of a Batch transaction. A
 * proposed Batch stores up to eight inner transactions plus multi-account
 * signatures, so it reserves more than an ordinary proposed transaction.
 */
constexpr std::uint32_t kBatchProposalOwnerCount = 10;

/**
 * Owner-reserve increments held by a proposal of the given transaction.
 */
inline std::uint32_t
proposalOwnerCount(STObject const& proposedTx)
{
    return proposedTx.getFieldU16(sfTransactionType) == ttBATCH ? kBatchProposalOwnerCount
                                                                : kProposalOwnerCount;
}

/**
 * Delete a TransactionProposal ledger entry.
 *
 * Removes the entry from its Owner's directory, releases the reserve the
 * proposal holds against the Owner, and erases the entry. Shared by every
 * deletion path the spec defines (XLS-0103 §4.5): automatic cleanup when the
 * proposed transaction's TicketSequence is consumed, and the
 * TransactionProposalCancel / TransactionProposalSign cleanup paths once
 * those transactions exist.
 *
 * A TransactionProposal cannot carry a reserve sponsor today (its type is
 * not sponsorship-supported), so the release always lands on the Owner; it
 * goes through decreaseOwnerCountForObject regardless, matching ticketDelete,
 * so it would follow an sfSponsor recorded on the entry if the type ever
 * becomes sponsorable.
 *
 * @param view The apply view for making changes
 * @param sleProposal The TransactionProposal ledger entry to delete
 * @param j Journal for logging
 * @return tesSUCCESS, or tefBAD_LEDGER if the ledger contradicts the entry
 */
TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j);

}  // namespace xrpl
