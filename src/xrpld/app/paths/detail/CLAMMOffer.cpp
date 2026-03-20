#include <xrpld/app/paths/CLAMMOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <type_traits>

namespace xrpl {

template <typename TIn, typename TOut>
CLAMMOffer<TIn, TOut>::CLAMMOffer(
    CLAMMLiquidity<TIn, TOut> const& clammLiquidity,
    CLAMMPoolInfo const& pool,
    TAmounts<TIn, TOut> const& amounts,
    Quality const& quality,
    beast::Journal j)
    : clammLiquidity_(clammLiquidity)
    , pool_(pool)
    , amounts_(amounts)
    , quality_(quality)
    , j_(j)
    , consumed_(false)
{
}

template <typename TIn, typename TOut>
void
CLAMMOffer<TIn, TOut>::consume(
    ApplyView& view,
    TAmounts<TIn, TOut> const& consumed)
{
    consumed_ = true;
    clammLiquidity_.context().setCLAMMUsed();

    // Compute actual input amount consumed.
    // Convert TIn to STAmount: STAmount is identity, IOUAmount/XRPAmount
    // need toSTAmount with issue.
    STAmount stIn;
    if constexpr (std::is_same_v<TIn, STAmount>)
        stIn = consumed.in;
    else if constexpr (std::is_same_v<TIn, IOUAmount>)
        stIn = toSTAmount(consumed.in, issueIn());
    else
        stIn = toSTAmount(consumed.in);
    auto const amountIn = clamm::extractAmount(stIn);

    if (amountIn == 0)
        return;

    // Execute swap on pool SLE
    auto const clammKeylet = keylet::clamm(pool_.poolID);
    auto sleClamm = view.peek(clammKeylet);
    if (!sleClamm)
    {
        // Pool disappeared -- zero amounts so caller does not credit
        // tokens that were never swapped.
        amounts_ = TAmounts<TIn, TOut>{};
        return;
    }

    // Read fresh pool state from the SLE (may have been modified by
    // earlier steps in the same payment path).
    auto const freshSqrtPrice =
        clamm::fromSLEField(sleClamm->getFieldH128(sfSqrtPrice));
    auto const freshTick = sleClamm->getFieldI32(sfCurrentTick);
    auto const freshLiquidity =
        clamm::fromSLEField(sleClamm->getFieldH128(sfLiquidityAmount));
    auto const freshFeeGrowth0 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal0));
    auto const freshFeeGrowth1 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal1));

    auto const sqrtPriceLimit = pool_.zeroForOne
        ? clamm::minSqrtRatio() + 1
        : clamm::maxSqrtRatio() - 1;

    auto const result = clamm::applySwap(
        view,
        pool_.poolID,
        freshSqrtPrice,
        freshTick,
        freshLiquidity,
        pool_.tickSpacing,
        pool_.tradingFee,
        freshFeeGrowth0,
        freshFeeGrowth1,
        amountIn,
        pool_.zeroForOne,
        sqrtPriceLimit,
        j_);

    // Update pool SLE with swap results
    sleClamm->setFieldH128(
        sfSqrtPrice, clamm::toSLEField(result.finalSqrtPrice));
    sleClamm->setFieldI32(sfCurrentTick, result.finalTick);
    if (result.finalLiquidity > 0)
        sleClamm->setFieldH128(
            sfLiquidityAmount,
            clamm::toSLEField(result.finalLiquidity));
    else
        sleClamm->makeFieldAbsent(sfLiquidityAmount);
    // Always set feeGrowthGlobal -- never make absent (modular counters)
    sleClamm->setFieldH128(sfFeeGrowthGlobal0,
        clamm::toSLEField(result.feeGrowthGlobal0));
    sleClamm->setFieldH128(sfFeeGrowthGlobal1,
        clamm::toSLEField(result.feeGrowthGlobal1));

    view.update(sleClamm);
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
CLAMMOffer<TIn, TOut>::limitOut(
    TAmounts<TIn, TOut> const& offerAmount,
    TOut const& limit,
    bool roundUp) const
{
    // Use constant quality approximation (CLOBLike behavior).
    // This works well because the offer is sized by simulateSwap
    // which already accounts for tick crossing effects.
    if (clammLiquidity_.multiPath())
        return quality_.ceil_out_strict(offerAmount, limit, roundUp);
    return quality_.ceil_out(offerAmount, limit);
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
CLAMMOffer<TIn, TOut>::limitIn(
    TAmounts<TIn, TOut> const& offerAmount,
    TIn const& limit,
    bool roundUp) const
{
    if (clammLiquidity_.multiPath())
        return quality_.ceil_in_strict(offerAmount, limit, roundUp);
    return quality_.ceil_in(offerAmount, limit);
}

template <typename TIn, typename TOut>
QualityFunction
CLAMMOffer<TIn, TOut>::getQualityFunc() const
{
    // Always use CLOBLike quality function.
    // The offer is already sized correctly via simulateSwap.
    return QualityFunction{quality_, QualityFunction::CLOBLikeTag{}};
}

template <typename TIn, typename TOut>
bool
CLAMMOffer<TIn, TOut>::checkInvariant(
    TAmounts<TIn, TOut> const& consumed,
    beast::Journal j) const
{
    if (consumed.in > amounts_.in || consumed.out > amounts_.out)
    {
        JLOG(j.error()) << "CLAMMOffer invariant failed:"
                        << " consumed.in=" << to_string(consumed.in)
                        << " amounts.in=" << to_string(amounts_.in)
                        << " consumed.out=" << to_string(consumed.out)
                        << " amounts.out=" << to_string(amounts_.out);
        return false;
    }
    return true;
}

template class CLAMMOffer<STAmount, STAmount>;
template class CLAMMOffer<IOUAmount, IOUAmount>;
template class CLAMMOffer<XRPAmount, IOUAmount>;
template class CLAMMOffer<IOUAmount, XRPAmount>;

}  // namespace xrpl
