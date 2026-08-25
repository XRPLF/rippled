#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <optional>
#include <vector>

/**
 * @brief Helpers for constructing TransactionProposal test transactions.
 */
namespace xrpl::test::jtx::proposal {

// The owner-reserve increments a proposal holds against its proposer. Tests
// spend these rather than repeating their values, so they follow the transactor
// instead of checking it; TransactionProposalCreate_test pins the values
// themselves, so a change to them has to be a deliberate one.
using xrpl::proposal::kBatchProposalOwnerCount;
using xrpl::proposal::kProposalOwnerCount;

/**
 * @brief Build a TransactionProposalCreate carrying an unsigned proposed
 * transaction.
 *
 * @param proposer The account creating and paying the reserve for the proposal.
 * @param proposedTx The proposed transaction, in unsigned canonical form; see
 *        unsignedPayload().
 * @param expiration Absolute time after which the proposal may no longer be
 *        completed.
 * @return The TransactionProposalCreate JSON object.
 */
json::Value
create(Account const& proposer, json::Value const& proposedTx, std::uint32_t expiration);

/**
 * @brief Build a TransactionProposalCancel naming the proposal to delete.
 *
 * @param account The account requesting the cancellation.
 * @param proposalID The ID of the TransactionProposal to cancel.
 * @return The TransactionProposalCancel JSON object.
 */
json::Value
cancel(Account const& account, uint256 const& proposalID);
  
/*
 * @brief Conditions that check what a proposal transaction did to the ledger.
 *
 * Each is named for the generator it verifies, so a submission and its check
 * read as a pair, and the transactions that later sign or complete a proposal
 * get their own entries here rather than sharing one.
 */
namespace verify {

/**
 * @brief The condition returned by create(); see it for what is checked.
 */
class Create
{
public:
    /**
     * @throws std::logic_error if attached to another transaction type.
     */
    void
    operator()(Env&, JTx&) const;
};

/**
 * @brief Check the ledger effects a TransactionProposalCreate must have,
 * whatever its outcome: on tesSUCCESS a new proposal holding what was
 * submitted, listed in the proposer's directory and paid for by its reserve;
 * otherwise nothing moved. Nothing of the target's moves either way.
 *
 * @code
 * env(proposal::create(alice, payload, expiration), proposal::verify::create());
 * @endcode
 */
[[nodiscard]] inline Create
create()
{
    return Create{};
}

}  // namespace verify

/**
 * @brief Put a transaction of any type into the form a proposal stores it in:
 * unsigned and ticket-based, with the fee the target account will pay fixed
 * now.
 *
 * This takes whatever any jtx generator produces — @c pay(), @c loan::set(),
 * @c sponsor::transfer(), an outer @c Batch — and applies only what the
 * proposal itself demands of the payload, so a test never hand-rolls those
 * fields. It leaves the rest of @p tx untouched, which is what lets a test
 * build one valid payload and then break exactly one rule of it.
 *
 * The payload is ticket-based so unrelated activity on the target account
 * cannot invalidate it while signatures are collected. A Fee already set on
 * @p tx is left alone, so a payload whose fee is not the base fee — a Batch,
 * say — can carry its own; otherwise the fee is filled in the same way as for
 * an ordinary submission.
 *
 * @param env The test environment providing ledger fee settings.
 * @param tx The transaction to propose.
 * @param ticketSeq A ticket sequence owned by @p tx's account.
 * @return The proposed transaction JSON object.
 */
json::Value
unsignedPayload(Env const& env, json::Value tx, std::uint32_t ticketSeq);

/**
 * @brief Put a transaction into the form an inner transaction of a proposed
 * Batch takes, as @c batch::Inner does for an ordinary Batch.
 *
 * @param tx The transaction to nest.
 * @param seq The sequence number of @p tx's own account.
 * @return The inner transaction JSON object.
 */
json::Value
innerTx(json::Value tx, std::uint32_t seq);

/**
 * @brief An unsigned outer Batch payload holding @p inners.
 *
 * A proposed Batch stores no BatchSigners — the participants' signatures are
 * collected on-ledger afterwards — but its fee is fixed now and must already
 * cover them. By default one signer is assumed for each inner account other
 * than @p target, which is what those participants will contribute.
 *
 * @param env The test environment providing ledger fee settings.
 * @param target The outer account of the Batch, and the proposal's target.
 * @param ticketSeq A ticket sequence owned by @p target.
 * @param flags The Batch mode flags, e.g. @c tfAllOrNothing.
 * @param inners The inner transactions; see innerTx().
 * @param numSigners Overrides the number of signatures the fee accounts for.
 * @return The proposed Batch JSON object.
 */
json::Value
unsignedBatch(
    Env const& env,
    Account const& target,
    std::uint32_t ticketSeq,
    std::uint32_t flags,
    std::vector<json::Value> const& inners,
    std::optional<std::uint32_t> numSigners = std::nullopt);

/**
 * @brief Give @p proposer a place on @p target's SignerList, so it may
 * create proposals against @p target.
 *
 * Only the target account itself, or an account on its SignerList, may
 * create a TransactionProposalCreate against it (On-Chain Cosigner V1
 * authorization). Sets a minimal one-signer, quorum-1 list; the quorum
 * itself is irrelevant here since it only governs the proposed
 * transaction's own completion, not who may propose it.
 *
 * @param env The test environment.
 * @param target The account whose SignerList is set.
 * @param proposer The account to add to it.
 */
void
authorizeProposer(Env& env, Account const& target, Account const& proposer);

/**
 * @brief Create tickets for a proposal to be built against, and close the
 * ledger.
 *
 * @param env The test environment.
 * @param account The account that will own the tickets, i.e. the target of the
 *        proposals to come.
 * @param count How many tickets to create.
 * @return The first ticket sequence created; the rest follow it.
 */
std::uint32_t
createTicket(Env& env, Account const& account, std::uint32_t count = 1);

/**
 * @brief An absolute expiration @p delta past the environment's current time.
 *
 * @c expiration(env, 0s) is an expiration that has already passed.
 */
std::uint32_t
expiration(Env& env, NetClock::duration delta);

/**
 * @brief The proposal stored against a target account's ticket.
 * @return empty if no such proposal exists.
 */
[[nodiscard]] SLE::const_pointer
entry(Env const& env, AccountID const& target, std::uint32_t ticketSeq);
[[nodiscard]] SLE::const_pointer
entry(Env const& env, Account const& target, std::uint32_t ticketSeq);

}  // namespace xrpl::test::jtx::proposal
