#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Executes the AccountDelete transaction on the XRP Ledger.
 *
 *  AccountDelete is the most structurally complex transactor in the system:
 *  it tears down an entire account by erasing every owned ledger object,
 *  transferring the remaining XRP balance to a destination, and finally
 *  removing the account root SLE.
 *
 *  `kCONSEQUENCES_FACTORY = Blocker` prevents the transaction queue from
 *  scheduling any transaction from the same account behind a pending delete,
 *  because deletion clears sequence numbers and owned objects, making any
 *  queued follower semantically undefined.
 *
 *  @note An account can only be deleted if it satisfies all of:
 *      its sequence number is at least 256 below the current ledger index,
 *      `FirstNFTokenSequence + MintedNFTokens + 255` does not exceed the
 *      current ledger index, it has no live NFTs and no outstanding obligations
 *      (trust lines, escrows, etc.), and the owner directory contains at most
 *      `maxDeletableDirEntries` entries.
 *
 *  @see Transactor
 */
class AccountDelete : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Blocker;

    explicit AccountDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on the `featureCredentials` amendment.
     *
     *  If `sfCredentialIDs` is present in the transaction, this method
     *  returns `false` unless the `featureCredentials` amendment is enabled,
     *  causing `invokePreflight` to return `temDISABLED`.  Called by the
     *  preflight framework before any field-level validation.
     *
     *  @param ctx  The preflight context carrying the transaction and rules.
     *  @return `true` if the transaction may proceed to `preflight`; `false`
     *      if the required amendment is not yet active.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Perform stateless early rejection of the transaction.
     *
     *  Rejects self-sends (`sfAccount == sfDestination` → `temDST_IS_SRC`)
     *  and validates the format of any `sfCredentialIDs` field via
     *  `credentials::checkFields`.  No ledger state is consulted.
     *
     *  @param ctx  The preflight context carrying the transaction and rules.
     *  @return `tesSUCCESS` if the transaction passes static checks, or a
     *      `tem*` code describing the first validation failure.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Compute the fee as one owner-reserve unit.
     *
     *  Overrides the standard reference-fee calculation to call
     *  `calculateOwnerReserveFee()`, pricing deletion at one reserve
     *  increment.  This makes the operation economically meaningful and
     *  discourages spam.
     *
     *  @param view  Read-only view used to look up current reserve settings.
     *  @param tx    The AccountDelete transaction.
     *  @return The required fee in drops (one owner-reserve unit).
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Validate ledger state against all deletion preconditions.
     *
     *  Performs every stateful check required before account deletion may
     *  proceed.  Checks are applied in this order:
     *
     *  1. Destination account must exist (`tecNO_DST`).
     *  2. Destination tag must be supplied if required by the destination
     *     (`tecDST_TAG_NEEDED`).
     *  3. If `sfCredentialIDs` are absent, the destination's `lsfDepositAuth`
     *     flag must be satisfied by a pre-authorized entry; if credentials are
     *     present this check is deferred to `doApply` so that expiry is caught
     *     at apply-time rather than claim-time.
     *  4. The account must have no live NFTs (minted ≠ burned → `tecHAS_OBLIGATIONS`).
     *  5. The account's sequence must be ≥ 256 below the current ledger index
     *     to prevent transaction replay after account resurrection (`tecTOO_SOON`).
     *  6. `FirstNFTokenSequence + MintedNFTokens + 255` must not exceed the
     *     current ledger index, preventing duplicate NFTokenIDs via authorized
     *     minters after resurrection (`tecTOO_SOON`).
     *  7. Every entry in the owner directory must have a registered deleter
     *     (i.e. be a non-obligation type such as an offer, ticket, or signer
     *     list); any unrecognized type returns `tecHAS_OBLIGATIONS`.
     *  8. The owner directory must not exceed `maxDeletableDirEntries`
     *     entries (`tefTOO_BIG`).
     *
     *  @param ctx  The preclaim context carrying a read-only ledger view.
     *  @return `tesSUCCESS` if all conditions are met, or the appropriate
     *      `tec`/`tef` code describing the first unmet condition.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the account teardown and XRP transfer.
     *
     *  Assumes `preclaim` has already validated ledger state and proceeds to:
     *
     *  1. If `sfCredentialIDs` are present, call `verifyDepositPreauth` to
     *     confirm authorization and detect expired credentials (deferred from
     *     `preclaim`).
     *  2. Walk the owner directory via `cleanupOnAccountDelete`, invoking the
     *     appropriate deleter for each entry.
     *  3. Transfer the remaining XRP balance to the destination and call
     *     `ctx_.deliver()` to record the delivered amount.
     *  4. Erase the (now empty) owner directory root node; a non-empty
     *     directory at this point is treated as a ledger integrity error.
     *  5. Clear `lsfPasswordSpent` on the destination if XRP was received
     *     and the flag was set.
     *  6. Erase the source account root SLE.
     *
     *  @return `tesSUCCESS` on successful deletion, or a `tec`/`tef` code
     *      if an unexpected error is encountered during cleanup.
     */
    TER
    doApply() override;

    /** Accumulate per-entry state for transaction-specific invariant checks.
     *
     *  Currently a no-op placeholder; AccountDelete relies on the protocol-
     *  level invariants (`AccountRootsNotDeleted`, `AccountRootsDeletedClean`,
     *  `XRPNotCreated`, etc.) rather than defining additional per-entry checks.
     *  Reserved for future transaction-specific invariants.
     *
     *  @param isDelete  `true` if the entry was erased.
     *  @param before    SLE state before the transaction (nullptr if new).
     *  @param after     SLE state after the transaction (nullptr for deletions
     *      is not guaranteed; use `isDelete` to detect deletions).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Verify transaction-specific post-conditions after all entries are visited.
     *
     *  Currently a no-op placeholder that always returns `true`.
     *  AccountDelete's correctness is enforced by the protocol-level invariant
     *  checkers.  Reserved for future transaction-specific invariants.
     *
     *  @param tx     The transaction being applied.
     *  @param result The tentative TER result so far.
     *  @param fee    The fee consumed by the transaction.
     *  @param view   Read-only view of the ledger after the transaction.
     *  @param j      Journal for logging invariant failures.
     *  @return Always `true`; no transaction-specific invariants are defined yet.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
