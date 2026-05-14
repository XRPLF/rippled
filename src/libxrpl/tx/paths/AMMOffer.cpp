/** @file
 *  Implements the `AMMOffer` adapter that presents an AMM pool as a
 *  synthetic offer to `BookStep`'s generic payment-engine inner loop.
 *
 *  `AMMOffer<TIn, TOut>` mirrors the interface of `TOffer` so that
 *  `BookStep` can consume AMM and CLOB liquidity through identical
 *  code paths. Eight explicit instantiations cover every valid pairing
 *  of `XRPAmount`, `IOUAmount`, and `MPTAmount`, keeping the
 *  implementation in `.cpp` rather than headers.
 *
 *  @see AMMLiquidity, BookStep, QualityFunction
 */
#include <xrpl/tx/paths/AMMOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/QualityFunction.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/AMMLiquidity.h>

#include <stdexcept>

namespace xrpl {

/** Construct an `AMMOffer` with the sizing and pool state decided by
 *  `AMMLiquidity::getOffer`.
 *
 *  @param ammLiquidity  The liquidity manager that owns this offer's
 *      `AMMContext` and provides pool metadata (account, assets, fee).
 *  @param amounts  Offer size as presented to `BookStep`. In multi-path
 *      mode this is a Fibonacci-sequence-scaled amount; in single-path
 *      mode it is either quality-matched or pool-draining.
 *  @param balances  Live pool token balances at the moment the offer was
 *      generated. Used by `limitOut`/`limitIn` in single-path mode to
 *      apply the constant-product swap formula.
 *  @param quality  Spot-price quality when `balances != amounts`; otherwise
 *      the quality implied by `amounts`. Fixed for multi-path, varying for
 *      single-path as the pool is consumed.
 */
template <StepAmount TIn, StepAmount TOut>
AMMOffer<TIn, TOut>::AMMOffer(
    AMMLiquidity<TIn, TOut> const& ammLiquidity,
    TAmounts<TIn, TOut> const& amounts,
    TAmounts<TIn, TOut> const& balances,
    Quality const& quality)
    : ammLiquidity_(ammLiquidity), amounts_(amounts), balances_(balances), quality_(quality)

{
}

template <StepAmount TIn, StepAmount TOut>
Asset const&
AMMOffer<TIn, TOut>::assetIn() const
{
    return ammLiquidity_.assetIn();
}

template <StepAmount TIn, StepAmount TOut>
Asset const&
AMMOffer<TIn, TOut>::assetOut() const
{
    return ammLiquidity_.assetOut();
}

template <StepAmount TIn, StepAmount TOut>
AccountID const&
AMMOffer<TIn, TOut>::owner() const
{
    return ammLiquidity_.ammAccount();
}

template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut> const&
AMMOffer<TIn, TOut>::amount() const
{
    return amounts_;
}

/** Mark this offer as consumed and notify the AMM execution context.
 *
 *  Validates that `consumed` does not exceed the initial offer size, sets
 *  the `consumed_` flag, and calls `AMMContext::setAMMUsed()` so the outer
 *  payment engine knows AMM liquidity was touched this iteration.
 *
 *  @note The `view` parameter is accepted for interface compatibility with
 *      `TOffer::consume` but is not used here. Actual pool balance updates
 *      are performed in `BookStep::consumeOffer()` via `accountSend`, which
 *      keeps all ledger mutations in one place.
 *  @note An AMM offer can only be consumed once per payment engine iteration;
 *      subsequent calls with the same offer will throw because `consumed.in`
 *      or `consumed.out` would exceed `amounts_`.
 *
 *  @param view      Mutable ledger view (unused; present for interface parity
 *      with `TOffer::consume`).
 *  @param consumed  The `in`/`out` amounts actually transferred. Must not
 *      exceed the initial `amounts_` in either dimension.
 *  @throws std::logic_error if `consumed.in > amounts_.in` or
 *      `consumed.out > amounts_.out`.
 */
template <StepAmount TIn, StepAmount TOut>
void
AMMOffer<TIn, TOut>::consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed)
{
    if (consumed.in > amounts_.in || consumed.out > amounts_.out)
        Throw<std::logic_error>("Invalid consumed AMM offer.");
    // AMM pool is updated when the amounts are transferred
    // in BookStep::consumeOffer().

    consumed_ = true;

    ammLiquidity_.context().setAMMUsed();
}

/** Resize the offer to deliver exactly `limit` units of the output asset.
 *
 *  **Multi-path mode**: resizes proportionally to the original quality via
 *  `Quality::ceilOutStrict`, keeping strand quality ordering stable. The
 *  taker overpays slightly relative to the raw AMM formula, ensuring the
 *  post-trade pool product `(poolPays − assetOut)(poolGets + assetIn)`
 *  exceeds the original `poolPays × poolGets`.
 *
 *  **Single-path mode**: applies the constant-product swap formula
 *  `swapAssetOut(balances_, limit, tradingFee())` for an exact result.
 *  Quality may shift but does not affect strand ordering since there is
 *  only one path.
 *
 *  @param offerAmount  Current offer size (used in multi-path proportional
 *      scaling; ignored in single-path mode).
 *  @param limit   Maximum output amount that can be delivered.
 *  @param roundUp Whether to round the input side up (passed to
 *      `ceilOutStrict` in multi-path mode).
 *  @return Resized `{in, out}` pair where `out <= limit`.
 */
template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitOut(
    TAmounts<TIn, TOut> const& offerAmount,
    TOut const& limit,
    bool roundUp) const
{
    // Multi-path: resize proportionally to original quality so that strand
    // quality ordering is preserved. The slight overpayment keeps the pool
    // product non-decreasing even under rounding.
    if (ammLiquidity_.multiPath())
    {
        return quality().ceilOutStrict(offerAmount, limit, roundUp);
    }
    // Single-path: use the conservation function directly. Quality may
    // increase, but that is acceptable with only one path.
    return {swapAssetOut(balances_, limit, ammLiquidity_.tradingFee()), limit};
}

/** Resize the offer to consume exactly `limit` units of the input asset.
 *
 *  Mirrors `limitOut` but caps on the input side.
 *
 *  **Multi-path mode**: resizes proportionally to the original quality,
 *  preserving strand ordering. When `fixReducedOffersV2` is active, uses
 *  `Quality::ceilInStrict` (removes a small rounding slop present in the
 *  older `ceilIn`). The older path is preserved for replay of historical
 *  ledgers where that amendment was inactive.
 *
 *  **Single-path mode**: applies `swapAssetIn(balances_, limit,
 *  tradingFee())` for an exact constant-product result.
 *
 *  @param offerAmount  Current offer size (used for proportional scaling in
 *      multi-path mode).
 *  @param limit   Maximum input amount the taker will supply.
 *  @param roundUp Whether to round the output side up (forwarded to
 *      `ceilInStrict` when `fixReducedOffersV2` is active).
 *  @return Resized `{in, out}` pair where `in <= limit`.
 */
template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp)
    const
{
    if (ammLiquidity_.multiPath())
    {
        if (auto const& rules = getCurrentTransactionRules();
            rules && rules->enabled(fixReducedOffersV2))
            return quality().ceilInStrict(offerAmount, limit, roundUp);

        return quality().ceilIn(offerAmount, limit);
    }
    return {limit, swapAssetIn(balances_, limit, ammLiquidity_.tradingFee())};
}

/** Return the quality function used by the path optimizer for this offer.
 *
 *  **Multi-path mode**: returns a constant `QualityFunction` (slope = 0,
 *  intercept = `quality_`), identical to a CLOB offer. Using a fixed rate
 *  prevents the AMM's varying spot price from disturbing the relative
 *  quality ordering of competing strands.
 *
 *  **Single-path mode**: returns an AMM quality function with a negative
 *  slope derived from the pool depth (`balances_` and `tradingFee()`).
 *  The optimizer uses this to solve in closed form for the output amount
 *  that meets the payment's requested quality limit.
 *
 *  @return A `QualityFunction` encoding the effective exchange rate for
 *      this offer as a linear function of output amount.
 */
template <StepAmount TIn, StepAmount TOut>
QualityFunction
AMMOffer<TIn, TOut>::getQualityFunc() const
{
    if (ammLiquidity_.multiPath())
        return QualityFunction{quality(), QualityFunction::CLOBLikeTag{}};
    return QualityFunction{balances_, ammLiquidity_.tradingFee(), QualityFunction::AMMTag{}};
}

/** Verify the constant-product invariant after offer execution.
 *
 *  First checks that `consumed` does not exceed the original offer size.
 *  Then recomputes the pre-trade pool product `k = balances_.in *
 *  balances_.out` and post-trade product `k' = (balances_.in +
 *  consumed.in) * (balances_.out - consumed.out)`.
 *
 *  The check passes when `k' >= k` (exact conservation) or when the
 *  relative deviation is within `1e-7` — a tolerance for finite-precision
 *  arithmetic that can cause `k'` to land just below `k`. Violations are
 *  logged at error level with full balance and product details for
 *  post-mortem analysis; the ledger is not aborted.
 *
 *  @param consumed  Amounts actually consumed in the trade. Must not exceed
 *      `amounts_` in either dimension.
 *  @param j  Journal for error-level diagnostics on invariant failure.
 *  @return `true` if the invariant holds (within tolerance); `false`
 *      otherwise.
 */
template <StepAmount TIn, StepAmount TOut>
bool
AMMOffer<TIn, TOut>::checkInvariant(TAmounts<TIn, TOut> const& consumed, beast::Journal j) const
{
    if (consumed.in > amounts_.in || consumed.out > amounts_.out)
    {
        JLOG(j.error()) << "AMMOffer::checkInvariant failed: consumed " << to_string(consumed.in)
                        << " " << to_string(consumed.out) << " amounts " << to_string(amounts_.in)
                        << " " << to_string(amounts_.out);

        return false;
    }

    Number const product = balances_.in * balances_.out;
    auto const newBalances =
        TAmounts<TIn, TOut>{balances_.in + consumed.in, balances_.out - consumed.out};
    Number const newProduct = newBalances.in * newBalances.out;

    if (newProduct >= product || withinRelativeDistance(product, newProduct, Number{1, -7}))
        return true;

    JLOG(j.error()) << "AMMOffer::checkInvariant failed: balances " << to_string(balances_.in)
                    << " " << to_string(balances_.out) << " new balances "
                    << to_string(newBalances.in) << " " << to_string(newBalances.out)
                    << " product/newProduct " << product << " " << newProduct << " diff "
                    << (product != Number{0} ? to_string((product - newProduct) / product)
                                             : "undefined");
    return false;
}

template class AMMOffer<IOUAmount, IOUAmount>;
template class AMMOffer<XRPAmount, IOUAmount>;
template class AMMOffer<IOUAmount, XRPAmount>;
template class AMMOffer<MPTAmount, MPTAmount>;
template class AMMOffer<XRPAmount, MPTAmount>;
template class AMMOffer<MPTAmount, XRPAmount>;
template class AMMOffer<IOUAmount, MPTAmount>;
template class AMMOffer<MPTAmount, IOUAmount>;

}  // namespace xrpl
