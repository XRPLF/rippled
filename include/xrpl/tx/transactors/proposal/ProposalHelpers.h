#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <cstdint>
#include <optional>
#include <vector>

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

/**
 * The role in which an account's authorization is required on a proposal.
 */
enum class SignerRole : std::uint8_t {
    account,            ///< the proposed transaction's initiator (Account, or Delegate if present)
    batchParticipant,   ///< an inner transaction's initiator in a proposed Batch
    counterparty,       ///< a Counterparty of the proposed transaction or of an inner
    sponsor,            ///< a co-signing Sponsor of the proposed transaction or of an inner
};

/**
 * One required authorization on a proposal and whether the signatures
 * collected so far currently satisfy it.
 */
struct SignerStatus
{
    AccountID account;
    SignerRole role;
    /// Whether the collected signature material authorizes `account` on the
    /// evaluated ledger (same rule the transaction path applies at preclaim).
    bool satisfied = false;
    /// Multi-signature progress: the weight the collected Signers entries
    /// carry against `account`'s live SignerList. Present only when the
    /// collected signature object holds a Signers array.
    std::optional<std::uint32_t> signedWeight;
    /// `account`'s live SignerQuorum. Present only when the account has a
    /// SignerList on the evaluated ledger.
    std::optional<std::uint32_t> quorum;
};

/**
 * Completeness state of a proposal (XLS-0103 §8.1.2). Terminal-first: an
 * expired proposal reports expired even when fully signed.
 */
enum class ProposalState : std::uint8_t { pending, complete, expired };

/**
 * A proposal's completeness state plus the per-account detail it derives
 * from.
 */
struct ProposalStatus
{
    ProposalState state = ProposalState::pending;
    std::vector<SignerStatus> signers;
};

/**
 * Evaluate how far a TransactionProposal's collected signatures are from a
 * submittable transaction on the given ledger.
 *
 * Signatures stored on the proposal were cryptographically verified when they
 * were appended, so this only re-checks their authorization against live
 * ledger state (SignerList membership and quorum, regular-key rotation,
 * disabled master keys), mirroring what Transactor::checkSign would decide at
 * submission time. For a proposed Batch, each inner initiator, counterparty,
 * and co-signing sponsor other than the outer account is a separate required
 * authorization collected through BatchSigners, mirroring
 * Batch::preflightSigValidated. A LoanSet without an explicit Counterparty
 * requires the owner of its LoanBroker instead, mirroring LoanSet::checkSign.
 *
 * @param view The ledger to evaluate against.
 * @param sleProposal A TransactionProposal ledger entry of that ledger.
 * @param j Journal for logging.
 */
ProposalStatus
evaluateProposal(ReadView const& view, SLE const& sleProposal, beast::Journal j);

}  // namespace xrpl::proposal
