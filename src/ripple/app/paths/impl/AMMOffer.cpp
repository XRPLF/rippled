//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================/
#include <ripple/app/paths/AMMOffer.h>

#include <ripple/app/paths/AMMLiquidity.h>
#include <ripple/protocol/QualityFunction.h>

namespace ripple {

template <typename TIn, typename TOut>
AMMOffer<TIn, TOut>::AMMOffer(
    AMMLiquidity<TIn, TOut> const& ammLiquidity,
    TAmounts<TIn, TOut> const& amounts,
    std::optional<TAmounts<TIn, TOut>> const& balances,
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
Issue const&
AMMOffer<TIn, TOut>::issueOut() const
{
    return ammLiquidity_.issueOut();
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
    bool fixReducedOffers,
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
        if (fixReducedOffers)
            // It turns out that the ceil_out implementation has some slop in
            // it.  ceil_out_strict removes that slop.  But removing that slop
            // affects transaction outcomes, so the change must be made using
            // an amendment.
            return quality().ceil_out_strict(offrAmt, limit, roundUp);
        return quality().ceil_out(offrAmt, limit);
    }
    // Change the offer size according to the conservation function. The offer
    // quality is increased in this case, but it doesn't matter since there is
    // only one path.
    return {swapAssetOut(*balances_, limit, ammLiquidity_.tradingFee()), limit};
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitIn(
    TAmounts<TIn, TOut> const& offrAmt,
    TIn const& limit) const
{
    // See the comments above in limitOut().
    if (ammLiquidity_.multiPath())
        return quality().ceil_in(offrAmt, limit);
    return {limit, swapAssetIn(*balances_, limit, ammLiquidity_.tradingFee())};
}

template <typename TIn, typename TOut>
QualityFunction
AMMOffer<TIn, TOut>::getQualityFunc() const
{
    if (ammLiquidity_.multiPath())
        return QualityFunction{quality(), QualityFunction::CLOBLikeTag{}};
    return QualityFunction{
        *balances_, ammLiquidity_.tradingFee(), QualityFunction::AMMTag{}};
}

template class AMMOffer<STAmount, STAmount>;
template class AMMOffer<IOUAmount, IOUAmount>;
template class AMMOffer<XRPAmount, IOUAmount>;
template class AMMOffer<IOUAmount, XRPAmount>;

}  // namespace ripple
