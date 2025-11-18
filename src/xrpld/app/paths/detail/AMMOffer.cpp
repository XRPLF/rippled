#include <xrpld/app/paths/AMMLiquidity.h>
#include <xrpld/app/paths/AMMOffer.h>

#include <xrpl/protocol/QualityFunction.h>

namespace ripple {

template <typename TIn, typename TOut>
AMMOffer<TIn, TOut>::AMMOffer(
    AMMLiquidity<TIn, TOut> const& ammLiquidity,
    TAmounts<TIn, TOut> const& amounts,
    TAmounts<TIn, TOut> const& balances,
    Quality const& quality)
    : ammLiquidity_(ammLiquidity)
    , amounts_(amounts)
    , balances_(balances)
    , quality_(quality)
    , consumed_(false)
{
}

template <typename TIn, typename TOut>
Issue const&
AMMOffer<TIn, TOut>::issueIn() const
{
    return ammLiquidity_.issueIn();
}

template <typename TIn, typename TOut>
AccountID const&
AMMOffer<TIn, TOut>::owner() const
{
    return ammLiquidity_.ammAccount();
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut> const&
AMMOffer<TIn, TOut>::amount() const
{
    return amounts_;
}

template <typename TIn, typename TOut>
void
AMMOffer<TIn, TOut>::consume(
    ApplyView& view,
    TAmounts<TIn, TOut> const& consumed)
{
    // Consumed offer must be less or equal to the original
    if (consumed.in > amounts_.in || consumed.out > amounts_.out)
        Throw<std::logic_error>("Invalid consumed AMM offer.");
    // AMM pool is updated when the amounts are transferred
    // in BookStep::consumeOffer().

    consumed_ = true;

    // Let the context know AMM offer is consumed
    ammLiquidity_.context().setAMMUsed();
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitOut(
    TAmounts<TIn, TOut> const& offrAmt,
    TOut const& limit,
    bool roundUp) const
{
    // Change the offer size proportionally to the original offer quality
    // to keep the strands quality order unchanged. The taker pays slightly
    // more for the offer in this case, which results in a slightly higher
    // pool product than the original pool product. I.e. if the original
    // pool is poolPays, poolGets and the offer is assetIn, assetOut then
    // poolPays * poolGets < (poolPays - assetOut) * (poolGets + assetIn)
    if (ammLiquidity_.multiPath())
    {
        // It turns out that the ceil_out implementation has some slop in
        // it, which ceil_out_strict removes.
        return quality().ceil_out_strict(offrAmt, limit, roundUp);
    }
    // Change the offer size according to the conservation function. The offer
    // quality is increased in this case, but it doesn't matter since there is
    // only one path.
    return {swapAssetOut(balances_, limit, ammLiquidity_.tradingFee()), limit};
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitIn(
    TAmounts<TIn, TOut> const& offrAmt,
    TIn const& limit,
    bool roundUp) const
{
    // See the comments above in limitOut().
    if (ammLiquidity_.multiPath())
    {
        if (auto const& rules = getCurrentTransactionRules();
            rules && rules->enabled(fixReducedOffersV2))
            return quality().ceil_in_strict(offrAmt, limit, roundUp);

        return quality().ceil_in(offrAmt, limit);
    }
    return {limit, swapAssetIn(balances_, limit, ammLiquidity_.tradingFee())};
}

template <typename TIn, typename TOut>
QualityFunction
AMMOffer<TIn, TOut>::getQualityFunc() const
{
    if (ammLiquidity_.multiPath())
        return QualityFunction{quality(), QualityFunction::CLOBLikeTag{}};
    return QualityFunction{
        balances_, ammLiquidity_.tradingFee(), QualityFunction::AMMTag{}};
}

template <typename TIn, typename TOut>
bool
AMMOffer<TIn, TOut>::checkInvariant(
    TAmounts<TIn, TOut> const& consumed,
    beast::Journal j) const
{
    if (consumed.in > amounts_.in || consumed.out > amounts_.out)
    {
        JLOG(j.error()) << "AMMOffer::checkInvariant failed: consumed "
                        << to_string(consumed.in) << " "
                        << to_string(consumed.out) << " amounts "
                        << to_string(amounts_.in) << " "
                        << to_string(amounts_.out);

        return false;
    }

    Number const product = balances_.in * balances_.out;
    auto const newBalances = TAmounts<TIn, TOut>{
        balances_.in + consumed.in, balances_.out - consumed.out};
    Number const newProduct = newBalances.in * newBalances.out;

    if (newProduct >= product ||
        withinRelativeDistance(product, newProduct, Number{1, -7}))
        return true;

    JLOG(j.error()) << "AMMOffer::checkInvariant failed: balances "
                    << to_string(balances_.in) << " "
                    << to_string(balances_.out) << " new balances "
                    << to_string(newBalances.in) << " "
                    << to_string(newBalances.out) << " product/newProduct "
                    << product << " " << newProduct << " diff "
                    << (product != Number{0}
                            ? to_string((product - newProduct) / product)
                            : "undefined");
    return false;
}

template class AMMOffer<STAmount, STAmount>;
template class AMMOffer<IOUAmount, IOUAmount>;
template class AMMOffer<XRPAmount, IOUAmount>;
template class AMMOffer<IOUAmount, XRPAmount>;

}  // namespace ripple
