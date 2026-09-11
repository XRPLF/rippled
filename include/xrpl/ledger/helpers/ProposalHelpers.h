#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
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
 * signature from: its Account or Delegate, an outer Counterparty or Sponsor
 * co-signer, or — for a Batch — an inner participant other than the outer
 * account.
 *
 * @param implicitCounterparty A caller-resolved Counterparty account for
 *        transaction types that infer one (a LoanSet without sfCounterparty
 *        defaults to LoanBroker.Owner, XLS-66 §3.8). nullopt when no
 *        resolution applies or the referent could not be found.
 */
bool
isRequiredSigningFor(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty = std::nullopt);

/**
 * Whether SigningFor names an account that contributes a signature at the
 * proposed transaction's own top level — its initiator (Delegate if
 * permission delegation is used, otherwise Account), or an outer
 * Counterparty / Sponsor co-signer. The initiator's contribution mirrors
 * STTx::getInitiator so it matches the signature Transactor::checkSign will
 * later look for on the ordinary submit path; the Counterparty and Sponsor
 * contributions authorize an XLS-66 counterparty or XLS-68 sponsor slot on
 * that same top-level transaction.
 *
 * All three targets are accounts the proposed transaction requires to exist
 * on the ledger (initiator by TransactionProposalCreate's target check,
 * sponsor by XLS-68's fee/reserve accounting, counterparty by the LoanSet
 * counterparty resolution), so a signer for any of these roles can never be
 * a phantom (uncreated) account. Inner-batch participants can be, which is
 * why "outer" and "inner-batch" are distinguished.
 */
bool
isOuterSigningFor(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty = std::nullopt);

/**
 * If SigningFor is contributing at the outer level in a co-signer role
 * (Counterparty or Sponsor), which role. Returns nullopt when SigningFor is
 * the proposed transaction's initiator (whose role is
 * SignatureRole::Transaction and does not need to be surfaced separately),
 * a batch inner participant, or not a required signer at all.
 *
 * When SigningFor matches more than one outer role (a Counterparty who is
 * also the Sponsor, for instance) this returns whichever role the routing
 * order picks first — Counterparty then Sponsor. Callers that must not
 * silently route to one slot when another is also expected should first
 * consult hasAmbiguousOuterRole.
 *
 * @param implicitCounterparty See isRequiredSigningFor.
 */
std::optional<SignatureRole>
auxiliaryRole(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty = std::nullopt);

/**
 * Whether SigningFor names more than one distinct outer role at the same
 * time — initiator plus a Counterparty / Sponsor slot, or Counterparty and
 * Sponsor both — for the proposed transaction. Under fixCleanup3_4_0 each
 * role signs a distinct payload (xrpl::signingPrefix), so a single
 * contribution cannot satisfy two slots; recording it into only one leaves
 * the other empty forever, and the proposal can never complete.
 *
 * The On-Chain Cosigner spec's §6.1.1 clause "if the same account fills
 * more than one role ... the same contribution is recorded in every
 * matching slot" predates that fix; TransactionProposalSign rejects the
 * ambiguous case with tecNO_PERMISSION until the two are reconciled (a
 * SigningForRole hint, or a spec edit).
 *
 * @param implicitCounterparty See isRequiredSigningFor.
 */
bool
hasAmbiguousOuterRole(
    STObject const& proposedTx,
    AccountID const& signingFor,
    std::optional<AccountID> const& implicitCounterparty = std::nullopt);

/**
 * The blob ProposalSignature.TxnSignature must be valid over for this
 * SigningFor / signer pair. The payload depends on where the contribution
 * lands:
 *  - the initiator of the proposed transaction (or the outer account of a
 *    Batch) signs the standard single- or multi-sign payload;
 *  - an outer Counterparty or Sponsor signs the same standard payload but
 *    under a role-specific HashPrefix (see xrpl::signingPrefix), so a
 *    contribution intended for one slot cannot be replayed into another
 *    once fixCleanup3_4_0 is enabled;
 *  - a Batch inner participant signs the XLS-56 batch signing payload,
 *    which binds the outer batch to the specific participant account.
 *
 * @param rules Current ledger rules. Determines the role HashPrefix under
 *        fixCleanup3_4_0.
 * @param implicitCounterparty See isRequiredSigningFor.
 * @return empty if the proposed transaction cannot be interpreted as signing
 *         data (a malformed Batch).
 */
std::optional<Serializer>
signingData(
    STObject const& proposedTx,
    AccountID const& signingFor,
    AccountID const& signerAccount,
    Slice const& signingPubKey,
    Rules const& rules,
    std::optional<AccountID> const& implicitCounterparty = std::nullopt);

/**
 * Record a validated ProposalSignature into the proposed transaction for
 * SigningFor. Mutates proposedTx in place. Callers must have already
 * verified the signature and the signer's authorization.
 *
 * Routes the contribution to the slot that matches SigningFor's role: the
 * outer Signers array (or top-level single-sign fields), the outer
 * sfCounterpartySignature / sfSponsorSignature slot, or a BatchSigners
 * entry, as returned by isOuterSigningFor / auxiliaryRole.
 *
 * @param implicitCounterparty See isRequiredSigningFor.
 * @return tesSUCCESS, tecDUPLICATE, tecNO_PERMISSION (mode conflict), or
 *         tecOVERSIZE.
 */
TER
recordContribution(
    STObject& proposedTx,
    AccountID const& signingFor,
    STObject const& proposalSignature,
    std::optional<AccountID> const& implicitCounterparty = std::nullopt);

}  // namespace xrpl::proposal
