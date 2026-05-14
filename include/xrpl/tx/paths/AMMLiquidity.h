/** @file
 *  Declares `AMMLiquidity`, the adapter that exposes an on-ledger Automated
 *  Market Maker pool as a sequence of synthetic offers to the XRPL payment
 *  engine's `BookStep` traversal layer.
 *
 *  `AMMLiquidity` produces `AMMOffer<TIn,TOut>` objects — virtual offers sized
 *  from live pool state — so that `BookStep` can consume AMM liquidity
 *  identically to CLOB (Central Limit Order Book) offers during path execution.
 *  Two sizing strategies are used depending on whether the payment traverses
 *  a single path or multiple paths.
 */

#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

namespace xrpl {

template <StepAmount TIn, StepAmount TOut>
class AMMOffer;

/** Adapts an on-ledger AMM pool to the payment engine's offer-based interface.
 *
 *  `BookStep` iterates over discrete offers sorted by quality. `AMMLiquidity`
 *  bridges the continuous-liquidity AMM model into that interface by generating
 *  synthetic `AMMOffer<TIn,TOut>` objects on demand from live pool state.
 *
 *  Two sizing strategies are selected at construction time via `AMMContext`:
 *  - **Multi-path** (`ammContext_.multiPath()` true): `generateFibSeqOffer()`
 *    emits exponentially growing offers keyed to the iteration count, so each
 *    path strand gets a modest initial slice and larger slices only after
 *    prior iterations have established price quality.
 *  - **Single-path**: `changeSpotPriceQuality()` computes the exact swap that
 *    moves the pool's spot price to the competing CLOB offer's quality level,
 *    maximising value extraction in a single pass.
 *
 *  @tparam TIn  Amount type for the pool's input asset (`IOUAmount`,
 *      `XRPAmount`, or `MPTAmount`).
 *  @tparam TOut Amount type for the pool's output asset (`IOUAmount`,
 *      `XRPAmount`, or `MPTAmount`).
 *
 *  @note Not copyable. `AMMLiquidity` holds a mutable reference to `AMMContext`
 *      (shared state) and an immutable snapshot of pool balances captured at
 *      construction; a copy would alias that state without representing a
 *      coherent point in time. `BookStep` stores instances via
 *      `std::optional::emplace()` to avoid copies.
 *  @note Explicitly instantiated for all eight valid `(TIn, TOut)` pairs in
 *      `AMMLiquidity.cpp`; do not add implicit instantiations elsewhere.
 */
template <typename TIn, typename TOut>
class AMMLiquidity
{
private:
    /** Base fraction of `initialBalances_.in` used for the first Fibonacci
     *  offer (iteration 0).  Equals 5/20000 = 0.025% of the initial input
     *  balance, keeping the opening offer small relative to the pool.
     */
    inline static Number const kINITIAL_FIB_SEQ_PCT = Number(5) / 20000;
    AMMContext& ammContext_;
    AccountID const ammAccountID_;
    std::uint32_t const tradingFee_;
    Asset const assetIn_;
    Asset const assetOut_;
    /** Pool balances captured at construction time.
     *
     *  Used as the scaling base for Fibonacci offer sizes in
     *  `generateFibSeqOffer()`. Keeping these fixed across iterations ensures
     *  offer sizes are deterministic given the same starting state; using live
     *  balances would create feedback loops where earlier iterations change
     *  the sizes of later ones unpredictably.
     */
    TAmounts<TIn, TOut> const initialBalances_;
    beast::Journal const j_;

public:
    /** Construct an `AMMLiquidity` for the given AMM account.
     *
     *  Immediately fetches pool balances from `view` and stores them in
     *  `initialBalances_` for use as a Fibonacci scaling base.
     *
     *  @param view          Read-only ledger view used to fetch initial
     *      pool balances.
     *  @param ammAccountID  On-ledger account ID of the AMM pool.
     *  @param tradingFee    AMM trading fee in basis points (0–1000).
     *  @param in            Input-side asset of the pool.
     *  @param out           Output-side asset of the pool.
     *  @param ammContext    Shared context tracking iteration count and
     *      multi-path state; must outlive this object.
     *  @param j             Journal for diagnostic logging.
     *  @throws std::runtime_error if either pool balance is negative, which
     *      indicates ledger corruption.
     */
    AMMLiquidity(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Asset const& in,
        Asset const& out,
        AMMContext& ammContext,
        beast::Journal j);
    ~AMMLiquidity() = default;
    AMMLiquidity(AMMLiquidity const&) = delete;
    AMMLiquidity&
    operator=(AMMLiquidity const&) = delete;

    /** Generate a synthetic AMM offer for the current payment engine iteration.
     *
     *  Returns `std::nullopt` without generating an offer when:
     *  - `AMMContext::maxItersReached()` is true (30-iteration cap exhausted),
     *  - Either pool balance is zero (frozen account),
     *  - The pool's current spot-price quality is less than or within 1e-7 of
     *    `clobQuality` (AMM cannot profitably compete), or
     *  - The chosen sizing strategy produces an offer of zero size or overflow.
     *
     *  Strategy selection:
     *  - **Multi-path**: delegates to `generateFibSeqOffer()`, then discards
     *    the result if its quality is below `clobQuality`.
     *  - **Single-path, no CLOB**: delegates to `maxOffer()`; `BookStep` will
     *    trim the offer to the actual delivery limit.
     *  - **Single-path, with CLOB**: uses `changeSpotPriceQuality()` to size
     *    the offer so that full consumption moves the spot price to exactly
     *    `clobQuality`. Falls back to `maxOffer()` under `fixAMMv1_2` if
     *    `changeSpotPriceQuality()` returns nothing and `maxOffer()` beats
     *    `clobQuality`. On `std::overflow_error` (pre-`fixAMMOverflowOffer`)
     *    falls back to `maxOffer()` rather than propagating.
     *
     *  @param view         Current read-only ledger view; used to fetch live
     *      pool balances and check active amendments.
     *  @param clobQuality  Quality of the best competing CLOB offer, or
     *      `std::nullopt` if no CLOB offer is available on this strand.
     *  @return A synthetic `AMMOffer` priced from live pool state, or
     *      `std::nullopt` if the AMM has no profitable offer to contribute.
     */
    [[nodiscard]] std::optional<AMMOffer<TIn, TOut>>
    getOffer(ReadView const& view, std::optional<Quality> const& clobQuality) const;

    [[nodiscard]] AccountID const&
    ammAccount() const
    {
        return ammAccountID_;
    }

    [[nodiscard]] bool
    multiPath() const
    {
        return ammContext_.multiPath();
    }

    [[nodiscard]] std::uint32_t
    tradingFee() const
    {
        return tradingFee_;
    }

    [[nodiscard]] AMMContext&
    context() const
    {
        return ammContext_;
    }

    [[nodiscard]] Asset const&
    assetIn() const
    {
        return assetIn_;
    }

    [[nodiscard]] Asset const&
    assetOut() const
    {
        return assetOut_;
    }

private:
    /** Fetch live pool balances from the ledger.
     *
     *  @param view  Read-only ledger view.
     *  @return Balances as a `TAmounts<TIn, TOut>` pair.
     *  @throws std::runtime_error if either balance is negative, indicating
     *      ledger corruption (the AMM invariant checker guarantees non-negative
     *      balances under normal operation).
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    fetchBalances(ReadView const& view) const;

    /** Compute the offer size for one multi-path engine iteration using the
     *  Fibonacci sequence.
     *
     *  The base offer (`curIters == 0`) is `kINITIAL_FIB_SEQ_PCT × initialBalances_.in`
     *  (0.025% of the input balance at construction). For subsequent iterations
     *  the output amount is scaled by `kFIB[curIters - 1]`, a hard-coded
     *  30-entry Fibonacci table matching `AMMContext::kMAX_ITERATIONS`.
     *  Scaling against `initialBalances_` (not live balances) keeps offer
     *  sizes deterministic across iterations.
     *
     *  @param balances  Current live pool balances, used to derive the input
     *      amount via `swapAssetOut()` and to guard against overflow.
     *  @return Synthetic `TAmounts` representing the offer's `{in, out}` pair.
     *  @throws std::overflow_error if the computed output equals or exceeds
     *      `balances.out`; the caller (`getOffer`) catches this and falls back
     *      to `std::nullopt` or `maxOffer()` depending on the active amendments.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    generateFibSeqOffer(TAmounts<TIn, TOut> const& balances) const;

    /** Construct the largest safe synthetic offer against the pool.
     *
     *  Behaviour depends on the `fixAMMOverflowOffer` amendment:
     *  - **Active**: `takerGets = 99% × balances.out` (rounded down);
     *    `takerPays = swapAssetOut(takerGets)`. Returns `std::nullopt` if the
     *    99% cap rounds to zero or equals `balances.out` (degenerate pool).
     *  - **Inactive** (legacy): `takerPays = maxAmount<TIn>()` (protocol
     *    ceiling); `takerGets = swapAssetIn(takerPays)`. This path could
     *    overflow on large pools — the bug that motivated the amendment.
     *
     *  @param balances  Current live pool balances.
     *  @param rules     Active amendment rules, used to gate `fixAMMOverflowOffer`.
     *  @return The maximum-size `AMMOffer`, or `std::nullopt` if the pool is
     *      too small to produce a valid offer under the fixed path.
     */
    [[nodiscard]] std::optional<AMMOffer<TIn, TOut>>
    maxOffer(TAmounts<TIn, TOut> const& balances, Rules const& rules) const;
};

}  // namespace xrpl
