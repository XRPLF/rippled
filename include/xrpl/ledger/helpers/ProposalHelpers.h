#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <optional>

namespace xrpl::proposal {

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
 * Whether the proposed transaction is itself a proposal transaction, which
 * would nest one proposal inside another.
 */
inline bool
isProposalTx(STObject const& proposedTx)
{
    auto const type = proposedTx.getFieldU16(sfTransactionType);
    return type == ttTRANSACTION_PROPOSAL_CREATE || type == ttTRANSACTION_PROPOSAL_SIGN;
}

/**
 * Whether the proposed transaction is independently submittable through the
 * ordinary multi-sign path: not a nested proposal, not a pseudo-transaction,
 * not itself flagged as someone else's inner batch transaction, and — if it
 * is a Batch — none of its own inner transactions is a nested proposal or a
 * pseudo-transaction either. A Batch inner transaction cannot itself be
 * pseudo (preflight0 rejects the pseudo/tfInnerBatchTxn combination
 * generically), but that guard lives outside this feature, so it is checked
 * again here rather than relied upon.
 */
bool
isValidProposal(STObject const& proposedTx);

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
 * Whether the proposal is terminal. A terminal proposal can never complete:
 * it stops accepting signatures and anyone may delete it.
 *
 * A proposal is terminal when either:
 * - its Expiration has passed (the parent ledger closed at or after it), or
 * - the proposed transaction carries a LastLedgerSequence that is strictly
 *   below the current ledger sequence (matching tefMAX_LEDGER: a transaction
 *   with LastLedgerSequence equal to the open ledger is still submittable).
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
 * proposal holds against the Owner, and erases the entry.
 */
TER
deleteProposal(ApplyView& view, SLE::pointer const& sleProposal, beast::Journal j);

/**
 * Whether SigningFor names an account the proposed transaction requires a
 * signature from: its Account or Delegate, or — for a Batch — an inner
 * participant other than the outer account.
 */
bool
isRequiredSigningFor(STObject const& proposedTx, AccountID const& signingFor);

/**
 * The blob ProposalSignature.TxnSignature must be valid over for this
 * SigningFor / signer pair. Ordinary (and Batch outer-account) contributions
 * use the standard single- or multi-sign payload; Batch participant
 * contributions use the XLS-56 batch signing payload.
 *
 * @return empty if the proposed transaction cannot be interpreted as signing
 *         data (a malformed Batch).
 */
std::optional<Serializer>
signingData(
    STObject const& proposedTx,
    AccountID const& signingFor,
    AccountID const& signerAccount,
    Slice const& signingPubKey);

/**
 * Record a validated ProposalSignature into the proposed transaction for
 * SigningFor. Mutates proposedTx in place. Callers must have already
 * verified the signature and the signer's authorization.
 *
 * @return tesSUCCESS, tecDUPLICATE, tecNO_PERMISSION (mode conflict), or
 *         tecOVERSIZE.
 */
TER
recordContribution(
    STObject& proposedTx,
    AccountID const& signingFor,
    STObject const& proposalSignature);

}  // namespace xrpl::proposal
