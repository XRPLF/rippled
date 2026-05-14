/** @file
 *  State governor for AMM participation in a single `flow()` call.
 *
 *  `AMMContext` is threaded by reference through `toStrands()`, `flowOne()`,
 *  and every `AMMLiquidity` instance so that all parts of the payment engine
 *  share one authoritative counter for AMM iteration bookkeeping.
 */

#pragma once

#include <xrpl/protocol/AccountID.h>

#include <cstdint>

namespace xrpl {

/** Tracks AMM offer consumption state across one payment engine execution.
 *
 *  A single instance is created at the top of `Flow.cpp::flow()` and passed
 *  by reference into every `AMMLiquidity` object for the life of that call.
 *  It governs two things:
 *
 *  1. **Iteration budget** — AMM pools are continuous and can produce a new
 *     offer at the current spot price after every consumption, unlike CLOB
 *     offers which are removed after use. `AMMContext` enforces
 *     `kMAX_ITERATIONS` to bound execution time.
 *
 *  2. **Offer-sizing strategy** — `multiPath_` tells `AMMLiquidity` whether
 *     to use quality-matched single-path sizing or Fibonacci-scaled multi-path
 *     sizing. It is updated dynamically as the set of active strands changes.
 *
 *  @note Non-copyable by design: all `AMMLiquidity` objects hold a reference
 *      to the shared instance. Copying would create divergent counters that
 *      the engine could not reconcile.
 *
 *  @see AMMLiquidity, AMMOffer, StrandFlow.h
 */
class AMMContext
{
public:
    /** Maximum number of payment engine iterations that may consume an AMM
     *  offer in a single `flow()` call.
     *
     *  AMM pools are continuous liquidity sources that never become exhausted
     *  the way CLOB offers do, so `BookStep`'s built-in offer counter does not
     *  bound them. This constant caps the AMM-specific iteration count to
     *  prevent unbounded execution. `AMMLiquidity::generateFibSeqOffer` uses
     *  a Fibonacci table with exactly this many entries.
     */
    constexpr static std::uint8_t kMAX_ITERATIONS = 30;

private:
    AccountID account_;
    bool multiPath_{false};
    bool ammUsed_{false};
    std::uint16_t ammIters_{0};

public:
    /** Construct with the initiating account and initial path multiplicity.
     *
     *  @param account   `AccountID` of the transaction sender. `BookStep`
     *      uses this to look up the sender's per-account AMM trading fee.
     *  @param multiPath `true` if the payment already has more than one
     *      strand at construction time; `false` otherwise. May be updated
     *      later via `setMultiPath()`.
     */
    AMMContext(AccountID const& account, bool multiPath) : account_(account), multiPath_(multiPath)
    {
    }
    ~AMMContext() = default;
    AMMContext(AMMContext const&) = delete;
    AMMContext&
    operator=(AMMContext const&) = delete;

    /** Return whether the payment currently has more than one active strand.
     *
     *  When `true`, `AMMLiquidity` sizes synthetic AMM offers using the
     *  Fibonacci sequence keyed to `curIters()`. When `false`, it sizes them
     *  so the post-swap spot price matches the best competing CLOB quality.
     *
     *  @return `true` if multi-path mode is active.
     */
    [[nodiscard]] bool
    multiPath() const
    {
        return multiPath_;
    }

    /** Update the path-multiplicity flag.
     *
     *  Called by `StrandFlow.h` after each `activateNext()` because the
     *  number of active strands can change mid-payment.
     *
     *  @param fs `true` if more than one strand is currently active.
     */
    void
    setMultiPath(bool fs)
    {
        multiPath_ = fs;
    }

    /** Mark that an AMM offer was consumed during the current iteration.
     *
     *  Called from `AMMOffer::consume()` the moment an AMM offer is accepted.
     *  The flag is read by `update()` to decide whether to increment the
     *  iteration counter, then cleared unconditionally.
     */
    void
    setAMMUsed()
    {
        ammUsed_ = true;
    }

    /** Commit the current iteration's AMM usage to the running counter.
     *
     *  Increments `ammIters_` if an AMM offer was consumed this iteration
     *  (i.e., `setAMMUsed()` was called), then resets the per-iteration flag.
     *  Called once per outer loop iteration in `StrandFlow.h` after the
     *  winning strand's sandbox has been applied to the main ledger view.
     *
     *  @note Iterations that route entirely through CLOB offers leave
     *      `ammIters_` unchanged.
     */
    void
    update()
    {
        if (ammUsed_)
            ++ammIters_;
        ammUsed_ = false;
    }

    /** Return whether the AMM iteration budget has been exhausted.
     *
     *  Checked by `AMMLiquidity::getOffer()` before generating each new AMM
     *  offer. Once `true`, no further AMM liquidity is offered for this
     *  payment.
     *
     *  @return `true` if `ammIters_` has reached `kMAX_ITERATIONS`.
     */
    [[nodiscard]] bool
    maxItersReached() const
    {
        return ammIters_ >= kMAX_ITERATIONS;
    }

    /** Return the number of iterations that have consumed AMM liquidity.
     *
     *  Used by `AMMLiquidity::generateFibSeqOffer()` as the index into the
     *  Fibonacci scaling table. Zero on the first AMM-consuming iteration.
     *
     *  @return Current AMM iteration count, in `[0, kMAX_ITERATIONS)`.
     */
    [[nodiscard]] std::uint16_t
    curIters() const
    {
        return ammIters_;
    }

    /** Return the `AccountID` of the transaction sender.
     *
     *  @return The account used to look up the per-sender AMM trading fee.
     */
    [[nodiscard]] AccountID
    account() const
    {
        return account_;
    }

    /** Reset the per-iteration AMM-used flag before a new strand attempt.
     *
     *  Strand execution can fail and its sandbox is discarded. Resetting
     *  `ammUsed_` here prevents a failed strand's AMM consumption from being
     *  double-counted when `update()` is called for the strand that succeeds.
     *  Called by `StrandFlow.h` at the start of each strand attempt.
     */
    void
    clear()
    {
        ammUsed_ = false;
    }
};

}  // namespace xrpl
