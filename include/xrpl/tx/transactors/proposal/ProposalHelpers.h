#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <optional>

namespace xrpl::proposal {

/**
 * Whether the proposed transaction is a proposal transaction, or a Batch
 * containing one. A proposal must not nest another proposal (On-Chain
 * Cosigner spec §5.3.1), and without the one-level walk into a proposed
 * Batch's inner transactions, a proposal transaction could be hidden there.
 * TODO: cover ttTRANSACTION_PROPOSAL_SIGN once that transaction exists.
 */
bool
isProposalTx(STObject const& proposedTx);

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
 * Whether the proposal is terminal (XLS-0103 §4.5). A terminal proposal can
 * never complete: it stops accepting signatures and anyone may delete it.
 *
 * A proposal is terminal when either:
 * - its Expiration has passed (the parent ledger closed at or after it), or
 * - the proposed transaction carries a LastLedgerSequence that is at or
 *   below the current ledger sequence. This matches the dead-on-arrival
 *   check in TransactionProposalCreate::preclaim: a proposal whose bound
 *   leaves no future ledger to collect signatures in is already dead.
 *
 * @param view The ledger the deciding transaction is being applied to.
 * @param expiration The proposal's Expiration field.
 * @param proposedTx The proposal's ProposedTransaction field.
 */
bool
isTerminal(
    ReadView const& view,
    std::optional<std::uint32_t> expiration,
    STObject const& proposedTx);

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
 * @return tesSUCCESS, tecINTERNAL for an invalid entry, or tefBAD_LEDGER if
 *         the ledger contradicts the entry
 */
TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j);

/**
 * Whether tx carries the same payload as a proposal's stored
 * ProposedTransaction (XLS-0103 §4.2.1).
 *
 * The payload is every field fixed at creation — everything except the
 * signature containers the proposal lets evolve (TxnSignature, Signers,
 * BatchSigners, CounterpartySignature, SponsorSignature) and SigningPubKey,
 * which is stored empty and filled only if the target signs with its own
 * key. So the completed transaction matches no matter which mix of collected
 * or off-ledger signatures it carries, and any change to a non-signature
 * field does not.
 *
 * @param proposedTx The proposal's ProposedTransaction field.
 * @param tx The transaction to compare, typically an STTx.
 */
bool
payloadMatches(STObject const& proposedTx, STObject const& tx);

/**
 * Whether tx may consume its Ticket.
 *
 * While a proposal keyed to the Ticket exists, the target's Ticket is
 * reserved: only the proposal's own proposed transaction may spend it
 * (XLS-0103 §4.2.1), so that unrelated target-account activity cannot
 * invalidate the proposal while signatures are being collected.
 * TransactionProposalCancel is not allowed through: it deletes the
 * reservation without consuming the ticket.
 *
 * @param view The ledger containing the Ticket and any reservation.
 * @param tx The transaction attempting to consume that ticket.
 */
bool
canConsumeTicket(ReadView const& view, STTx const& tx);

}  // namespace xrpl::proposal
