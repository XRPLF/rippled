#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <vector>

namespace xrpl {

/** Invariant checker that verifies every touched `ltLOAN` ledger entry is
 *  internally consistent after a transaction.
 *
 *  Implements the two-method invariant-checker interface and is registered
 *  unconditionally in the `InvariantChecks` tuple. If the Lending Protocol
 *  amendment is not active, no `ltLOAN` objects can exist, so `visitEntry`
 *  never collects anything and `finalize` returns `true` immediately — no
 *  explicit amendment gate is required.
 *
 *  The following invariants are enforced on every collected loan:
 *  1. **Payment-completion consistency (zero direction)** — if
 *     `sfPaymentRemaining == 0` then `sfTotalValueOutstanding`,
 *     `sfPrincipalOutstanding`, and `sfManagementFeeOutstanding` must all
 *     be zero (loan is fully settled).
 *  2. **Payment-completion consistency (non-zero direction)** — if
 *     `sfPaymentRemaining != 0` then at least one of the outstanding amounts
 *     must also be non-zero (a zeroed balance cannot coexist with a non-zero
 *     payment count).
 *  3. **Overpayment flag immutability** — `lsfLoanOverpayment` must not
 *     change during a transaction.
 *  4. **Non-negative financial fields** — `sfLoanServiceFee`,
 *     `sfLatePaymentFee`, `sfClosePaymentFee`, `sfPrincipalOutstanding`,
 *     `sfTotalValueOutstanding`, and `sfManagementFeeOutstanding` must each
 *     be ≥ 0.
 *  5. **Strictly positive periodic payment** — `sfPeriodicPayment` must be
 *     > 0; a zero or negative value would produce undefined amortization.
 */
class ValidLoan
{
    /** Collected loan entries touched by the transaction.
     *
     *  Each pair holds the (before, after) SLE snapshots.  `after` is used
     *  for all absolute-value checks; `before` is used only for the
     *  overpayment-flag change detection and may be `nullptr` for newly
     *  created loans.
     */
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> loans_;

public:
    /** Accumulate `ltLOAN` entries modified by the transaction for later
     *  validation.
     *
     *  Records an entry only when `after` is non-null and its type is
     *  `ltLOAN`; deletions (where `after` is null) are intentionally
     *  ignored because a removed loan has no post-transaction state to
     *  validate.
     *
     *  @param isDelete  `true` when the entry is being removed from the
     *      ledger (unused — filtering is based on `after` nullness).
     *  @param before    SLE snapshot before the transaction; `nullptr` if
     *      the entry was created by this transaction.
     *  @param after     SLE snapshot after the transaction; `nullptr` if
     *      the entry was deleted by this transaction.
     */
    void
    visitEntry(bool isDelete, std::shared_ptr<SLE const> const& before, std::shared_ptr<SLE const> const& after);

    /** Validate all `ltLOAN` objects collected during `visitEntry`.
     *
     *  Iterates over the collected (before, after) pairs and enforces the
     *  five invariants described in the class documentation. Returns `false`
     *  and emits a `fatal`-level journal message on the first violation
     *  detected within each loan entry.
     *
     *  The `tx`, `fee`, and `view` parameters are unused; loan consistency
     *  can be verified entirely from the collected SLE snapshots.
     *
     *  @param tx    The transaction being applied (unused by this checker).
     *  @param ter   Result code returned by `doApply` (unused by this checker).
     *  @param fee   Fee charged by the transaction (unused by this checker).
     *  @param view  Read-only post-transaction ledger view (unused by this
     *      checker).
     *  @param j     Journal for fatal-level diagnostic logging on failure.
     *  @return `true` if every collected loan satisfies all invariants;
     *      `false` if any invariant is violated (triggers rollback and
     *      escalation to `tecINVARIANT_FAILED`).
     */
    bool
    finalize(STTx const& tx, TER const ter, XRPAmount const fee, ReadView const& view, beast::Journal const& j);
};

}  // namespace xrpl
