#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <functional>
#include <optional>

namespace xrpl {

/**
 * @brief Runtime interface for a transaction-specific invariant check.
 *
 * The free checkInvariants runner drives two layers of checks over a single
 * walk of the modified ledger entries:
 *
 *  - Protocol checks are the concrete types in InvariantChecks, held in a
 *    std::tuple and dispatched statically by a compile-time fold (no
 *    virtual calls).  They are duck-typed against the two-phase contract
 *    described below; see InvariantChecker_PROTOTYPE in InvariantCheck.h.
 *  - The transaction-specific check is injected at runtime through this
 *    interface, so the runner can call it without depending on the concrete
 *    transactor type.  Transactor implements this interface directly (see
 *    Transactor.h) so that the interface's access can stay narrower than
 *    Transactor's own public surface: calling through a TxInvariantCheck&
 *    (all the runner ever holds) is public, but calling through a
 *    Transactor& is not, since Transactor overrides these as private
 *    (forwarding to its own protected visitInvariantEntry/finalizeInvariants).
 *
 * Both layers honour the same two-phase protocol:
 *
 * Phase 1 — state collection (visitEntry).  Called once for each ledger
 * entry created, modified, or deleted by the transaction.  Implementations
 * accumulate whatever state they need to evaluate their post-conditions.
 * Must not throw.
 *
 * Phase 2 — condition evaluation (finalize).  Called once after every
 * modified entry has been visited.  Returns true if all post-conditions
 * hold, false to fail the transaction.
 *
 * Rule: invariants must run regardless of transaction result.  finalize
 * MUST perform meaningful checks even when the transaction has failed
 * (when result is not tesSUCCESS).  A bug or exploit could cause a failed
 * transaction to mutate ledger state in unexpected ways; invariants are the
 * last line of defense.
 *
 * The typical pattern: an invariant that expects a domain-specific state
 * change (e.g. a Vault being created) should expect that change only when
 * the transaction succeeded.  A failed VaultCreate must not have created a
 * Vault.
 *
 * Rule: privilege-gated checks apply to failed transactions too.  Failed
 * transactions carry no privileges.  Any privilege-gated assertion must
 * therefore also be enforced for failed transactions.
 */
class TxInvariantCheck
{
public:
    virtual ~TxInvariantCheck() = default;

    /**
     * @brief Called for each ledger entry modified by the transaction.
     *
     * @param isDelete true if the SLE is being deleted.
     * @param before   the entry's state before the transaction (nullptr for
     *                 newly created entries).
     * @param after    the entry's state after the transaction.  For deletions
     *                 this is the SLE being erased; use @p isDelete rather than
     *                 a null @p after to detect deletions.  @p after is
     *                 never null.
     */
    virtual void
    visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) = 0;

    /**
     * @brief Called after all entries have been visited.
     *
     * @param tx     the transaction being applied.
     * @param result the tentative TER result of the transaction.
     * @param fee    the fee consumed by the transaction.
     * @param view   read-only view of the ledger after the transaction.
     * @param j      journal for logging invariant failures.
     * @return true if all invariants hold; false to fail with
     *         tecINVARIANT_FAILED / tefINVARIANT_FAILED.
     */
    [[nodiscard]] virtual bool
    finalize(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) = 0;
};

/**
 * @brief Run all protocol invariant checks plus the transaction-specific check
 * in a single pass over the modified entries.
 *
 * Both layers share one walk of the modified-entry set: @p txCheck's
 * visitEntry accumulates state on the same traversal that drives the
 * protocol checkers, then both layers' finalize run on the complete state.
 *
 * Any failure (a finalize returning false or an exception anywhere in the
 * check) returns failInvariantCheck(result).  On the first pass that yields
 * tecINVARIANT_FAILED, which the transactor treats as a signal to roll the
 * transaction's effects back to a fee-claim-only state and re-run this
 * runner against the reduced state (see Transactor::InvariantScope).  If
 * that second pass also fails, the result escalates to tefINVARIANT_FAILED,
 * which excludes the transaction from the ledger entirely.
 *
 * The whole traversal — both layers' visitEntry calls and both layers'
 * finalize calls — runs under a single try/catch.  There is no per-layer
 * isolation: an exception anywhere aborts the remaining traversal and
 * finalize calls and fails the transaction.
 *
 * @param ctx     the apply context for the current transaction.
 * @param result  the tentative TER from transaction processing.
 * @param fee     the fee consumed by the transaction.
 * @param txCheck the transaction-specific invariant check.
 * @return the final TER after all invariant checks.
 */
[[nodiscard]] TER
checkInvariants(
    ApplyContext& ctx,
    TER result,
    XRPAmount fee,
    std::optional<std::reference_wrapper<TxInvariantCheck>> txCheck);

[[nodiscard]] inline TER
checkInvariants(ApplyContext& ctx, TER result, XRPAmount fee)
{
    return checkInvariants(ctx, result, fee, std::nullopt);
}

}  // namespace xrpl
