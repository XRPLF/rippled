#include <xrpl/tx/paths/AMMLiquidity.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/AMMOffer.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>

namespace xrpl {

template <typename TIn, typename TOut>
AMMLiquidity<TIn, TOut>::AMMLiquidity(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::uint32_t tradingFee,
    Asset const& in,
    Asset const& out,
    AMMContext& ammContext,
    beast::Journal j,
    std::uint8_t curveType)
    : ammContext_(ammContext)
    , ammAccountID_(ammAccountID)
    , tradingFee_(tradingFee)
    , assetIn_(in)
    , assetOut_(out)
    , initialBalances_{fetchBalances(view)}
    , j_(j)
    , curveType_(curveType)
    , ammID_(keylet::amm(in, out, curveType).key)
{
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMLiquidity<TIn, TOut>::fetchBalances(ReadView const& view) const
{
    auto const amountIn = ammAccountHolds(view, ammAccountID_, assetIn_);
    auto const amountOut = ammAccountHolds(view, ammAccountID_, assetOut_);
    // This should not happen.
    if (amountIn < beast::kZero || amountOut < beast::kZero)
        Throw<std::runtime_error>("AMMLiquidity: invalid balances");

    return TAmounts{get<TIn>(amountIn), get<TOut>(amountOut)};
}

template <typename TIn, typename TOut>
AMMLiquidity<TIn, TOut>::CurveState
AMMLiquidity<TIn, TOut>::curveState(ReadView const& view) const
{
    if (curveType_ == kCtConstantProduct)
        return {};
    auto const sle = view.read(keylet::amm(ammID_));
    if (!sle || curveType_ != kCtConcentratedLiquidity)
        return {.params = sle, .range = std::nullopt, .reserves = std::nullopt};
    auto const range = clCurrentRangeCapacity(view, ammID_, *sle, assetIn_ < assetOut_);
    if (!range)
        return {.params = sle, .range = std::nullopt, .reserves = std::nullopt};  // LCOV_EXCL_LINE
    auto const start = std::make_shared<SLE>(*sle);
    start->setFieldI32(sfCurrentTick, range->tick);
    start->setFieldNumber(sfSqrtPrice, STNumber{sfSqrtPrice, range->sqrtP});
    start->setFieldU64(sfActiveLiquidity, range->liquidity);
    return {.params = start, .range = range, .reserves = clRangeReserves(*start)};
}

template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
AMMLiquidity<TIn, TOut>::clRangeReserves(SLE const& params) const
{
    if (params.getFieldU64(sfActiveLiquidity) == 0)
        return std::nullopt;
    Number const liquidity{static_cast<std::int64_t>(params.getFieldU64(sfActiveLiquidity))};
    Number const sqrtP = clSqrtPrice(params);
    Number const reserve0 = liquidity / sqrtP;
    Number const reserve1 = liquidity * sqrtP;
    bool const zeroForOne = assetIn_ < assetOut_;
    try
    {
        return TAmounts<TIn, TOut>{
            toAmount<TIn>(
                assetIn_, zeroForOne ? reserve0 : reserve1, Number::RoundingMode::Downward),
            toAmount<TOut>(
                assetOut_, zeroForOne ? reserve1 : reserve0, Number::RoundingMode::Downward),
        };
    }
    catch (std::overflow_error const&)
    {
        return std::nullopt;
    }
}

template <typename TIn, typename TOut>
TAmounts<TIn, TOut>
AMMLiquidity<TIn, TOut>::generateFibSeqOffer(
    ReadView const& view,
    TAmounts<TIn, TOut> const& balances,
    SLE const* params) const
{
    TAmounts<TIn, TOut> cur{};
    CurveContext const cctx{.view = &view, .ammID = &ammID_};

    cur.in = toAmount<TIn>(
        getAsset(balances.in),
        kInitialFibSeqPct * initialBalances_.in,
        Number::RoundingMode::Upward);
    cur.out = curveSwapIn(
        initialBalances_, assetIn(), assetOut(), cur.in, tradingFee_, curveType_, params, cctx);

    if (ammContext_.curIters() == 0)
        return cur;

    // clang-format off
    static constexpr std::uint32_t kFib[AMMContext::kMaxIterations] = {
        1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987,
        1597, 2584, 4181, 6765, 10946, 17711, 28657, 46368, 75025, 121393,
        196418, 317811, 514229, 832040, 1346269,};
    // clang-format on

    XRPL_ASSERT(
        !ammContext_.maxItersReached(),
        "xrpl::AMMLiquidity::generateFibSeqOffer : maximum iterations");

    cur.out = toAmount<TOut>(
        getAsset(balances.out),
        cur.out * kFib[ammContext_.curIters() - 1],
        Number::RoundingMode::Downward);
    // swapAssetOut() returns negative in this case
    if (cur.out >= balances.out)
        Throw<std::overflow_error>("AMMLiquidity: generateFibSeqOffer exceeds the balance");

    cur.in = curveSwapOut(
        balances, assetIn(), assetOut(), cur.out, tradingFee_, curveType_, params, cctx);

    return cur;
}

namespace {
// The amount no greater than the limit; IOU amounts round down as well.
template <typename T>
T
capOut(Asset const& asset, Number const& limit)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
    return toAmount<T>(asset, limit, Number::RoundingMode::Downward);
}

template <typename T>
T
maxOut(T const& out, Asset const& asset)
{
    Number const res = out * Number{99, -2};
    return toAmount<T>(asset, res, Number::RoundingMode::Downward);
}
}  // namespace

template <typename TIn, typename TOut>
Quality
AMMLiquidity<TIn, TOut>::spotQuality(TAmounts<TIn, TOut> const& balances, CurveState const& state)
    const
{
    if (state.reserves)
        return Quality{*state.reserves};
    if (curveType_ == kCtStableSwap)
    {
        if (auto const* curve = getCurrentCurve(curveType_))
        {
            if (auto const price = curve->spotPrice(
                    toSTAmount(balances.in, assetIn_),
                    toSTAmount(balances.out, assetOut_),
                    0,
                    state.params.get());
                price && *price > Number{0})
            {
                // spotPrice is output per input before the fee; a quality is input per output.
                return Quality{Amounts{
                    STAmount{noIssue(), Number{1} / feeMult(tradingFee_)},
                    STAmount{noIssue(), *price}}};
            }
        }
    }
    return Quality{balances};
}

template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
AMMLiquidity<TIn, TOut>::sizeToQuality(
    TAmounts<TIn, TOut> const& balances,
    Quality const& clobQuality,
    CurveState const& state,
    ReadView const& view) const
{
    CurveContext const cctx{.view = &view, .ammID = &ammID_};
    auto const quote = [&](TOut const& out) -> std::optional<TAmounts<TIn, TOut>> {
        if (out <= TOut{0} || out >= balances.out)
            return std::nullopt;
        auto const in = curveSwapOut(
            balances,
            assetIn(),
            assetOut(),
            out,
            tradingFee_,
            curveType_,
            state.params.get(),
            cctx);
        if (in == toMaxAmount<TIn>(assetIn()))
            return std::nullopt;
        return TAmounts<TIn, TOut>{in, out};
    };
    auto const holds = [&](std::optional<TAmounts<TIn, TOut>> const& offer) {
        return offer &&
            spotQuality(
                TAmounts<TIn, TOut>{balances.in + offer->in, balances.out - offer->out}, state) >
            clobQuality;
    };

    auto const top = quote(maxOut<TOut>(balances.out, assetOut()));
    if (holds(top))
        return top;

    Number low{0};
    Number high{maxOut<TOut>(balances.out, assetOut())};
    for (int i = 0; i < 32; ++i)
    {
        Number const mid = (low + high) / 2;
        if (holds(quote(capOut<TOut>(assetOut(), mid))))
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }
    auto const amounts = quote(capOut<TOut>(assetOut(), low));
    if (!amounts || Quality{*amounts} < clobQuality)
        return std::nullopt;
    return amounts;
}

template <typename TIn, typename TOut>
std::optional<AMMOffer<TIn, TOut>>
AMMLiquidity<TIn, TOut>::maxOffer(
    ReadView const& view,
    TAmounts<TIn, TOut> const& balances,
    CurveState const& state) const
{
    CurveContext const cctx{.view = &view, .ammID = &ammID_};
    auto const& params = state.params;

    auto out = maxOut<TOut>(balances.out, assetOut());
    // A CL offer capped at its range pays the input that takes the price
    // exactly to the range's boundary, so consuming it crosses the tick.
    std::optional<TIn> boundaryIn;

    // Audit #19: for CL, cap the advertised output at what the current
    // tick range can deliver. Without this cap, curveSwapOut walks
    // across multiple tick crossings to satisfy the 99%-of-reserves
    // target, and the resulting offer's quality is a *blended* average
    // across ranges of different liquidity depths. BookStep then picks
    // by that blended quality, mispricing the AMM against CLOB and
    // against itself when multi-path routing is involved. Capping at
    // the within-range max makes each AMMOffer carry the marginal
    // quality of its own range; BookStep iterates naturally across
    // ranges as the tick state advances.
    //
    // Same reasoning for binned: cap at the active bin's output
    // reserve so the offer's quality reflects that single bin's price.
    // BookStep iterates and pulls the next bin's offer on the next
    // call as sfActiveBinID advances.
    if (curveType_ == kCtConcentratedLiquidity && params)
    {
        auto const& range = state.range;
        // No room in the walk's reach: the pool offers nothing this way.
        if (range && range->out <= Number{0})
            return std::nullopt;
        if (range)
        {
            auto const cap = capOut<TOut>(assetOut(), range->out);
            if (cap < out)
            {
                out = cap;
                boundaryIn = toAmount<TIn>(
                    assetIn(), range->in / feeMult(tradingFee_), Number::RoundingMode::Upward);
            }
        }
    }
    else if (curveType_ == kCtBinned && params)
    {
        bool const inIsAsset0 = assetIn_ < assetOut_;
        auto const atBin = maxBinnedOutputAtActiveBin(view, ammID_, *params, inIsAsset0);
        if (atBin && *atBin > Number{0})
        {
            auto const cap = capOut<TOut>(assetOut(), *atBin);
            if (cap < out)
                out = cap;
        }
    }

    if (out <= TOut{0} || out >= balances.out)
        return std::nullopt;
    auto const in = boundaryIn
        ? *boundaryIn
        : curveSwapOut(
              balances, assetIn(), assetOut(), out, tradingFee_, curveType_, params.get(), cctx);
    // A curve that cannot deliver `out` quotes the largest input: no offer.
    if (in == toMaxAmount<TIn>(assetIn()))
        return std::nullopt;
    TAmounts<TIn, TOut> const offered{in, out};
    // A binned offer trades at its bin's price, which the pool's holdings across bins do not show.
    return AMMOffer<TIn, TOut>(
        *this,
        offered,
        balances,
        curveType_ == kCtBinned ? Quality{offered} : spotQuality(balances, state),
        params);
}

template <typename TIn, typename TOut>
std::optional<AMMOffer<TIn, TOut>>
AMMLiquidity<TIn, TOut>::getOffer(ReadView const& view, std::optional<Quality> const& clobQuality)
    const
{
    // Can't generate more offers if multi-path.
    if (ammContext_.maxItersReached())
        return std::nullopt;

    auto const balances = fetchBalances(view);

    // Frozen accounts, or nothing to pay out. A CL or binned pool may hold only
    // the asset it pays out, so its input balance counts only through a freeze.
    bool const oneSided = curveType_ == kCtConcentratedLiquidity || curveType_ == kCtBinned;
    if (balances.out == beast::kZero ||
        (oneSided ? isFrozen(view, ammAccountID_, assetIn_) : balances.in == beast::kZero))
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, frozen accounts";
        return std::nullopt;
    }

    JLOG(j_.trace()) << "AMMLiquidity::getOffer balances " << to_string(initialBalances_.in) << " "
                     << to_string(initialBalances_.out) << " new balances "
                     << to_string(balances.in) << " " << to_string(balances.out);

    // Can't generate AMM with a better quality than CLOB's
    // quality if AMM's Spot Price quality is less than CLOB quality or is
    // within a threshold.
    // Spot price quality (SPQ) is calculated within some precision threshold.
    // On the next iteration, after SPQ is changed, the new SPQ might be close
    // to the requested clobQuality but not exactly and potentially SPQ may keep
    // on approaching clobQuality for many iterations. Checking for the quality
    // threshold prevents this scenario.
    // A CL pool trades inside its current range as constant product on that
    // range's virtual reserves, so its spot quality comes from them.
    // A StableSwap pool's from its curve's marginal price. A binned pool's is
    // its first populated bin's, known once its offer is sized.
    auto const state = curveState(view);
    auto const& clReserves = state.reserves;
    if (curveType_ != kCtBinned && clobQuality)
    {
        auto const spotPriceQ = spotQuality(balances, state);
        if (spotPriceQ <= clobQuality ||
            withinRelativeDistance(spotPriceQ, *clobQuality, Number(1, -7)))
        {
            JLOG(j_.trace()) << "AMMLiquidity::getOffer, higher clob quality";
            return std::nullopt;
        }
    }

    auto offer = [&] -> std::optional<AMMOffer<TIn, TOut>> {
        try
        {
            // Binned: always use the per-bin-capped maxOffer path. The
            // Fib sequence model assumes a continuous curve where bigger
            // offers have monotonically worse quality; bins are stepped
            // (each bin is its own quality level), so Fib-style
            // iteration produces blended-quality offers BookStep can't
            // route correctly. Per-bin-capped offers let BookStep
            // iterate one bin at a time as activeBinID advances.
            if (curveType_ == kCtBinned)
            {
                auto binned = maxOffer(view, balances, state);
                if (binned && clobQuality &&
                    (binned->quality() <= *clobQuality ||
                     withinRelativeDistance(binned->quality(), *clobQuality, Number(1, -7))))
                {
                    JLOG(j_.trace()) << "AMMLiquidity::getOffer, higher clob quality";
                    return std::nullopt;
                }
                return binned;
            }
            if (ammContext_.multiPath())
            {
                auto const amounts = generateFibSeqOffer(view, balances, state.params.get());
                if (clobQuality && Quality{amounts} < clobQuality)
                    return std::nullopt;
                return AMMOffer<TIn, TOut>(
                    *this, amounts, balances, Quality{amounts}, state.params);
            }
            if (!clobQuality)
            {
                // If there is no CLOB to compare against, return the largest
                // amount, which doesn't overflow. The size is going to be
                // changed in BookStep per either deliver amount limit, or
                // sendmax, or available output or input funds. Might return
                // nullopt if the pool is small.
                return maxOffer(view, balances, state);
            }
            if (curveType_ == kCtConcentratedLiquidity)
            {
                // Bring the range's price to the CLOB quality, inside the range;
                // else the range-capped offer when it beats the CLOB.
                auto const& range = state.range;
                // No room to move: the pool offers nothing this way, as in maxOffer.
                if (range && range->out <= Number{0})
                    return std::nullopt;
                if (clReserves)
                {
                    if (auto const amounts =
                            changeSpotPriceQuality(*clReserves, *clobQuality, tradingFee_, j_);
                        amounts && (!range || Number{amounts->out} <= range->out))
                    {
                        return AMMOffer<TIn, TOut>(
                            *this, *amounts, *clReserves, Quality{*amounts}, state.params);
                    }
                }
                if (auto const maxAMMOffer = maxOffer(view, balances, state);
                    maxAMMOffer && Quality{maxAMMOffer->amount()} > *clobQuality)
                    return maxAMMOffer;
                return std::nullopt;
            }
            if (curveType_ == kCtStableSwap)
            {
                // Size the offer on the curve's own swap amounts, not constant product's.
                if (auto const amounts = sizeToQuality(balances, *clobQuality, state, view))
                {
                    return AMMOffer<TIn, TOut>(
                        *this, *amounts, balances, spotQuality(balances, state), state.params);
                }
                return std::nullopt;
            }
            if (auto const amounts =
                    changeSpotPriceQuality(balances, *clobQuality, tradingFee_, j_))
            {
                return AMMOffer<TIn, TOut>(
                    *this, *amounts, balances, Quality{*amounts}, state.params);
            }
            if (view.rules().enabled(fixAMMv1_2))
            {
                if (auto const maxAMMOffer = maxOffer(view, balances, state);
                    maxAMMOffer && Quality{maxAMMOffer->amount()} > *clobQuality)
                    return maxAMMOffer;
            }
        }
        catch (std::overflow_error const& e)
        {
            JLOG(j_.error()) << "AMMLiquidity::getOffer overflow " << e.what();
            return std::nullopt;
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "AMMLiquidity::getOffer exception " << e.what();
        }
        return std::nullopt;
    }();

    if (offer)
    {
        if (offer->amount().in > beast::kZero && offer->amount().out > beast::kZero)
        {
            JLOG(j_.trace()) << "AMMLiquidity::getOffer, created " << to_string(offer->amount().in)
                             << "/" << assetIn_ << " " << to_string(offer->amount().out) << "/"
                             << assetOut_;
            return offer;
        }

        JLOG(j_.debug()) << "AMMLiquidity::getOffer, no valid offer " << ammContext_.multiPath()
                         << " " << ammContext_.curIters() << " "
                         << (clobQuality ? clobQuality->rate() : STAmount{}) << " "
                         << to_string(balances.in) << " " << to_string(balances.out);
    }

    return std::nullopt;
}

template class AMMLiquidity<IOUAmount, IOUAmount>;
template class AMMLiquidity<XRPAmount, IOUAmount>;
template class AMMLiquidity<IOUAmount, XRPAmount>;
template class AMMLiquidity<MPTAmount, MPTAmount>;
template class AMMLiquidity<XRPAmount, MPTAmount>;
template class AMMLiquidity<MPTAmount, XRPAmount>;
template class AMMLiquidity<MPTAmount, IOUAmount>;
template class AMMLiquidity<IOUAmount, MPTAmount>;

}  // namespace xrpl
