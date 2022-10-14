//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/app/paths/AMMLiquidity.h>
#include <ripple/app/paths/AMMOffer.h>

namespace ripple {

template <typename TIn, typename TOut>
AMMOffer<TIn, TOut>::AMMOffer(
    AMMLiquidity<TIn, TOut> const& ammLiquidity,
    TAmounts<TIn, TOut> const& offer,
    std::optional<TAmounts<TIn, TOut>> const& balances)
    : ammLiquidity_(ammLiquidity), amounts_(offer), balances_(balances)
{
}

template <typename TIn, typename TOut>
Issue
AMMOffer<TIn, TOut>::issueIn() const
{
    return ammLiquidity_.issueIn();
}

template <typename TIn, typename TOut>
Issue
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

    // Let the context know AMM offer is consumed
    ammLiquidity_.context().setAMMUsed();
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitOut(
    TAmounts<TIn, TOut> const& offrAmt,
    TOut const& limit) const
{
    if (ammLiquidity_.multiPath())
        return quality().ceil_out(offrAmt, limit);
    return {swapAssetOut(*balances_, limit, ammLiquidity_.tradingFee()), limit};
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitIn(
    TAmounts<TIn, TOut> const& offrAmt,
    TIn const& limit) const
{
    if (ammLiquidity_.multiPath())
        return quality().ceil_in(offrAmt, limit);
    return {limit, swapAssetIn(*balances_, limit, ammLiquidity_.tradingFee())};
}

template class AMMOffer<STAmount, STAmount>;
template class AMMOffer<IOUAmount, IOUAmount>;
template class AMMOffer<XRPAmount, IOUAmount>;
template class AMMOffer<IOUAmount, XRPAmount>;

}  // namespace ripple