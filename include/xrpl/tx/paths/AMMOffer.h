/** @file
 *  Declares `AMMOffer`, the synthetic offer adapter that presents an AMM pool
 *  as a `TOffer`-compatible object for `BookStep`'s generic payment-engine loop.
 *
 *  @see AMMLiquidity, BookStep, QualityFunction
 */

#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

template <typename TIn, typename TOut>
class AMMLiquidity;
class QualityFunction;

/** Synthetic offer representing AMM pool liquidity inside `BookStep`.
 *
 *  `AMMOffer` exposes the same named interface as `TOffer<TIn, TOut>` —
 *  `quality()`, `amount()`, `consume()`, `fullyConsumed()`, `limitIn()`,
 *  `limitOut()`, `send()`, `isFunded()`, `adjustRates()`, `checkInvariant()`
 *  — so that `BookStep` can treat CLOB and AMM liquidity polymorphically via
 *  structural duck-typing without virtual dispatch.
 *
 *  An `AMMOffer` is not backed by any ledger entry; `key()` always returns
 *  `std::nullopt` and pool balance updates happen in `BookStep::consumeOffer()`
 *  via `accountSend`, not here.  Each instance may be consumed at most once
 *  per payment-engine iteration; `AMMLiquidity::getOffer()` creates a fresh
 *  one for each iteration.
 *
 *  Behavior diverges between single-path and multi-path modes:
 *  - **Single-path**: `limitOut`/`limitIn` apply the constant-product swap
 *    formula against `balances_`; `getQualityFunc()` returns an AMM quality
 *    function with a nonzero slope encoding the pool curve.
 *  - **Multi-path**: `limitOut`/`limitIn` scale proportionally to `quality_`
 *    (like a fixed-rate CLOB offer); `getQualityFunc()` returns a constant
 *    quality function.  This preserves strand quality ordering across
 *    competing paths.
 *
 *  @tparam TIn  Amount type for the input asset (`IOUAmount`, `XRPAmount`,
 *      or `MPTAmount`).
 *  @tparam TOut Amount type for the output asset (`IOUAmount`, `XRPAmount`,
 *      or `MPTAmount`).
 *
 *  @note Explicitly instantiated for all eight valid `(TIn, TOut)` pairings in
 *      `AMMOffer.cpp`; do not add implicit instantiations elsewhere.
 */
template <StepAmount TIn, StepAmount TOut>
class AMMOffer
{
private:
    AMMLiquidity<TIn, TOut> const& ammLiquidity_;

    /** Synthetic offer size as presented to `BookStep`.
     *
     *  In multi-path mode this is a Fibonacci-sequence-scaled amount so that
     *  successive iterations probe progressively larger AMM liquidity slices.
     *  In single-path mode it is either quality-matched to the competing CLOB
     *  offer (full consumption moves the pool's spot price to that quality) or
     *  a "max offer" representing 99% of the output-side pool balance when no
     *  CLOB offer exists.
     */
    TAmounts<TIn, TOut> const amounts_;

    /** Pool token balances at the moment this offer was generated.
     *
     *  Snapshotted separately from `amounts_` because in single-path mode the
     *  spot-price quality used as `quality_` can diverge from the raw ratio of
     *  `amounts_` when the offer is sized relative to a competing CLOB.
     *  `limitOut` and `limitIn` use these in single-path mode to evaluate the
     *  constant-product swap formula.
     */
    TAmounts<TIn, TOut> const balances_;

    /** Effective exchange-rate quality for this offer.
     *
     *  Equals the spot-price quality derived from `balances_` when
     *  `balances_ != amounts_` (single-path quality-matched sizing); otherwise
     *  equals the quality implied directly by `amounts_`.
     */
    Quality const quality_;

    /** True once `consume()` has been called; enforces at-most-once crossing. */
    bool consumed_{false};

public:
    /** Construct from sizing data provided by `AMMLiquidity::getOffer`.
     *
     *  @param ammLiquidity  Owning liquidity manager; provides pool metadata
     *      (account ID, assets, trading fee) and the `AMMContext` that tracks
     *      cross-iteration state.  Must outlive this offer.
     *  @param amounts   Synthetic offer size — Fibonacci-scaled in multi-path
     *      mode, quality-matched or pool-draining in single-path mode.
     *  @param balances  Live pool balances at the moment of offer generation;
     *      used by `limitOut`/`limitIn` in single-path mode.
     *  @param quality   Spot-price quality when `balances != amounts`; otherwise
     *      the quality implied by `amounts`.
     */
    AMMOffer(
        AMMLiquidity<TIn, TOut> const& ammLiquidity,
        TAmounts<TIn, TOut> const& amounts,
        TAmounts<TIn, TOut> const& balances,
        Quality const& quality);

    /** Return the effective exchange-rate quality for this offer.
     *
     *  In single-path mode this is the pool's spot-price quality; in
     *  multi-path mode it is the fixed quality of the Fibonacci-sized offer.
     *  `BookStep` uses this to order competing AMM and CLOB offers.
     *
     *  @return The offer's quality (input-to-output ratio, sorted ascending).
     */
    [[nodiscard]] Quality
    quality() const noexcept
    {
        return quality_;
    }

    /** Return the input-side asset of the underlying AMM pool.
     *
     *  @return Reference to the pool's input `Asset`; lifetime is that of
     *      the owning `AMMLiquidity`.
     */
    [[nodiscard]] Asset const&
    assetIn() const;

    /** Return the output-side asset of the underlying AMM pool.
     *
     *  @return Reference to the pool's output `Asset`; lifetime is that of
     *      the owning `AMMLiquidity`.
     */
    [[nodiscard]] Asset const&
    assetOut() const;

    /** Return the AMM pool's on-ledger account ID.
     *
     *  `BookStep` uses this as the logical "owner" of the synthetic offer for
     *  logging and metadata purposes.
     *
     *  @return Reference to the AMM `AccountID`; lifetime is that of the
     *      owning `AMMLiquidity`.
     */
    [[nodiscard]] AccountID const&
    owner() const;

    /** Return `std::nullopt` to indicate there is no backing ledger entry.
     *
     *  `TOffer::key()` returns the offer's ledger-object key so `BookStep` can
     *  erase it after crossing.  `AMMOffer` has no ledger object; returning
     *  `nullopt` signals to `BookStep` that no erase is needed.
     *
     *  @return Always `std::nullopt`.
     */
    [[nodiscard]] std::optional<uint256>
    key() const
    {
        return std::nullopt;
    }

    /** Return the synthetic offer size (TakerPays / TakerGets equivalent).
     *
     *  @return Reference to the `{in, out}` amounts set at construction;
     *      not modified by `limitOut`, `limitIn`, or `consume`.
     */
    [[nodiscard]] TAmounts<TIn, TOut> const&
    amount() const;

    /** Mark this offer as consumed and notify the AMM execution context.
     *
     *  Validates that `consumed` does not exceed the initial offer size, sets
     *  the `consumed_` flag, and calls `AMMContext::setAMMUsed()` so the outer
     *  payment engine knows AMM liquidity was touched this iteration.
     *
     *  @note The `view` parameter is accepted for interface compatibility with
     *      `TOffer::consume` but is not used here.  Actual pool balance updates
     *      are performed in `BookStep::consumeOffer()` via `accountSend`, which
     *      keeps all ledger mutations in one place.
     *  @note An AMM offer may only be consumed once per payment-engine iteration.
     *
     *  @param view      Mutable ledger view (unused; present for interface
     *      parity with `TOffer::consume`).
     *  @param consumed  The `{in, out}` amounts actually transferred.  Must not
     *      exceed `amounts_` in either dimension.
     *  @throws std::logic_error if `consumed.in > amounts_.in` or
     *      `consumed.out > amounts_.out`.
     */
    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed);

    /** Return `true` once the offer has been consumed this iteration.
     *
     *  Unlike `TOffer::fullyConsumed()`, which tests whether the remaining
     *  amount has reached zero, this simply reflects the `consumed_` flag set
     *  by `consume()`.  AMM offers are always either fully consumed or not
     *  consumed at all within a single payment-engine iteration.
     *
     *  @return `true` if `consume()` has been called; `false` otherwise.
     */
    [[nodiscard]] bool
    fullyConsumed() const
    {
        return consumed_;
    }

    /** Resize the offer to deliver at most `limit` units of the output asset.
     *
     *  **Single-path mode**: applies `swapAssetOut(balances_, limit,
     *  tradingFee())` — the constant-product formula — for an exact result
     *  along the AMM curve.
     *
     *  **Multi-path mode**: resizes proportionally via
     *  `quality().ceilOutStrict(offerAmount, limit, roundUp)`, preserving
     *  strand quality ordering.  The taker overpays slightly, ensuring the
     *  post-trade pool product does not decrease.
     *
     *  @param offerAmount  Current offer size (used for proportional scaling in
     *      multi-path mode; ignored in single-path mode).
     *  @param limit   Maximum output amount that may be delivered.
     *  @param roundUp Whether to round the computed input side up (forwarded
     *      to `ceilOutStrict` in multi-path mode).
     *  @return Resized `{in, out}` pair where `out <= limit`.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    limitOut(TAmounts<TIn, TOut> const& offerAmount, TOut const& limit, bool roundUp) const;

    /** Resize the offer to consume at most `limit` units of the input asset.
     *
     *  **Single-path mode**: applies `swapAssetIn(balances_, limit,
     *  tradingFee())` — the constant-product formula — for an exact result.
     *
     *  **Multi-path mode**: resizes proportionally to `quality_`.  When the
     *  `fixReducedOffersV2` amendment is active, uses `ceilInStrict` (removes
     *  a small rounding slop present in the older `ceilIn`); the older path is
     *  preserved for replay of historical ledgers where that amendment was
     *  inactive.
     *
     *  @param offerAmount  Current offer size (used for proportional scaling in
     *      multi-path mode; ignored in single-path mode).
     *  @param limit   Maximum input amount the taker will supply.
     *  @param roundUp Whether to round the computed output side up (forwarded
     *      to `ceilInStrict` when `fixReducedOffersV2` is active).
     *  @return Resized `{in, out}` pair where `in <= limit`.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp) const;

    /** Return the quality function used by the single-path optimizer.
     *
     *  **Single-path mode**: returns a `QualityFunction` with a nonzero slope
     *  derived from `balances_` and the trading fee, encoding the AMM curve
     *  `q(out) = -cfee/poolIn × out + poolOut × cfee/poolIn`.  The optimizer
     *  uses this to solve in closed form for the output amount that satisfies
     *  a requested quality limit.
     *
     *  **Multi-path mode**: returns a constant `QualityFunction` (slope = 0,
     *  intercept = `quality_`), identical to a CLOB offer, so that the AMM's
     *  varying spot price does not disturb relative quality ordering across
     *  competing strands.
     *
     *  @return A `QualityFunction` encoding the effective exchange rate as a
     *      linear function of output amount.
     */
    [[nodiscard]] QualityFunction
    getQualityFunc() const;

    /** Transfer funds from the AMM pool, waiving the transfer fee.
     *
     *  Delegates to `accountSend` with `WaiveTransferFee::Yes`.  AMM swaps on
     *  Payment transactions are exempt from transfer fees; this is the
     *  send-side enforcement of that exemption (the rate-side enforcement is
     *  in `adjustRates()`).
     *
     *  @param args  Forwarded verbatim to `accountSend`.
     *  @return The `TER` result of `accountSend`.
     */
    template <typename... Args>
    static TER
    send(Args&&... args)
    {
        return accountSend(
            std::forward<Args>(args)..., WaiveTransferFee::Yes, AllowMPTOverflow::Yes);
    }

    /** Return `true` unconditionally — the AMM pool is always its own issuer.
     *
     *  Unlike CLOB offers, which can become unfunded if the owner's balance
     *  falls, an AMM offer is backed by the pool itself and is never
     *  underfunded at the time it is generated.
     *
     *  @return Always `true`.
     */
    [[nodiscard]] bool
    isFunded() const
    {
        return true;
    }

    /** Return adjusted transfer-fee rates, zeroing the output-side rate.
     *
     *  AMM swaps on Payment transactions are exempt from transfer fees on the
     *  output side.  Passing `QUALITY_ONE` for `ofrOutRate` suppresses the
     *  output-side fee that `BookStep` would otherwise apply.  The input-side
     *  rate is passed through unchanged.
     *
     *  @param ofrInRate   Transfer fee rate on the input asset (passed through).
     *  @param ofrOutRate  Transfer fee rate on the output asset (ignored;
     *      replaced with `QUALITY_ONE`).
     *  @return `{ofrInRate, QUALITY_ONE}`.
     */
    static std::pair<std::uint32_t, std::uint32_t>
    adjustRates(std::uint32_t ofrInRate, std::uint32_t ofrOutRate)
    {
        return {ofrInRate, QUALITY_ONE};
    }

    /** Verify the constant-product invariant after offer execution.
     *
     *  Recomputes `k = balances_.in × balances_.out` and the post-trade
     *  product `k' = (balances_.in + consumed.in) × (balances_.out -
     *  consumed.out)`.  The check passes when `k' >= k` (exact conservation)
     *  or when the relative decrease is within `1e-7`, a tolerance that absorbs
     *  finite-precision rounding in the swap formulas without masking genuinely
     *  broken swaps.  Violations are logged at error level; the ledger is not
     *  aborted.
     *
     *  @param consumed  Amounts actually consumed in the trade.  Must not
     *      exceed `amounts_` in either dimension.
     *  @param j  Journal for error-level diagnostics on invariant failure.
     *  @return `true` if the invariant holds (within tolerance); `false`
     *      otherwise.
     */
    [[nodiscard]] bool
    checkInvariant(TAmounts<TIn, TOut> const& consumed, beast::Journal j) const;
};

}  // namespace xrpl
