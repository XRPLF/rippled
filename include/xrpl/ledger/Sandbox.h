#pragma once

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>

namespace xrpl {

/** Discardable staging layer for ledger mutations within a single transaction.
 *
 *  `Sandbox` accumulates ledger changes in a private write buffer inherited
 *  from `detail::ApplyViewBase` without touching the underlying ledger.  The
 *  caller decides at the end of the operation whether to commit — by calling
 *  `apply()` — or to discard — by letting the sandbox go out of scope.  This
 *  eliminates the need for explicit rollback: on failure, destruction of the
 *  sandbox is sufficient.
 *
 *  The typical pattern used by transactors:
 *  @code
 *  Sandbox sb(&ctx_.view());
 *  auto const result = doWork(sb, ...);
 *  if (result == tesSUCCESS)
 *      sb.apply(ctx_.rawView());
 *  @endcode
 *
 *  `Sandbox` is the minimal concrete subclass of `ApplyViewBase`: it adds
 *  only constructors and `apply()`.  It does not produce `TxMeta` (that is
 *  `ApplyViewImpl`'s responsibility) and does not track deferred credits (that
 *  is `PaymentSandbox`'s responsibility).  Use `Sandbox` whenever a transactor
 *  or helper needs a safe, atomic scratchpad without those heavier features.
 *
 *  The sandbox always inherits the `ApplyFlags` of its base view, so
 *  dry-run, no-check-sign, and similar execution-context properties propagate
 *  correctly through nested sandboxes without re-specification.
 *
 *  Not copyable or move-assignable; move-constructible only.  This enforces
 *  single ownership of the change buffer.
 *
 *  @see detail::ApplyViewBase for the full `ApplyView`/`RawView` interface.
 *  @see ApplyViewImpl for the outermost commit path that also builds `TxMeta`.
 *  @see PaymentSandbox for the variant that prevents within-payment
 *      double-counting of credits.
 */
class Sandbox : public detail::ApplyViewBase
{
public:
    Sandbox() = delete;
    Sandbox(Sandbox const&) = delete;
    Sandbox&
    operator=(Sandbox&&) = delete;
    Sandbox&
    operator=(Sandbox const&) = delete;

    Sandbox(Sandbox&&) = default;

    /** Construct over any read-only ledger snapshot with explicit flags.
     *
     *  @param base Non-owning pointer to the underlying ledger state; must
     *      outlive this sandbox.  All reads that bypass the change buffer
     *      are forwarded here.
     *  @param flags Per-transaction policy flags (e.g. `tapDRY_RUN`,
     *      `tapNO_CHECK_SIGN`) governing this apply pass.
     */
    Sandbox(ReadView const* base, ApplyFlags flags) : ApplyViewBase(base, flags)
    {
    }

    /** Construct over an existing `ApplyView`, inheriting its flags.
     *
     *  Convenience form used when stacking a `Sandbox` on top of another
     *  mutable view (including another `Sandbox` or a `PaymentSandbox`).
     *  Flags are copied from the parent so that execution-context properties
     *  such as `tapDRY_RUN` propagate without the caller re-specifying them.
     *
     *  @param base Non-owning pointer to the parent mutable view; must
     *      outlive this sandbox.
     */
    Sandbox(ApplyView const* base) : Sandbox(base, base->flags())
    {
    }

    /** Commit all buffered changes to a target `RawView`.
     *
     *  Replays every insert, modify, and erase action accumulated in the
     *  internal change buffer against `to`, atomically promoting the tentative
     *  mutations into the target.  After this call the buffer is reset; the
     *  sandbox must not be used again.
     *
     *  If the caller decides the operation failed, simply do not call `apply()`
     *  — destroying the sandbox discards all buffered changes without touching
     *  the target view.
     *
     *  @param to The target `RawView` to receive the committed mutations;
     *      typically `ctx_.rawView()` at the outermost transactor boundary.
     */
    void
    apply(RawView& to)
    {
        items_.apply(to);
    }
};

}  // namespace xrpl
