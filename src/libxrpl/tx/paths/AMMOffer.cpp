#include <xrpl/tx/paths/AMMOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/protocol/AMMCore.h>
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

template <StepAmount TIn, StepAmount TOut>
void
AMMOffer<TIn, TOut>::consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed)
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

template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitOut(
    TAmounts<TIn, TOut> const& offerAmount,
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
        return quality().ceilOutStrict(offerAmount, limit, roundUp);
    }
    // Change the offer size according to the conservation function. The offer
    // quality is increased in this case, but it doesn't matter since there is
    // only one path.
    return {
        curveSwapOut(
            balances_,
            limit,
            ammLiquidity_.tradingFee(),
            ammLiquidity_.curveType(),
            ammLiquidity_.curveParams(),
            CurveContext{nullptr, &ammLiquidity_.ammID()}),
        limit};
}

template <StepAmount TIn, StepAmount TOut>
TAmounts<TIn, TOut>
AMMOffer<TIn, TOut>::limitIn(TAmounts<TIn, TOut> const& offerAmount, TIn const& limit, bool roundUp)
    const
{
    // See the comments above in limitOut().
    if (ammLiquidity_.multiPath())
    {
        if (auto const& rules = getCurrentTransactionRules();
            rules && rules->enabled(fixReducedOffersV2))
            return quality().ceilInStrict(offerAmount, limit, roundUp);

        return quality().ceilIn(offerAmount, limit);
    }
    return {
        limit,
        curveSwapIn(
            balances_,
            limit,
            ammLiquidity_.tradingFee(),
            ammLiquidity_.curveType(),
            ammLiquidity_.curveParams(),
            CurveContext{nullptr, &ammLiquidity_.ammID()})};
}

template <StepAmount TIn, StepAmount TOut>
QualityFunction
AMMOffer<TIn, TOut>::getQualityFunc() const
{
    if (ammLiquidity_.multiPath())
        return QualityFunction{quality(), QualityFunction::CLOBLikeTag{}};
    return QualityFunction{balances_, ammLiquidity_.tradingFee(), QualityFunction::AMMTag{}};
}

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

    auto const oldIn = toSTAmount(balances_.in);
    auto const oldOut = toSTAmount(balances_.out);
    auto const newBalances =
        TAmounts<TIn, TOut>{balances_.in + consumed.in, balances_.out - consumed.out};
    auto const newIn = toSTAmount(newBalances.in);
    auto const newOut = toSTAmount(newBalances.out);

    auto const ct = ammLiquidity_.curveType();
    if (auto const* curve = getCurve(ct, *getCurrentTransactionRules()))
    {
        if (curve->checkInvariant(oldIn, oldOut, newIn, newOut, ammLiquidity_.curveParams()))
            return true;

        JLOG(j.error()) << "AMMOffer::checkInvariant failed (curve " << static_cast<int>(ct)
                        << "): balances " << to_string(balances_.in) << " "
                        << to_string(balances_.out) << " consumed " << to_string(consumed.in) << " "
                        << to_string(consumed.out);
        return false;
    }

    JLOG(j.error()) << "AMMOffer::checkInvariant: unknown curve type " << static_cast<int>(ct);
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
