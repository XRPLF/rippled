/** @file
 *  Declares the `MPTokenIssuanceDestroy` transactor (`ttMPTOKEN_ISSUANCE_DESTROY`).
 *
 *  Handles permanent deletion of an MPToken issuance object from the ledger,
 *  reclaiming the issuer's owner-count slot and removing the entry from the
 *  owner directory.  Gated behind `featureMPTokensV1`; delegable via the
 *  `destroyMPTIssuance` privilege.
 */

#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttMPTOKEN_ISSUANCE_DESTROY` transaction type (opcode 55).
 *
 *  Permanently removes an `MPTokenIssuance` ledger object when no tokens remain
 *  in circulation (`sfOutstandingAmount == 0`) and none are locked
 *  (`sfLockedAmount == 0`), protecting any holders from having live balances
 *  orphaned.  On success the issuer's owner count is decremented by one.
 *
 *  All substantive validation lives in `preclaim`; `preflight` is intentionally
 *  trivial because every guard requires reading current ledger state.
 *
 *  @see MPTokenIssuanceCreate
 */
class MPTokenIssuanceDestroy : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit MPTokenIssuanceDestroy(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Static validation phase — unconditionally returns `tesSUCCESS`.
     *
     *  Every meaningful precondition for this transaction requires ledger
     *  state that is unavailable at preflight time.  Field-level checks
     *  (valid fee, sequence, well-formed signature) are handled by the
     *  framework's `preflight0`/`preflight1` layers before this method runs.
     *
     *  @param ctx  The preflight context (unused).
     *  @return `tesSUCCESS` always.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Read-only ledger validation — enforces all destruction preconditions.
     *
     *  Reads the `MPTokenIssuance` SLE identified by `sfMPTokenIssuanceID` and
     *  enforces three invariants in order:
     *  1. The issuance object must exist (`tecOBJECT_NOT_FOUND` if absent).
     *  2. `sfIssuer` must match the transaction submitter (`tecNO_PERMISSION`
     *     if it does not) — only the issuer may destroy their own issuance.
     *  3. `sfOutstandingAmount` must be zero, and `sfLockedAmount` (if present)
     *     must also be zero (`tecHAS_OBLIGATIONS` if either is non-zero).
     *
     *  @param ctx  The preclaim context providing a read-only ledger view.
     *  @return `tesSUCCESS` if all guards pass; `tecOBJECT_NOT_FOUND`,
     *      `tecNO_PERMISSION`, or `tecHAS_OBLIGATIONS` otherwise.
     *  @note A non-zero `sfLockedAmount` when `sfOutstandingAmount` is already
     *      zero would indicate corrupted ledger state unreachable through normal
     *      transaction flow; the check is present as a defensive backstop.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Execute the ledger mutation: erase the issuance SLE and update accounting.
     *
     *  Sequence of operations:
     *  1. Peeks the `MPTokenIssuance` SLE via `view().peek(keylet::mptIssuance(...))`.
     *  2. Removes the entry from the issuer's owner directory via `view().dirRemove()`.
     *  3. Erases the SLE with `view().erase()`.
     *  4. Decrements the issuer's owner count by one via `adjustOwnerCount()`.
     *
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if the owner-directory
     *      removal fails (indicates ledger corruption, not user error).
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op stub; no transaction-specific invariants are defined
     *  yet.  Satisfies the `Transactor` interface and serves as the extension
     *  point for future checks.
     *
     *  @param isDelete  True if the entry was erased.
     *  @param before    Entry state before the transaction (nullptr for new entries).
     *  @param after     Entry state after the transaction (nullptr for deleted entries).
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalize transaction-specific invariant checks.
     *
     *  Currently a no-op stub that always returns `true`.  Satisfies the
     *  `Transactor` interface; reserved for future per-transaction invariants.
     *
     *  @param tx     The transaction being applied.
     *  @param result The tentative TER result.
     *  @param fee    The fee consumed.
     *  @param view   Read-only ledger view after the transaction.
     *  @param j      Logging sink.
     *  @return Always `true` (no invariants to fail).
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
