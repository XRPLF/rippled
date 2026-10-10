#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace xrpl::proposal {

/**
 * Why a required authorization on a TransactionProposal is not currently
 * satisfied (XLS-0103 §8.1.2).
 */
enum class UnsignedReason : std::uint8_t {
    /**
     * Nothing has been collected for a single-signing account, or the shares
     * collected for a multi-signing account do not reach its live quorum.
     */
    InadequateSignatures,
    /**
     * A collected Signers entry is not authorized by the live SignerList, so
     * submission would reject the whole set.
     */
    InvalidSignerSet,
    /**
     * Signers were collected, but the account has no SignerList on this
     * ledger.
     */
    NoSignerList,
    /**
     * A master-key signature was collected and the master key has since been
     * disabled.
     */
    MasterDisabled,
    /**
     * The signing key does not currently authorize the account, e.g. a
     * rotated regular key.
     */
    NotAuthorized,
    /**
     * The account does not exist on this ledger.
     */
    AccountNotFound,
    /**
     * Sponsor rows only: a Sponsorship entry exists, but its flags require a
     * co-signature for what the transaction sponsors.
     */
    AwaitingSponsorshipSignature,
    /**
     * Sponsor rows, and the participant row of a co-signing inner Sponsor:
     * the sponsorship is one submission rejects outright (reserve
     * sponsorship of a delegated transaction), so no signature or Sponsorship
     * entry can ever satisfy it. Cancel and recreate.
     */
    InvalidSponsorship,
    /**
     * The Sponsor of a Batch inner transaction that does not co-sign: no
     * Sponsorship entry pre-authorizes it, or the entry's flags demand a
     * co-signature the proposal has no slot to collect. The sponsor must
     * create or relax the entry, or the proposal must be recreated.
     */
    SponsorshipEntryRequired,
    /**
     * The required co-signer cannot be named: a LoanSet without a
     * Counterparty names a LoanBroker that no longer exists. Permanent, so
     * the row carries no account; cancel and recreate.
     */
    CounterpartyUnresolvable,
    /**
     * The row of a Delegate, of the proposed transaction or of one of a
     * proposed Batch's inner transactions, that does not hold permission for
     * it (xrpl::invokeCheckPermission, granular semantics included), so the
     * transaction cannot apply whatever has been collected.
     */
    NoDelegatePermission,
    /**
     * The row of a Batch inner transaction's authorizer whose inner cannot
     * take its turn at submission: its Sequence is not the one its account
     * will hold then, its TicketSequence names no Ticket, its AccountTxnID
     * does not match, or its LastLedgerSequence has passed
     * (Transactor::checkSeqProxy, checkPriorTxAndLastLedger), or a Ticket
     * this Batch itself spends first. Nothing collectable changes that;
     * recreate the proposal.
     */
    SequenceMismatch,
    /**
     * The row of a Batch inner transaction's authorizer whose inner spends a
     * Ticket another live proposal reserves for its own transaction
     * (proposal::canConsumeTicket). Cancel that proposal, or recreate this
     * one.
     */
    TicketReserved,
    /**
     * The stored signature material is not usable. Defensive: unreachable
     * through TransactionProposalSign.
     */
    Malformed,
};

/**
 * The wire name of a reason, e.g. "inadequate_signatures".
 *
 * @param reason The reason to name.
 * @return The name XLS-0103 §8.1.2 assigns it.
 */
std::string_view
toString(UnsignedReason reason);

/**
 * One member of an account's live SignerList, and whether a signature from it
 * that is currently valid has been collected.
 */
struct SignerListMemberStatus
{
    AccountID account;
    std::uint32_t weight = 0;
    bool hasSigned = false;
};

/**
 * One required authorization on a proposal (XLS-0103 §8.1.3.1) and whether
 * the signature material collected so far currently satisfies it.
 */
struct AuthorizationStatus
{
    /**
     * The account whose authorization is required. Absent only when it
     * cannot be named (UnsignedReason::CounterpartyUnresolvable) or when a
     * rejected BatchSigners entry carries no account.
     */
    std::optional<AccountID> account;
    /**
     * Whether the collected material authorizes the account on the queried
     * ledger, by the rule submission applies (Transactor::checkSign).
     */
    bool satisfied = false;
    /**
     * Why not; present exactly when `satisfied` is false.
     */
    std::optional<UnsignedReason> reason;
    /**
     * Weight of the collected Signers entries that the live SignerList
     * currently authorizes. Present only when a Signers array has been
     * collected for this account.
     */
    std::optional<std::uint32_t> signedWeight;
    /**
     * The account's live SignerQuorum. Present only when it has a SignerList.
     */
    std::optional<std::uint32_t> quorum;
    /**
     * The live SignerList's members. Present only when the account has one
     * that deserializes.
     */
    std::optional<std::vector<SignerListMemberStatus>> signers;
};

/**
 * Where a proposal is in its lifecycle (XLS-0103 §8.1.2).
 */
enum class ProposalState : std::uint8_t { Pending, Complete, Expired };

/**
 * The wire name of a state, e.g. "pending".
 *
 * @param state The state to name.
 * @return The name XLS-0103 §8.1.2 assigns it.
 */
std::string_view
toString(ProposalState state);

/**
 * A proposal's completeness on one ledger.
 */
struct ProposalStatus
{
    /**
     * Terminal-first: an expired proposal reports expired even when every
     * authorization is satisfied.
     */
    ProposalState state = ProposalState::Pending;
    /**
     * Whether every required authorization is satisfied. Independent of
     * expiry: an expired proposal that reached quorum still holds a
     * submittable transaction (XLS-0103 §13.4).
     */
    bool authorized = false;
    /**
     * One entry per required authorization, in the order XLS-0103 §8.1.2
     * fixes: the account (or Delegate), then the Counterparty, then the
     * Sponsor, then each Batch participant in ascending account order. Two
     * kinds of entry follow them, so that `complete` keeps meaning that every
     * entry is satisfied: the Sponsor of each inner transaction that does not
     * co-sign, which only a Sponsorship entry can authorize, and a `malformed`
     * entry for each BatchSigners entry submission would reject outright (one
     * the Batch does not require, a duplicate, or one out of order).
     */
    std::vector<AuthorizationStatus> authorizations;
};

/**
 * Evaluate how far a TransactionProposal's collected signatures are from a
 * submittable transaction on the given ledger (XLS-0103 §8.1.3).
 *
 * Each signature was cryptographically verified when TransactionProposalSign
 * appended it, so only its authorization is re-checked, against live ledger
 * state: SignerList membership and quorum, regular-key rotation, disabled
 * master keys, account existence. The verdict for each required account comes
 * from Transactor::checkSign, the rule submission applies, so it cannot drift
 * from what a submitted copy of the proposed transaction would meet; the
 * weights and per-member flags are progress detail derived alongside it.
 *
 * The required accounts mirror submission: the proposed transaction's Account
 * or, when present, its Delegate; its Counterparty, which for a LoanSet
 * without one is the owner of the LoanBroker it names (LoanSet::checkSign);
 * its Sponsor, who may instead be pre-authorized by a Sponsorship entry
 * (Transactor::checkSponsor); and, for a Batch, every participant that
 * Batch::preflightSigValidated requires a BatchSigners entry from.
 *
 * @param view The ledger to evaluate against.
 * @param sleProposal A TransactionProposal entry of that ledger.
 * @param j Journal for logging.
 * @return The proposal's state and the status of each required authorization.
 */
ProposalStatus
evaluateProposal(ReadView const& view, SLE const& sleProposal, beast::Journal j);

}  // namespace xrpl::proposal
