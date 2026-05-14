/**
 * @file
 * @brief Transactor for the CheckCancel transaction type.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Removes a Check ledger object without transferring value.
 *
 * `CheckCancel` completes the Check lifecycle alongside `CheckCreate` (which
 * writes the on-ledger object and reserves owner funds) and `CheckCash` (which
 * redeems it). It is the only path for reclaiming the source account's owner
 * reserve when a check goes unused.
 *
 * Permission model: if the check has **not** yet expired (tested against the
 * parent ledger's close time), only the source account or the destination
 * account may cancel it. An expired check may be removed by any account,
 * allowing third-party cleanup of stale objects.
 *
 * `ConsequencesFactory` is `Normal` — no special blocking semantics. Unlike
 * `CheckCreate` and `CheckCash`, this transactor does not override
 * `checkExtraFeatures` because cancellation is always permitted regardless of
 * active amendments.
 */
class CheckCancel : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    /** Constructs a `CheckCancel` transactor bound to the given apply context. */
    explicit CheckCancel(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Stateless validation phase — always succeeds.
     *
     * All meaningful validation (Check existence, canceller permission) is
     * deferred to `preclaim`, which has read access to ledger state.
     *
     * @param ctx  The preflight context (no ledger access).
     * @return `tesSUCCESS` unconditionally.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Ledger-state validation phase.
     *
     * Resolves the Check object via `sfCheckID`. Enforces the permission rule:
     * if the check has not expired, only the source or destination account may
     * cancel it. Expiry is evaluated against the **parent** ledger's close time
     * (the only definitively known close time at apply time).
     *
     * @param ctx  The preclaim context providing a read-only ledger view.
     * @return `tesSUCCESS` if the caller is permitted to cancel.
     * @return `tecNO_ENTRY` if the Check object does not exist.
     * @return `tecNO_PERMISSION` if the check is unexpired and the submitter
     *     is neither the source nor the destination account.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Commits the cancellation to the ledger.
     *
     * Performs four coordinated writes in order:
     * 1. Removes the Check from the destination account's owner directory
     *    using the stored `sfDestinationNode` page index (skipped when source
     *    and destination are the same account).
     * 2. Removes the Check from the source account's owner directory using
     *    the stored `sfOwnerNode` page index.
     * 3. Decrements the source account's owner count by 1 via
     *    `adjustOwnerCount`, releasing the reserve that `CheckCreate` claimed.
     * 4. Erases the Check SLE from the ledger.
     *
     * The two `dirRemove` calls are guarded by `tefBAD_LEDGER` paths
     * (marked `LCOV_EXCL`) — the stored page indices are immutable after
     * creation and `preclaim` has already confirmed the Check exists, so
     * those branches represent ledger corruption rather than user error.
     *
     * @return `tesSUCCESS` on success.
     * @return `tecNO_ENTRY` if the Check cannot be peeked (should not occur
     *     after a passing `preclaim`).
     * @return `tefBAD_LEDGER` if a directory removal fails due to ledger
     *     corruption (unreachable in a correctly functioning ledger).
     */
    TER
    doApply() override;

    /**
     * Per-entry invariant visitor hook — no transaction-specific invariants
     * are currently registered for `CheckCancel`.
     *
     * @param isDelete  Whether the entry is being deleted.
     * @param before    SLE state before the transaction.
     * @param after     SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Post-transaction invariant finalizer — no transaction-specific
     * invariants are currently registered for `CheckCancel`.
     *
     * @param tx      The applied transaction.
     * @param result  The TER result from `doApply`.
     * @param fee     The fee deducted.
     * @param view    A read-only view of the resulting ledger state.
     * @param j       Journal for diagnostic logging.
     * @return `true` unconditionally (no invariants to check yet).
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
