//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
//==============================================================================

#ifndef RIPPLE_APP_PATHS_IMPL_STRANDFLOW_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_STRANDFLOW_H_INCLUDED

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/Credit.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/paths/detail/AmountSpec.h>
#include <xrpld/app/paths/detail/FlatSets.h>
#include <xrpld/app/paths/detail/FlowDebugInfo.h>
#include <xrpld/app/paths/detail/Steps.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <boost/container/flat_set.hpp>

#include <algorithm>
#include <iterator>
#include <numeric>

namespace ripple {

/** Result of flow() execution of a single Strand. */
template <class TInAmt, class TOutAmt>
struct StrandResult
{
    bool success;                                  ///< Strand succeeded
    TInAmt in = beast::zero;                       ///< Currency amount in
    TOutAmt out = beast::zero;                     ///< Currency amount out
    std::optional<PaymentSandbox> sandbox;         ///< Resulting Sandbox state
    boost::container::flat_set<uint256> ofrsToRm;  ///< Offers to remove
    // Num offers consumed or partially consumed (includes expired and unfunded
    // offers)
    std::uint32_t ofrsUsed = 0;
    // strand can be inactive if there is no more liquidity or too many offers
    // have been consumed
    bool inactive = false;  ///< Strand should not considered as a further
                            ///< source of liquidity (dry)

    /** Strand result constructor */
    StrandResult() = default;

    StrandResult(
        Strand const& strand,
        TInAmt const& in_,
        TOutAmt const& out_,
        PaymentSandbox&& sandbox_,
        boost::container::flat_set<uint256> ofrsToRm_,
        bool inactive_)
        : success(true)
        , in(in_)
        , out(out_)
        , sandbox(std::move(sandbox_))
        , ofrsToRm(std::move(ofrsToRm_))
        , ofrsUsed(offersUsed(strand))
        , inactive(inactive_)
    {
    }

    StrandResult(
        Strand const& strand,
        boost::container::flat_set<uint256> ofrsToRm_)
        : success(false)
        , ofrsToRm(std::move(ofrsToRm_))
        , ofrsUsed(offersUsed(strand))
    {
    }
};

/**
   Request `out` amount from a strand

   @param baseView Trust lines and balances
   @param strand Steps of Accounts to ripple through and offer books to use
   @param maxIn Max amount of input allowed
   @param out Amount of output requested from the strand
   @param j Journal to write log messages to
   @return Actual amount in and out from the strand, errors, offers to remove,
           and payment sandbox
 */
template <class TInAmt, class TOutAmt>
StrandResult<TInAmt, TOutAmt>
flow(
    PaymentSandbox const& baseView,
    Strand const& strand,
    std::optional<TInAmt> const& maxIn,
    TOutAmt const& out,
    beast::Journal j)
{
    using Result = StrandResult<TInAmt, TOutAmt>;
    if (strand.empty())
    {
        JLOG(j.warn()) << "Empty strand passed to Liquidity";
        return {};
    }

    boost::container::flat_set<uint256> ofrsToRm;

    if (isDirectXrpToXrp<TInAmt, TOutAmt>(strand))
    {
        return Result{strand, std::move(ofrsToRm)};
    }

    try
    {
        std::size_t const s = strand.size();

        std::size_t limitingStep = strand.size();
        std::optional<PaymentSandbox> sb(&baseView);
        // The "all funds" view determines if an offer becomes unfunded or is
        // found unfunded
        // These are the account balances before the strand executes
        std::optional<PaymentSandbox> afView(&baseView);
        EitherAmount limitStepOut;
        {
            EitherAmount stepOut(out);
            for (auto i = s; i--;)
            {
                auto r = strand[i]->rev(*sb, *afView, ofrsToRm, stepOut);
                if (strand[i]->isZero(r.second))
                {
                    JLOG(j.trace()) << "Strand found dry in rev";
                    return Result{strand, std::move(ofrsToRm)};
                }

                if (i == 0 && maxIn && *maxIn < get<TInAmt>(r.first))
                {
                    // limiting - exceeded maxIn
                    // Throw out previous results
                    sb.emplace(&baseView);
                    limitingStep = i;

                    // re-execute the limiting step
                    r = strand[i]->fwd(
                        *sb, *afView, ofrsToRm, EitherAmount(*maxIn));
                    limitStepOut = r.second;

                    if (strand[i]->isZero(r.second))
                    {
                        JLOG(j.trace()) << "First step found dry";
                        return Result{strand, std::move(ofrsToRm)};
                    }
                    if (get<TInAmt>(r.first) != *maxIn)
                    {
                        // Something is very wrong
                        // throwing out the sandbox can only increase liquidity
                        // yet the limiting is still limiting
                        JLOG(j.fatal())
                            << "Re-executed limiting step failed. r.first: "
                            << to_string(get<TInAmt>(r.first))
                            << " maxIn: " << to_string(*maxIn);
                        UNREACHABLE(
                            "ripple::flow : first step re-executing the "
                            "limiting step failed");
                        return Result{strand, std::move(ofrsToRm)};
                    }
                }
                else if (!strand[i]->equalOut(r.second, stepOut))
                {
                    // limiting
                    // Throw out previous results
                    sb.emplace(&baseView);
                    afView.emplace(&baseView);
                    limitingStep = i;

                    // re-execute the limiting step
                    stepOut = r.second;
                    r = strand[i]->rev(*sb, *afView, ofrsToRm, stepOut);
                    limitStepOut = r.second;

                    if (strand[i]->isZero(r.second))
                    {
                        // A tiny input amount can cause this step to output
                        // zero. I.e. 10^-80 IOU into an IOU -> XRP offer.
                        JLOG(j.trace()) << "Limiting step found dry";
                        return Result{strand, std::move(ofrsToRm)};
                    }
                    if (!strand[i]->equalOut(r.second, stepOut))
                    {
                        // Something is very wrong
                        // throwing out the sandbox can only increase liquidity
                        // yet the limiting is still limiting
#ifndef NDEBUG
                        JLOG(j.fatal())
                            << "Re-executed limiting step failed. r.second: "
                            << r.second << " stepOut: " << stepOut;
#else
                        JLOG(j.fatal()) << "Re-executed limiting step failed";
#endif
                        UNREACHABLE(
                            "ripple::flow : limiting step re-executing the "
                            "limiting step failed");
                        return Result{strand, std::move(ofrsToRm)};
                    }
                }

                // prev node needs to produce what this node wants to consume
                stepOut = r.first;
            }
        }

        {
            EitherAmount stepIn(limitStepOut);
            for (auto i = limitingStep + 1; i < s; ++i)
            {
                auto const r = strand[i]->fwd(*sb, *afView, ofrsToRm, stepIn);
                if (strand[i]->isZero(r.second))
                {
                    // A tiny input amount can cause this step to output zero.
                    // I.e. 10^-80 IOU into an IOU -> XRP offer.
                    JLOG(j.trace()) << "Non-limiting step found dry";
                    return Result{strand, std::move(ofrsToRm)};
                }
                if (!strand[i]->equalIn(r.first, stepIn))
                {
                    // The limits should already have been found, so executing a
                    // strand forward from the limiting step should not find a
                    // new limit
#ifndef NDEBUG
                    JLOG(j.fatal())
                        << "Re-executed forward pass failed. r.first: "
                        << r.first << " stepIn: " << stepIn;
#else
                    JLOG(j.fatal()) << "Re-executed forward pass failed";
#endif
                    UNREACHABLE(
                        "ripple::flow : non-limiting step re-executing the "
                        "forward pass failed");
                    return Result{strand, std::move(ofrsToRm)};
                }
                stepIn = r.second;
            }
        }

        auto const strandIn = *strand.front()->cachedIn();
        auto const strandOut = *strand.back()->cachedOut();

#ifndef NDEBUG
        {
            // Check that the strand will execute as intended
            // Re-executing the strand will change the cached values
            PaymentSandbox checkSB(&baseView);
            PaymentSandbox checkAfView(&baseView);
            EitherAmount stepIn(*strand[0]->cachedIn());
            for (auto i = 0; i < s; ++i)
            {
                bool valid;
                std::tie(valid, stepIn) =
                    strand[i]->validFwd(checkSB, checkAfView, stepIn);
                if (!valid)
                {
                    JLOG(j.warn())
                        << "Strand re-execute check failed. Step: " << i;
                    break;
                }
            }
        }
#endif

        bool const inactive = std::any_of(
            strand.begin(),
            strand.end(),
            [](std::unique_ptr<Step> const& step) { return step->inactive(); });

        return Result(
            strand,
            get<TInAmt>(strandIn),
            get<TOutAmt>(strandOut),
            std::move(*sb),
            std::move(ofrsToRm),
            inactive);
    }
    catch (FlowException const&)
    {
        return Result{strand, std::move(ofrsToRm)};
    }
}

/// @cond INTERNAL
template <class TInAmt, class TOutAmt>
struct FlowResult
{
    TInAmt in = beast::zero;
    TOutAmt out = beast::zero;
    std::optional<PaymentSandbox> sandbox;
    boost::container::flat_set<uint256> removableOffers;
    TER ter = temUNKNOWN;

    FlowResult() = default;

    FlowResult(
        TInAmt const& in_,
        TOutAmt const& out_,
        PaymentSandbox&& sandbox_,
        boost::container::flat_set<uint256> ofrsToRm)
        : in(in_)
        , out(out_)
        , sandbox(std::move(sandbox_))
        , removableOffers(std::move(ofrsToRm))
        , ter(tesSUCCESS)
    {
    }

    FlowResult(TER ter_, boost::container::flat_set<uint256> ofrsToRm)
        : removableOffers(std::move(ofrsToRm)), ter(ter_)
    {
    }

    FlowResult(
        TER ter_,
        TInAmt const& in_,
        TOutAmt const& out_,
        boost::container::flat_set<uint256> ofrsToRm)
        : in(in_), out(out_), removableOffers(std::move(ofrsToRm)), ter(ter_)
    {
    }
};
/// @endcond

/// @cond INTERNAL
inline std::optional<Quality>
qualityUpperBound(ReadView const& v, Strand const& strand)
{
    Quality q{STAmount::uRateOne};
    std::optional<Quality> stepQ;
    DebtDirection dir = DebtDirection::issues;
    for (auto const& step : strand)
    {
        if (std::tie(stepQ, dir) = step->qualityUpperBound(v, dir); stepQ)
            q = composed_quality(q, *stepQ);
        else
            return std::nullopt;
    }
    return q;
};
/// @endcond

/// @cond INTERNAL
/** Limit remaining out only if one strand and limitQuality is included.
 * Targets one path payment with AMM where the average quality is linear
 * and instant quality is quadratic function of output. Calculating quality
 * function for the whole strand enables figuring out required output
 * to produce requested strand's limitQuality. Reducing the output,
 * increases quality of AMM steps, increasing the strand's composite
 * quality as the result.
 */
template <typename TOutAmt>
inline TOutAmt
limitOut(
    ReadView const& v,
    Strand const& strand,
    TOutAmt const& remainingOut,
    Quality const& limitQuality)
{
    std::optional<QualityFunction> stepQualityFunc;
    std::optional<QualityFunction> qf;
    DebtDirection dir = DebtDirection::issues;
    for (auto const& step : strand)
    {
        if (std::tie(stepQualityFunc, dir) = step->getQualityFunc(v, dir);
            stepQualityFunc)
        {
            if (!qf)
                qf = stepQualityFunc;
            else
                qf->combine(*stepQualityFunc);
        }
        else
            return remainingOut;
    }

    // QualityFunction is constant
    if (!qf || qf->isConst())
        return remainingOut;

    auto const out = [&]() {
        if (auto const out = qf->outFromAvgQ(limitQuality); !out)
            return remainingOut;
        else if constexpr (std::is_same_v<TOutAmt, XRPAmount>)
            return XRPAmount{*out};
        else if constexpr (std::is_same_v<TOutAmt, IOUAmount>)
            return IOUAmount{*out};
        else
            return STAmount{
                remainingOut.issue(), out->mantissa(), out->exponent()};
    }();
    // A tiny difference could be due to the round off
    if (withinRelativeDistance(out, remainingOut, Number(1, -9)))
        return remainingOut;
    return std::min(out, remainingOut);
};
/// @endcond

/// @cond INTERNAL
/* Track the non-dry strands

   flow will search the non-dry strands (stored in `cur_`) for the best
   available liquidity If flow doesn't use all the liquidity of a strand, that
   strand is added to `next_`. The strands in `next_` are searched after the
   current best liquidity is used.
 */
class ActiveStrands
{
private:
    // Strands to be explored for liquidity
    std::vector<Strand const*> cur_;
    // Strands that may be explored for liquidity on the next iteration
    std::vector<Strand const*> next_;

public:
    ActiveStrands(std::vector<Strand> const& strands)
    {
        cur_.reserve(strands.size());
        next_.reserve(strands.size());
        for (auto& strand : strands)
            next_.push_back(&strand);
    }

    // Start a new iteration in the search for liquidity
    // Set the current strands to the strands in `next_`
    void
    activateNext(ReadView const& v, std::optional<Quality> const& limitQuality)
    {
        // add the strands in `next_` to `cur_`, sorted by theoretical quality.
        // Best quality first.
        cur_.clear();
        if (v.rules().enabled(featureFlowSortStrands) && !next_.empty())
        {
            std::vector<std::pair<Quality, Strand const*>> strandQuals;
            strandQuals.reserve(next_.size());
            if (next_.size() > 1)  // no need to sort one strand
            {
                for (Strand const* strand : next_)
                {
                    if (!strand)
                    {
                        // should not happen
                        continue;
                    }
                    if (auto const qual = qualityUpperBound(v, *strand))
                    {
                        if (limitQuality && *qual < *limitQuality)
                        {
                            // If a strand's quality is ever over limitQuality
                            // it is no longer part of the candidate set. Note
                            // that when transfer fees are charged, and an
                            // account goes from redeeming to issuing then
                            // strand quality _can_ increase; However, this is
                            // an unusual corner case.
                            continue;
                        }
                        strandQuals.push_back({*qual, strand});
                    }
                }
                // must stable sort for deterministic order across different c++
                // standard library implementations
                std::stable_sort(
                    strandQuals.begin(),
                    strandQuals.end(),
                    [](auto const& lhs, auto const& rhs) {
                        // higher qualities first
                        return std::get<Quality>(lhs) > std::get<Quality>(rhs);
                    });
                next_.clear();
                next_.reserve(strandQuals.size());
                for (auto const& sq : strandQuals)
                {
                    next_.push_back(std::get<Strand const*>(sq));
                }
            }
        }
        std::swap(cur_, next_);
    }

    Strand const*
    get(size_t i) const
    {
        if (i >= cur_.size())
        {
            UNREACHABLE("ripple::ActiveStrands::get : input out of range");
            return nullptr;
        }
        return cur_[i];
    }

    void
    push(Strand const* s)
    {
        next_.push_back(s);
    }

    // Push the strands from index i to the end of cur_ to next_
    void
    pushRemainingCurToNext(size_t i)
    {
        if (i >= cur_.size())
            return;
        next_.insert(next_.end(), std::next(cur_.begin(), i), cur_.end());
    }

    auto
    size() const
    {
        return cur_.size();
    }

    void
    removeIndex(std::size_t i)
    {
        if (i >= next_.size())
            return;
        next_.erase(next_.begin() + i);
    }
};
/// @endcond

/**
   Request `out` amount from a collection of strands

   Attempt to fulfill the payment by using liquidity from the strands in order
   from least expensive to most expensive

   @param baseView Trust lines and balances
   @param strands Each strand contains the steps of accounts to ripple through
                  and offer books to use
   @param outReq Amount of output requested from the strand
   @param partialPayment If true allow less than the full payment
   @param offerCrossing If true offer crossing, not handling a standard payment
   @param limitQuality If present, the minimum quality for any strand taken
   @param sendMaxST If present, the maximum STAmount to send
   @param j Journal to write journal messages to
   @param ammContext counts iterations with AMM offers
   @param flowDebugInfo If pointer is non-null, write flow debug info here
   @return Actual amount in and out from the strands, errors, and payment
   sandbox
*/
template <class TInAmt, class TOutAmt>
FlowResult<TInAmt, TOutAmt>
flow(
    PaymentSandbox const& baseView,
    std::vector<Strand> const& strands,
    TOutAmt const& outReq,
    bool partialPayment,
    OfferCrossing offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMaxST,
    beast::Journal j,
    AMMContext& ammContext,
    path::detail::FlowDebugInfo* flowDebugInfo = nullptr)
{
    // Used to track the strand that offers the best quality (output/input
    // ratio)
    struct BestStrand
    {
        TInAmt in;
        TOutAmt out;
        PaymentSandbox sb;
        Strand const& strand;
        Quality quality;

        BestStrand(
            TInAmt const& in_,
            TOutAmt const& out_,
            PaymentSandbox&& sb_,
            Strand const& strand_,
            Quality const& quality_)
            : in(in_)
            , out(out_)
            , sb(std::move(sb_))
            , strand(strand_)
            , quality(quality_)
        {
        }
    };

    std::size_t const maxTries = 1000;
    std::size_t curTry = 0;
    std::uint32_t maxOffersToConsider = 1500;
    std::uint32_t offersConsidered = 0;

    // There is a bug in gcc that incorrectly warns about using uninitialized
    // values if `remainingIn` is initialized through a copy constructor. We can
    // get similar warnings for `sendMax` if it is initialized in the most
    // natural way. Using `make_optional`, allows us to work around this bug.
    TInAmt const sendMaxInit =
        sendMaxST ? toAmount<TInAmt>(*sendMaxST) : TInAmt{beast::zero};
    std::optional<TInAmt> const sendMax =
        (sendMaxST && sendMaxInit >= beast::zero)
        ? std::make_optional(sendMaxInit)
        : std::nullopt;
    std::optional<TInAmt> remainingIn =
        !!sendMax ? std::make_optional(sendMaxInit) : std::nullopt;
    // std::optional<TInAmt> remainingIn{sendMax};

    TOutAmt remainingOut(outReq);

    PaymentSandbox sb(&baseView);

    // non-dry strands
    ActiveStrands activeStrands(strands);

    // Keeping a running sum of the amount in the order they are processed
    // will not give the best precision. Keep a collection so they may be summed
    // from smallest to largest
    boost::container::flat_multiset<TInAmt> savedIns;
    savedIns.reserve(maxTries);
    boost::container::flat_multiset<TOutAmt> savedOuts;
    savedOuts.reserve(maxTries);

    auto sum = [](auto const& col) {
        using TResult = std::decay_t<decltype(*col.begin())>;
        if (col.empty())
            return TResult{beast::zero};
        return std::accumulate(col.begin() + 1, col.end(), *col.begin());
    };

    // These offers only need to be removed if the payment is not
    // successful
    boost::container::flat_set<uint256> ofrsToRmOnFail;

    while (remainingOut > beast::zero &&
           (!remainingIn || *remainingIn > beast::zero))
    {
        ++curTry;
        if (curTry >= maxTries)
        {
            return {telFAILED_PROCESSING, std::move(ofrsToRmOnFail)};
        }

        activeStrands.activateNext(sb, limitQuality);

        ammContext.setMultiPath(activeStrands.size() > 1);

        // Limit only if one strand and limitQuality
        auto const limitRemainingOut = [&]() {
            if (activeStrands.size() == 1 && limitQuality)
                if (auto const strand = activeStrands.get(0))
                    return limitOut(sb, *strand, remainingOut, *limitQuality);
            return remainingOut;
        }();
        auto const adjustedRemOut = limitRemainingOut != remainingOut;

        boost::container::flat_set<uint256> ofrsToRm;
        std::optional<BestStrand> best;
        if (flowDebugInfo)
            flowDebugInfo->newLiquidityPass();
        // Index of strand to mark as inactive (remove from the active list) if
        // the liquidity is used. This is used for strands that consume too many
        // offers Constructed as `false,0` to workaround a gcc warning about
        // uninitialized variables
        std::optional<std::size_t> markInactiveOnUse;
        for (size_t strandIndex = 0, sie = activeStrands.size();
             strandIndex != sie;
             ++strandIndex)
        {
            Strand const* strand = activeStrands.get(strandIndex);
            if (!strand)
            {
                // should not happen
                continue;
            }
            // Clear AMM liquidity used flag. The flag might still be set if
            // the previous strand execution failed. It has to be reset
            // since this strand might not have AMM liquidity.
            ammContext.clear();
            if (offerCrossing && limitQuality)
            {
                auto const strandQ = qualityUpperBound(sb, *strand);
                if (!strandQ || *strandQ < *limitQuality)
                    continue;
            }
            auto f = flow<TInAmt, TOutAmt>(
                sb, *strand, remainingIn, limitRemainingOut, j);

            // rm bad offers even if the strand fails
            SetUnion(ofrsToRm, f.ofrsToRm);

            offersConsidered += f.ofrsUsed;

            if (!f.success || f.out == beast::zero)
                continue;

            if (flowDebugInfo)
                flowDebugInfo->pushLiquiditySrc(
                    EitherAmount(f.in), EitherAmount(f.out));

            XRPL_ASSERT(
                f.out <= remainingOut && f.sandbox &&
                    (!remainingIn || f.in <= *remainingIn),
                "ripple::flow : remaining constraints");

            Quality const q(f.out, f.in);

            JLOG(j.trace())
                << "New flow iter (iter, in, out): " << curTry - 1 << " "
                << to_string(f.in) << " " << to_string(f.out);

            // limitOut() finds output to generate exact requested
            // limitQuality. But the actual limit quality might be slightly
            // off due to the round off.
            if (limitQuality && q < *limitQuality &&
                (!adjustedRemOut ||
                 !withinRelativeDistance(q, *limitQuality, Number(1, -7))))
            {
                JLOG(j.trace())
                    << "Path rejected by limitQuality"
                    << " limit: " << *limitQuality << " path q: " << q;
                continue;
            }

            if (baseView.rules().enabled(featureFlowSortStrands))
            {
                XRPL_ASSERT(!best, "ripple::flow : best is unset");
                if (!f.inactive)
                    activeStrands.push(strand);
                best.emplace(f.in, f.out, std::move(*f.sandbox), *strand, q);
                activeStrands.pushRemainingCurToNext(strandIndex + 1);
                break;
            }

            activeStrands.push(strand);

            if (!best || best->quality < q ||
                (best->quality == q && best->out < f.out))
            {
                // If this strand is inactive (because it consumed too many
                // offers) and ends up having the best quality, remove it
                // from the activeStrands. If it doesn't end up having the
                // best quality, keep it active.

                if (f.inactive)
                {
                    // This should be `nextSize`, not `size`. This issue is
                    // fixed in featureFlowSortStrands.
                    markInactiveOnUse = activeStrands.size() - 1;
                }
                else
                {
                    markInactiveOnUse.reset();
                }

                best.emplace(f.in, f.out, std::move(*f.sandbox), *strand, q);
            }
        }

        bool const shouldBreak = [&] {
            if (baseView.rules().enabled(featureFlowSortStrands))
                return !best || offersConsidered >= maxOffersToConsider;
            return !best;
        }();

        if (best)
        {
            if (markInactiveOnUse)
            {
                activeStrands.removeIndex(*markInactiveOnUse);
                markInactiveOnUse.reset();
            }
            savedIns.insert(best->in);
            savedOuts.insert(best->out);
            remainingOut = outReq - sum(savedOuts);
            if (sendMax)
                remainingIn = *sendMax - sum(savedIns);

            if (flowDebugInfo)
                flowDebugInfo->pushPass(
                    EitherAmount(best->in),
                    EitherAmount(best->out),
                    activeStrands.size());

            JLOG(j.trace()) << "Best path: in: " << to_string(best->in)
                            << " out: " << to_string(best->out)
                            << " remainingOut: " << to_string(remainingOut);

            best->sb.apply(sb);
            ammContext.update();
        }
        else
        {
            JLOG(j.trace()) << "All strands dry.";
        }

        best.reset();  // view in best must be destroyed before modifying base
                       // view
        if (!ofrsToRm.empty())
        {
            SetUnion(ofrsToRmOnFail, ofrsToRm);
            for (auto const& o : ofrsToRm)
            {
                if (auto ok = sb.peek(keylet::offer(o)))
                    offerDelete(sb, ok, j);
            }
        }

        if (shouldBreak)
            break;
    }

    auto const actualOut = sum(savedOuts);
    auto const actualIn = sum(savedIns);

    JLOG(j.trace()) << "Total flow: in: " << to_string(actualIn)
                    << " out: " << to_string(actualOut);

    /* flowCross doesn't handle offer crossing with tfFillOrKill flag correctly.
     * 1. If tfFillOrKill is set then the owner must receive the full
     *   TakerPays. We reverse pays and gets because during crossing
     *   we are taking, therefore the owner must deliver the full TakerPays and
     *   the entire TakerGets doesn't have to be spent.
     *   Pre-fixFillOrKill amendment code fails if the entire TakerGets
     *   is not spent. fixFillOrKill addresses this issue.
     * 2. If tfSell is also set then the owner must spend the entire TakerGets
     *   even if it means obtaining more than TakerPays. Since the pays and gets
     *   are reversed, the owner must send the entire TakerGets.
     */
    bool const fillOrKillEnabled = baseView.rules().enabled(fixFillOrKill);

    if (actualOut != outReq)
    {
        if (actualOut > outReq)
        {
            // Rounding in the payment engine is causing this assert to
            // sometimes fire with "dust" amounts. This is causing issues when
            // running debug builds of rippled. While this issue still needs to
            // be resolved, the assert is causing more harm than good at this
            // point.
            // UNREACHABLE("ripple::flow : rounding error");

            return {tefEXCEPTION, std::move(ofrsToRmOnFail)};
        }
        if (!partialPayment)
        {
            // If we're offerCrossing a !partialPayment, then we're
            // handling tfFillOrKill.
            // Pre-fixFillOrKill amendment:
            //   That case is handled below; not here.
            // fixFillOrKill amendment:
            //   That case is handled here if tfSell is also not set; i.e,
            //   case 1.
            if (!offerCrossing ||
                (fillOrKillEnabled && offerCrossing != OfferCrossing::sell))
                return {
                    tecPATH_PARTIAL,
                    actualIn,
                    actualOut,
                    std::move(ofrsToRmOnFail)};
        }
        else if (actualOut == beast::zero)
        {
            return {tecPATH_DRY, std::move(ofrsToRmOnFail)};
        }
    }
    if (offerCrossing &&
        (!partialPayment &&
         (!fillOrKillEnabled || offerCrossing == OfferCrossing::sell)))
    {
        // If we're offer crossing and partialPayment is *not* true, then
        // we're handling a FillOrKill offer.  In this case remainingIn must
        // be zero (all funds must be consumed) or else we kill the offer.
        // Pre-fixFillOrKill amendment:
        //   Handles both cases 1. and 2.
        // fixFillOrKill amendment:
        //   Handles 2. 1. is handled above and falls through for tfSell.
        XRPL_ASSERT(remainingIn, "ripple::flow : nonzero remainingIn");
        if (remainingIn && *remainingIn != beast::zero)
            return {
                tecPATH_PARTIAL,
                actualIn,
                actualOut,
                std::move(ofrsToRmOnFail)};
    }

    return {actualIn, actualOut, std::move(sb), std::move(ofrsToRmOnFail)};
}

}  // namespace ripple

#endif
