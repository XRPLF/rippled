#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl::proposal {

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

}  // namespace xrpl::proposal
