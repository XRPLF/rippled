#pragma once

#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <functional>
#include <optional>

namespace xrpl {

class Transactor;

/**
 * @brief Run all protocol invariant checks plus the transaction-specific check
 * in a single pass over the modified entries.
 *
 * Two layers of checks share one walk of the modified-entry set:
 *
 *  - **Protocol checks** are the concrete types in @c InvariantChecks, held in
 *    a @c std::tuple and dispatched statically by a compile-time fold (no
 *    virtual calls).  They are duck-typed against the two-phase contract
 *    described below; see @c InvariantChecker_PROTOTYPE in InvariantCheck.h.
 *  - **The transaction-specific check**, when @p txCheck is seated, is
 *    dispatched at runtime through @c Transactor::visitInvariantEntry and
 *    @c Transactor::finalizeInvariants.
 *
 * Both layers honour the same two-phase protocol:
 *
 * **Phase 1 — state collection** (`visitEntry` / `visitInvariantEntry`)
 * Called once for each ledger entry created, modified, or deleted by the
 * transaction.  Implementations accumulate whatever state they need to
 * evaluate their post-conditions.  Must not throw.
 *
 * **Phase 2 — condition evaluation** (`finalize` / `finalizeInvariants`)
 * Called once after every modified entry has been visited.  Returns true if
 * all post-conditions hold, false to fail the transaction.
 *
 * `txCheck`'s phase 1 accumulates state on the same traversal that drives the
 * protocol checkers, then both layers' phase 2 run on the complete state.
 *
 * Any failure (a finalize step returning false or an exception anywhere in
 * the check) returns @c failInvariantCheck(result).  On the first pass that
 * yields @c tecINVARIANT_FAILED.  If that triggers a fee-claim reset and
 * invariants are checked again, a second failure escalates to
 * @c tefINVARIANT_FAILED, which excludes the transaction from the ledger
 * entirely.
 *
 * @param ctx     the apply context for the current transaction.
 * @param result  the tentative TER from transaction processing.
 * @param fee     the fee consumed by the transaction.
 * @param txCheck the transactor whose transaction-specific invariants should
 *                also be checked, or @c std::nullopt to run only the
 *                protocol checks.
 * @return the final TER after all invariant checks.
 */
[[nodiscard]] TER
checkInvariants(
    ApplyContext& ctx,
    TER result,
    XRPAmount fee,
    std::optional<std::reference_wrapper<Transactor>> txCheck);

[[nodiscard]] inline TER
checkInvariants(ApplyContext& ctx, TER result, XRPAmount fee)
{
    return checkInvariants(ctx, result, fee, std::nullopt);
}

}  // namespace xrpl
