/** @file
 *  Declares the ValidAMM post-transaction invariant checker for AMM ledger state.
 */

#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <optional>

namespace xrpl {

/** Post-transaction invariant checker that validates AMM ledger state consistency.
 *
 *  Part of the `InvariantChecks` tuple run after every transaction. Detects
 *  corrupt or impossible AMM state that correct code should never produce. Two
 *  categories of corruption are guarded against:
 *
 *  1. **Constant-product violation** — after any deposit or withdrawal the
 *     geometric mean of pool reserves must satisfy
 *     `sqrt(amount × amount2) ≥ lptAMMBalance`.
 *  2. **Structural invariants** — create must set balances exactly equal to
 *     `sqrt(amount × amount2)`; bid must burn LP tokens without touching the
 *     pool; vote must change nothing at all; delete must remove the AMM object;
 *     DEX operations (Payment, OfferCreate, CheckCash) must not write to the
 *     AMM object.
 *
 *  Enforcement is gated on the `fixAMMv1_3` amendment. Before the amendment
 *  activates, violations are logged but the checker still returns `true`,
 *  preserving backward compatibility. After activation, any violation returns
 *  `false` and triggers the invariant-failure escalation in `ApplyContext`.
 *
 *  @note Each instance is constructed fresh per transaction by the invariant
 *      framework; there is no shared mutable state between transactions.
 *
 *  @see InvariantChecks
 *  @see InvariantChecker_PROTOTYPE
 */
class ValidAMM
{
    /** AMM pseudo-account ID, populated when an `ltAMM` object is modified. */
    std::optional<AccountID> ammAccount_;
    /** LP token supply recorded from the `ltAMM` object after the transaction. */
    std::optional<STAmount> lptAMMBalanceAfter_;
    /** LP token supply recorded from the `ltAMM` object before the transaction. */
    std::optional<STAmount> lptAMMBalanceBefore_;
    /** True if any AMM pool entry (`ltRIPPLE_STATE` with `lsfAMMNode`, or
     *  `ltACCOUNT_ROOT` with `sfAMMID`) was modified by the transaction. */
    bool ammPoolChanged_{false};

public:
    /** Controls whether an all-zeros pool state is accepted as valid.
     *
     *  `No` requires all three balances (amount, amount2, LP supply) to be
     *  strictly positive — used for deposits where a zero pool is never
     *  legitimate.  `Yes` additionally permits the simultaneous all-zeros case
     *  that occurs when the final withdrawal drains the pool completely.
     */
    enum class ZeroAllowed : bool { No = false, Yes = true };

    ValidAMM() = default;

    /** Record AMM-relevant changes for a single modified ledger entry.
     *
     *  Called by the invariant framework once per modified SLE before
     *  `finalize` is called. Deletions are ignored entirely. For surviving
     *  entries the method captures:
     *  - The AMM pseudo-account ID and post-transaction LP token balance when
     *    an `ltAMM` object is modified.
     *  - The pre-transaction LP token balance from the before-snapshot of an
     *    `ltAMM` object.
     *  - The `ammPoolChanged_` flag when an AMM pool entry is touched.
     *
     *  @param isDelete True if the entry is being deleted; the call is a no-op
     *      in that case.
     *  @param before SLE snapshot from before the transaction, or nullptr for
     *      newly created entries.
     *  @param after SLE snapshot from after the transaction, or nullptr for
     *      deleted entries.
     */
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    /** Render a verdict on AMM state consistency after all entries are visited.
     *
     *  Short-circuits (returns `true`) for any failure result that is neither
     *  `tesSUCCESS` nor `tecINCOMPLETE`. The `tecINCOMPLETE` carve-out exists
     *  because `AMMDelete` may return that code on a partial deletion pass and
     *  still requires validation.
     *
     *  Dispatches to a per-transaction-type helper:
     *  - `ttAMM_CREATE` → `finalizeCreate` (exact equality check)
     *  - `ttAMM_DEPOSIT` → `finalizeDeposit` (geometric-mean ≥ LP supply,
     *    zero pool disallowed)
     *  - `ttAMM_WITHDRAW`, `ttAMM_CLAWBACK` → `finalizeWithdraw`
     *    (geometric-mean ≥ LP supply, all-zeros terminal state allowed)
     *  - `ttAMM_BID` → `finalizeBid` (pool unchanged, LP supply decreased)
     *  - `ttAMM_VOTE` → `finalizeVote` (pool and LP supply both unchanged)
     *  - `ttAMM_DELETE` → `finalizeDelete` (AMM object absent on success)
     *  - `ttCHECK_CASH`, `ttOFFER_CREATE`, `ttPAYMENT` → `finalizeDEX`
     *    (AMM object must not have been written)
     *
     *  Whether a detected violation returns `false` or is merely logged depends
     *  on whether the `fixAMMv1_3` amendment is active in `view`.
     *
     *  @param tx The transaction that was applied.
     *  @param result The TER result of the transaction.
     *  @param fee The XRP fee charged (unused by this checker).
     *  @param view Read-only ledger view used to re-read pool balances.
     *  @param j Journal for error logging.
     *  @return `true` if the invariant holds (or is unenforced); `false` if a
     *      violation is detected and `fixAMMv1_3` is active.
     */
    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);

private:
    /** Verify that a bid left the pool unchanged and burned LP tokens.
     *
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param j Journal for error logging.
     *  @return `false` if the pool changed or LP supply did not strictly
     *      decrease to a positive value and `enforce` is true.
     */
    [[nodiscard]] bool
    finalizeBid(bool enforce, beast::Journal const&) const;

    /** Verify that a vote left both the pool and the LP token supply unchanged.
     *
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param j Journal for error logging.
     *  @return `false` if either the LP token balance or the pool changed and
     *      `enforce` is true.
     */
    [[nodiscard]] bool
    finalizeVote(bool enforce, beast::Journal const&) const;

    /** Verify that an AMM create produced a valid initial pool state.
     *
     *  Checks that the AMM object was actually created, that all three
     *  balances are strictly positive, and that the LP token supply equals
     *  `sqrt(amount × amount2)` exactly (using `ammLPTokens`).
     *
     *  @param tx The `ttAMM_CREATE` transaction.
     *  @param view Ledger view used to read actual pool balances via
     *      `ammPoolHolds`.
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param j Journal for error logging.
     *  @return `false` if the AMM object is absent, any balance is non-positive,
     *      or `sqrt(amount × amount2) ≠ lptAMMBalance` and `enforce` is true.
     */
    [[nodiscard]] bool
    finalizeCreate(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;

    /** Verify that an AMM delete removed the AMM object.
     *
     *  On `tesSUCCESS` the AMM object must be absent. On `tecINCOMPLETE`
     *  (partial deletion pass) the object must similarly not have been written.
     *
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param res The TER result of the delete transaction.
     *  @param j Journal for error logging.
     *  @return `false` if `ammAccount_` is set (object was modified rather than
     *      deleted) and `enforce` is true.
     */
    [[nodiscard]] bool
    finalizeDelete(bool enforce, TER res, beast::Journal const&) const;

    /** Verify the constant-product invariant after a deposit.
     *
     *  Delegates to `generalInvariant` with `ZeroAllowed::No`; a zero pool
     *  is never legitimate after a deposit.
     *
     *  @param tx The `ttAMM_DEPOSIT` transaction (provides `sfAsset`/`sfAsset2`).
     *  @param view Ledger view used to read actual pool balances.
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param j Journal for error logging.
     *  @return `false` if the AMM object was deleted or the constant-product
     *      invariant is violated and `enforce` is true.
     */
    [[nodiscard]] bool
    finalizeDeposit(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;

    /** Verify the constant-product invariant after a withdrawal or clawback.
     *
     *  If `ammAccount_` is absent, the final withdrawal deleted the AMM — this
     *  is legitimate and the method returns `true` immediately. Otherwise
     *  delegates to `generalInvariant` with `ZeroAllowed::Yes`, which permits
     *  the simultaneous all-zeros terminal state.
     *
     *  @param tx The `ttAMM_WITHDRAW` or `ttAMM_CLAWBACK` transaction.
     *  @param view Ledger view used to read actual pool balances.
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param j Journal for error logging.
     *  @return `false` if the constant-product invariant is violated and
     *      `enforce` is true; `true` if the AMM was legitimately deleted.
     */
    // Includes clawback
    [[nodiscard]] bool
    finalizeWithdraw(STTx const&, ReadView const&, bool enforce, beast::Journal const&) const;

    /** Verify that a DEX operation (Payment, OfferCreate, CheckCash) did not
     *  write to the AMM object.
     *
     *  DEX transactions may route liquidity through AMM pools but must never
     *  mutate the `ltAMM` ledger object itself.
     *
     *  @param enforce Whether to return `false` on violation (`fixAMMv1_3`).
     *  @param j Journal for error logging.
     *  @return `false` if `ammAccount_` is set (the AMM object was written)
     *      and `enforce` is true.
     */
    [[nodiscard]] bool
    finalizeDEX(bool enforce, beast::Journal const&) const;

    /** Check the constant-product invariant for deposit and withdrawal paths.
     *
     *  Re-reads actual pool balances from `view` via `ammPoolHolds`, then
     *  verifies:
     *  - All balances satisfy the `zeroAllowed` constraint (strictly positive,
     *    or simultaneously all-zeros when `ZeroAllowed::Yes`).
     *  - `sqrt(amount × amount2) ≥ lptAMMBalanceAfter_` (strong check), or
     *    within a relative tolerance of `1e-11` (weak fallback to absorb
     *    fixed-point rounding).
     *
     *  Logging is unconditional on failure: the error line always includes the
     *  transaction hash, individual pool amounts, geometric mean, LP token
     *  balance, and relative deviation. The `enforce` flag is NOT consulted
     *  here — callers decide whether to propagate `false`.
     *
     *  @param tx Transaction providing `sfAsset`/`sfAsset2` for pool lookup.
     *  @param view Ledger view used to read pool balances.
     *  @param zeroAllowed Whether an all-zeros pool is a valid terminal state.
     *  @param j Journal for error logging.
     *  @return `false` if either the balance constraint or the geometric-mean
     *      bound is violated; `true` otherwise.
     */
    [[nodiscard]] bool
    generalInvariant(STTx const&, ReadView const&, ZeroAllowed zeroAllowed, beast::Journal const&)
        const;
};

}  // namespace xrpl
