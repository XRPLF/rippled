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

#ifndef RIPPLE_APP_PATHS_IMPL_FLOW_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_FLOW_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/impl/XRPEndpointStep.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/AmountSpec.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/XRPAmount.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template<class TInAmt, class TOutAmt>
struct StrandResult
{
    TER ter = temUNKNOWN;
    TInAmt in = beast::zero;
    TOutAmt out = beast::zero;
    boost::optional<PaymentSandbox> sandbox;
    std::vector<uint256> ofrsToRm; // offers to remove

    StrandResult () = default;

    StrandResult (TInAmt const& in_,
        TOutAmt const& out_,
        PaymentSandbox&& sandbox_,
        std::vector<uint256> ofrsToRm_)
        : ter (tesSUCCESS)
        , in (in_)
        , out (out_)
        , sandbox (std::move (sandbox_))
        , ofrsToRm (std::move (ofrsToRm_))
    {
    }

    StrandResult (TER ter_, std::vector<uint256> ofrsToRm_)
        : ter (ter_), ofrsToRm (std::move (ofrsToRm_))
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
template<class TInAmt, class TOutAmt>
StrandResult <TInAmt, TOutAmt>
flow (
    PaymentSandbox const& baseView,
    Strand const& strand,
    boost::optional<TInAmt> const& maxIn,
    TOutAmt const& out,
    beast::Journal j)
{
    using Result = StrandResult<TInAmt, TOutAmt>;
    if (strand.empty ())
    {
        JLOG (j.warning) << "Empty strand passed to Liquidity";
        return {};
    }

    std::vector<uint256> ofrsToRm;

    if (strand.size() == 2 &&
        dynamic_cast<XRPEndpointStep const*>(strand[0].get()) &&
        dynamic_cast<XRPEndpointStep const*>(strand[1].get()))
    {
        // The current implementation returns NO_LINE for XRP->XRP transfers
        // keep this behavior
        return {tecNO_LINE, std::move (ofrsToRm)};
    }


    try
    {
        std::size_t const s = strand.size ();

        std::size_t limitingStep = strand.size ();
        boost::optional<PaymentSandbox> sb (&baseView);
        // The "all funds" view determines if an offer becomes unfunded or is
        // found unfunded
        // These are the account balances before the strand executes
        boost::optional<PaymentSandbox> afView (&baseView);
        EitherAmount limitStepOut;
        {
            EitherAmount stepOut (out);
            for (auto i = s; i--;)
            {
                auto r = strand[i]->rev (*sb, *afView, ofrsToRm, stepOut);
                if (strand[i]->dry (r.second))
                {
                    JLOG (j.trace) << "Strand found dry in rev";
                    return {tecPATH_DRY, std::move (ofrsToRm)};
                }

                if (i == 0 && maxIn && *maxIn < get<TInAmt> (r.first))
                {
                    // limiting - exceeded maxIn
                    // Throw out previous results
                    sb.emplace (&baseView);
                    limitingStep = i;

                    // re-execute the limiting step
                    r = strand[i]->fwd (
                        *sb, *afView, ofrsToRm, EitherAmount (*maxIn));
                    limitStepOut = r.second;

                    if (strand[i]->dry (r.second) ||
                        get<TInAmt> (r.first) != get<TInAmt> (*maxIn))
                    {
                        // Something is very wrong
                        // throwing out the sandbox can only increase liquidity
                        // yet the limiting is still limiting
                        JLOG (j.fatal) << "Re-executed limiting step failed";
                        assert (0);
                        return {telFAILED_PROCESSING, std::move (ofrsToRm)};
                    }
                }
                else if (!strand[i]->equalOut (r.second, stepOut))
                {
                    // limiting
                    // Throw out previous results
                    sb.emplace (&baseView);
                    afView.emplace (&baseView);
                    limitingStep = i;

                    // re-execute the limiting step
                    stepOut = r.second;
                    r = strand[i]->rev (*sb, *afView, ofrsToRm, stepOut);
                    limitStepOut = r.second;

                    if (strand[i]->dry (r.second) ||
                        !strand[i]->equalOut (r.second, stepOut))
                    {
                        // Something is very wrong
                        // throwing out the sandbox can only increase liquidity
                        // yet the limiting is still limiting
                        JLOG (j.fatal) << "Re-executed limiting step failed";
                        assert (0);
                        return {telFAILED_PROCESSING, std::move (ofrsToRm)};
                    }
                }

                // prev node needs to produce what this node wants to consume
                stepOut = r.first;
            }
        }

        {
            EitherAmount stepIn (limitStepOut);
            for (auto i = limitingStep + 1; i < s; ++i)
                stepIn = strand[i]->fwd (*sb, *afView, ofrsToRm, stepIn).second;
        }

        auto const strandIn = *strand.front ()->cachedIn ();
        auto const strandOut = *strand.back ()->cachedOut ();

#ifndef NDEBUG
        {
            // Check that the strand will execute as intended
            // Re-executing the strand will change the cached values
            PaymentSandbox checkSB (&baseView);
            PaymentSandbox checkAfView (&baseView);
            EitherAmount stepIn (*strand[0]->cachedIn ());
            for (auto i = 0; i < s; ++i)
            {
                bool valid;
                std::tie (valid, stepIn) =
                    strand[i]->validFwd (checkSB, checkAfView, stepIn);
                if (!valid)
                {
                    JLOG (j.trace)
                        << "Strand re-execute check failed. Step: " << i;
                    assert (0);
                    return {telFAILED_PROCESSING, std::move (ofrsToRm)};
                }
            }
        }
#endif

        return Result (get<TInAmt> (strandIn), get<TOutAmt> (strandOut),
            std::move (*sb), std::move (ofrsToRm));
    }
    catch (StepError const& e)
    {
        return {e.ter, std::move (ofrsToRm)};
    }
}

template<class TInAmt, class TOutAmt>
struct FlowResult
{
    TInAmt in = beast::zero;
    TOutAmt out = beast::zero;
    boost::optional<PaymentSandbox> sandbox;
    TER ter = temUNKNOWN;

    FlowResult () = default;

    FlowResult (TInAmt const& in_,
        TOutAmt const& out_,
        PaymentSandbox&& sandbox_)
        : in (in_)
        , out (out_)
        , sandbox (std::move (sandbox_))
        , ter (tesSUCCESS)
    {
    }

    FlowResult (TER ter_)
        : ter (ter_)
    {
    }

    FlowResult (TER ter_, TInAmt const& in_, TOutAmt const& out_)
        : in (in_)
        , out (out_)
        , ter (ter_)
    {
    }
};

/**
   Request `out` amount from a collection of strands

   Attempt to fullfill the payment by using liquidity from the strands in order
   from least expensive to most expensive

   @param baseView Trust lines and balances
   @param strands Each strand contains the steps of accounts to ripple through
                  and offer books to use
   @param outReq Amount of output requested from the strand
   @flowParams Constraints and options on the payment
   @param logs Logs to write journal messages to
   @return Actual amount in and out from the strands, errors, and payment sandbox
*/
template <class TInAmt, class TOutAmt>
FlowResult<TInAmt, TOutAmt>
flow (PaymentSandbox const& baseView,
    std::vector<Strand> const& strands,
    TOutAmt const& outReq,
    FlowParams const& flowParams,
    Logs& logs)
{
    using Result = FlowResult<TInAmt, TOutAmt>;
    struct BestStrand
    {
        TInAmt in;
        TOutAmt out;
        PaymentSandbox sb;
        Strand const& strand;
        Quality quality;

        BestStrand (TInAmt const& in_,
            TOutAmt const& out_,
            PaymentSandbox&& sb_,
            Strand const& strand_,
            Quality const& quality_)
            : in (in_)
            , out (out_)
            , sb (std::move (sb_))
            , strand (strand_)
            , quality (quality_)
        {
        }
    };

    auto j = logs.journal ("Flow");
    auto viewJ = logs.journal ("View");

    std::size_t const maxTries = 1000;
    std::size_t curTry = 0;

    boost::optional<TInAmt> const sendMax = [&flowParams]()->boost::optional<TInAmt>
    {
        if (flowParams.sendMax && *flowParams.sendMax >= beast::zero)
        {
            return toAmount<TInAmt> (*flowParams.sendMax);
        }
        return boost::none;
    }();
    TOutAmt remainingOut (outReq);
    boost::optional<TInAmt> remainingIn (sendMax);

    PaymentSandbox sb (&baseView);

    // activeStrands[curActiveI] contains the current active strands
    // activeStrands[!curActiveI] contains the next active strands
    std::array<std::vector<Strand const*>, 2> activeStrands;
    int curActiveI = 0;

    activeStrands[0].reserve (strands.size ());
    activeStrands[1].reserve (strands.size ());
    for (auto& strand : strands)
        activeStrands[!curActiveI].push_back (&strand);

    // Keeping a running sum of the amount in the order they are processed
    // will not give the best precision. Keep a collection so they may be summed
    // from smallest to largest
    boost::container::flat_multiset<TInAmt> savedIns;
    savedIns.reserve(maxTries);
    boost::container::flat_multiset<TOutAmt> savedOuts;
    savedOuts.reserve(maxTries);

    auto sum = [](auto const& col)
    {
        using TResult = std::decay_t<decltype (*col.begin ())>;
        if (col.empty ())
            return TResult{beast::zero};
        return std::accumulate (col.begin () + 1, col.end (), *col.begin ());
    };

    while (remainingOut > beast::zero &&
        (!remainingIn || *remainingIn > beast::zero))
    {
        ++curTry;
        if (curTry >= maxTries)
        {
            assert (0);
            return Result (telFAILED_PROCESSING);
        }

        activeStrands[curActiveI].clear ();
        curActiveI = !curActiveI;

        std::set<uint256> ofrsToRm;
        boost::optional<BestStrand> best;
        for (auto strand : activeStrands[curActiveI])
        {
            auto f = flow<TInAmt, TOutAmt> (
                sb, *strand, remainingIn, remainingOut, j);

            // rm bad offers even if the strand fails
            ofrsToRm.insert (f.ofrsToRm.begin (), f.ofrsToRm.end ());

            if (f.ter != tesSUCCESS || f.out == beast::zero)
                continue;

            assert (f.out <= remainingOut && f.sandbox &&
                (!remainingIn || f.in <= *remainingIn));

            Quality const q (f.out, f.in);

            if (flowParams.limitQuality && q < *flowParams.limitQuality)
            {
                JLOG (j.trace)
                    << "Path rejected by limitQuality"
                    << " limit: " << *flowParams.limitQuality
                    << " path q: " << q;
                continue;
            }

            activeStrands[!curActiveI].push_back (strand);

            if (!best || q > best->quality)
                best.emplace (f.in, f.out, std::move (*f.sandbox), *strand, q);
        }

        bool const shouldBreak = !bool(best);

        if (best)
        {
            savedIns.insert (best->in);
            savedOuts.insert (best->out);
            remainingOut = outReq - sum (savedOuts);
            if (sendMax)
                remainingIn = *sendMax - sum (savedIns);

            JLOG (j.trace)
                << "Best path: in: " << to_string (best->in)
                << " out: " << to_string (best->out)
                << " remainingOut: " << to_string (remainingOut);

            best->sb.apply (sb);
        }
        else
        {
            JLOG (j.trace) << "All strands dry.";
        }

        best.reset ();  // view in best must be destroyed before modifying base
                        // view
        for (auto const& o : ofrsToRm)
            if (auto ok = sb.peek (keylet::offer (o)))
                offerDelete (sb, ok, viewJ);

        if (shouldBreak)
            break;
    }

    auto const actualOut = sum (savedOuts);
    auto const actualIn = sum (savedIns);

    JLOG (j.trace)
        << "Total flow: in: " << to_string (actualIn)
        << " out: " << to_string (actualOut);

    if (actualOut != outReq)
    {
        if (actualOut > outReq)
        {
            assert (0);
            return {tefEXCEPTION};
        }
        if (!flowParams.partialPayment)
        {
            return {tecPATH_PARTIAL, actualIn, actualOut};
        }
        else if (actualOut == beast::zero)
        {
            return {tecPATH_DRY};
        }
    }

    return Result (actualIn, actualOut, std::move (sb));
}

} // ripple

#endif
