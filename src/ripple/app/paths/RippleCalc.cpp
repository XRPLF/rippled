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

#include <BeastConfig.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/paths/Tuning.h>
#include <ripple/app/paths/cursor/PathCursor.h>
#include <ripple/app/paths/impl/FlowDebugInfo.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>

namespace ripple {
namespace path {

static
TER
deleteOffers (ApplyView& view,
    boost::container::flat_set<uint256> const& offers, beast::Journal j)
{
    for (auto& e: offers)
        if (TER r = offerDelete (view,
                view.peek(keylet::offer(e)), j))
            return r;
    return tesSUCCESS;
}

RippleCalc::Output RippleCalc::rippleCalculate (
    PaymentSandbox& view,

    // Compute paths using this ledger entry set.  Up to caller to actually
    // apply to ledger.

    // Issuer:
    //      XRP: xrpAccount()
    //  non-XRP: uSrcAccountID (for any issuer) or another account with
    //           trust node.
    STAmount const& saMaxAmountReq,             // --> -1 = no limit.

    // Issuer:
    //      XRP: xrpAccount()
    //  non-XRP: uDstAccountID (for any issuer) or another account with
    //           trust node.
    STAmount const& saDstAmountReq,

    AccountID const& uDstAccountID,
    AccountID const& uSrcAccountID,

    // A set of paths that are included in the transaction that we'll
    // explore for liquidity.
    STPathSet const& spsPaths,
    Logs& l,
    Input const* const pInputs)
{
    // call flow v1 and v2 so results may be compared
    bool const compareFlowV1V2 =
        view.rules ().enabled (featureCompareFlowV1V2);

    bool const useFlowV1Output =
        !view.rules().enabled(featureFlow);
    bool const callFlowV1 = useFlowV1Output || compareFlowV1V2;
    bool const callFlowV2 = !useFlowV1Output || compareFlowV1V2;

    Output flowV1Out;
    PaymentSandbox flowV1SB (&view);

    auto const inNative = saMaxAmountReq.native();
    auto const outNative = saDstAmountReq.native();
    detail::FlowDebugInfo flowV1FlowDebugInfo (inNative, outNative);
    if (callFlowV1)
    {
        auto const timeIt = flowV1FlowDebugInfo.timeBlock ("main");
        RippleCalc rc (
            flowV1SB,
            saMaxAmountReq,
            saDstAmountReq,
            uDstAccountID,
            uSrcAccountID,
            spsPaths,
            l);
        if (pInputs != nullptr)
        {
            rc.inputFlags = *pInputs;
        }

        auto result = rc.rippleCalculate (compareFlowV1V2 ? &flowV1FlowDebugInfo : nullptr);
        flowV1Out.setResult (result);
        flowV1Out.actualAmountIn = rc.actualAmountIn_;
        flowV1Out.actualAmountOut = rc.actualAmountOut_;
        if (result != tesSUCCESS && !rc.permanentlyUnfundedOffers_.empty ())
            flowV1Out.removableOffers = std::move (rc.permanentlyUnfundedOffers_);
    }

    Output flowV2Out;
    PaymentSandbox flowV2SB (&view);
    detail::FlowDebugInfo flowV2FlowDebugInfo (inNative, outNative);
    auto j = l.journal ("Flow");
    if (callFlowV2)
    {
        bool defaultPaths = true;
        bool partialPayment = false;
        boost::optional<Quality> limitQuality;
        boost::optional<STAmount> sendMax;

        if (pInputs)
        {
            defaultPaths = pInputs->defaultPathsAllowed;
            partialPayment = pInputs->partialPaymentAllowed;
            if (pInputs->limitQuality && saMaxAmountReq > beast::zero)
                limitQuality.emplace (
                    Amounts (saMaxAmountReq, saDstAmountReq));
        }

        if (saMaxAmountReq >= beast::zero ||
            saMaxAmountReq.getCurrency () != saDstAmountReq.getCurrency () ||
            saMaxAmountReq.getIssuer () != uSrcAccountID)
        {
            sendMax.emplace (saMaxAmountReq);
        }

        try
        {
            bool const ownerPaysTransferFee =
                    view.rules ().enabled (featureOwnerPaysFee);
            auto const timeIt = flowV2FlowDebugInfo.timeBlock ("main");
            flowV2Out = flow (flowV2SB, saDstAmountReq, uSrcAccountID,
                uDstAccountID, spsPaths, defaultPaths, partialPayment,
                ownerPaysTransferFee, limitQuality, sendMax, j,
                compareFlowV1V2 ? &flowV2FlowDebugInfo : nullptr);
        }
        catch (std::exception& e)
        {
            JLOG (j.trace()) << "Exception from flow" << e.what ();
            if (!useFlowV1Output)
                Rethrow();
        }
    }

    if (j.debug())
    {
        using BalanceDiffs = detail::BalanceDiffs;
        auto logResult = [&](std::string const& algoName,
            Output const& result,
            detail::FlowDebugInfo const& flowDebugInfo,
            boost::optional<BalanceDiffs> const& balanceDiffs,
            bool outputPassInfo,
            bool outputBalanceDiffs) {
                j.debug () << "RippleCalc Result> " <<
                " actualIn: " << result.actualAmountIn <<
                ", actualOut: " << result.actualAmountOut <<
                ", result: " << result.result () <<
                ", dstAmtReq: " << saDstAmountReq <<
                ", sendMax: " << saMaxAmountReq <<
                (compareFlowV1V2 ? ", " + flowDebugInfo.to_string (outputPassInfo): "") <<
                (outputBalanceDiffs && balanceDiffs
                 ? ", " + detail::balanceDiffsToString(balanceDiffs)  : "") <<
                ", algo: " << algoName;
        };
        bool outputPassInfo = false;
        bool outputBalanceDiffs = false;
        boost::optional<BalanceDiffs> bdV1, bdV2;
        if (compareFlowV1V2)
        {
            auto const v1r = flowV1Out.result ();
            auto const v2r = flowV2Out.result ();
            if (v1r != v2r ||
                (((v1r == tesSUCCESS) || (v1r == tecPATH_PARTIAL)) &&
                    ((flowV1Out.actualAmountIn !=
                         flowV2Out.actualAmountIn) ||
                        (flowV1Out.actualAmountOut !=
                            flowV2Out.actualAmountOut))))
            {
                outputPassInfo = true;
            }
            bdV1 = detail::balanceDiffs (flowV1SB, view);
            bdV2 = detail::balanceDiffs (flowV2SB, view);
            outputBalanceDiffs = bdV1 != bdV2;
        }

        if (callFlowV1)
        {
            logResult ("V1", flowV1Out, flowV1FlowDebugInfo, bdV1,
                outputPassInfo, outputBalanceDiffs);
        }
        if (callFlowV2)
        {
            logResult ("V2", flowV2Out, flowV2FlowDebugInfo, bdV2,
                outputPassInfo, outputBalanceDiffs);
        }
    }

    JLOG (j.trace()) << "Using old flow: " << useFlowV1Output;

    if (!useFlowV1Output)
    {
        flowV2SB.apply (view);
        return flowV2Out;
    }
    flowV1SB.apply (view);
    return flowV1Out;
}

bool RippleCalc::addPathState(STPath const& path, TER& resultCode)
{
    auto pathState = std::make_shared<PathState> (
        view, saDstAmountReq_, saMaxAmountReq_, j_);

    if (!pathState)
    {
        resultCode = temUNKNOWN;
        return false;
    }

    pathState->expandPath (
        path,
        uDstAccountID_,
        uSrcAccountID_);

    if (pathState->status() == tesSUCCESS)
        pathState->checkNoRipple (uDstAccountID_, uSrcAccountID_);

    if (pathState->status() == tesSUCCESS)
        pathState->checkFreeze ();

    pathState->setIndex (pathStateList_.size ());

    JLOG (j_.debug())
        << "rippleCalc: Build direct:"
        << " status: " << transToken (pathState->status());

    // Return if malformed.
    if (isTemMalformed (pathState->status()))
    {
        resultCode = pathState->status();
        return false;
    }

    if (pathState->status () == tesSUCCESS)
    {
        resultCode = pathState->status();
        pathStateList_.push_back (pathState);
    }
    else if (pathState->status () != terNO_LINE)
    {
        resultCode = pathState->status();
    }

    return true;
}

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.

// <-- TER: Only returns tepPATH_PARTIAL if partialPaymentAllowed.
TER RippleCalc::rippleCalculate (detail::FlowDebugInfo* flowDebugInfo)
{
    JLOG (j_.trace())
        << "rippleCalc>"
        << " saMaxAmountReq_:" << saMaxAmountReq_
        << " saDstAmountReq_:" << saDstAmountReq_;

    TER resultCode = temUNCERTAIN;
    permanentlyUnfundedOffers_.clear ();
    mumSource_.clear ();

    // YYY Might do basic checks on src and dst validity as per doPayment.

    // Incrementally search paths.
    if (inputFlags.defaultPathsAllowed)
    {
        if (!addPathState (STPath(), resultCode))
            return resultCode;
    }
    else if (spsPaths_.empty ())
    {
        JLOG (j_.debug())
            << "rippleCalc: Invalid transaction:"
            << "No paths and direct ripple not allowed.";

        return temRIPPLE_EMPTY;
    }

    // Build a default path.  Use saDstAmountReq_ and saMaxAmountReq_ to imply
    // nodes.
    // XXX Might also make a XRP bridge by default.

    JLOG (j_.trace())
        << "rippleCalc: Paths in set: " << spsPaths_.size ();

    // Now expand the path state.
    for (auto const& spPath: spsPaths_)
    {
        if (!addPathState (spPath, resultCode))
            return resultCode;
    }

    if (resultCode != tesSUCCESS)
        return (resultCode == temUNCERTAIN) ? terNO_LINE : resultCode;

    resultCode = temUNCERTAIN;

    actualAmountIn_ = saMaxAmountReq_.zeroed();
    actualAmountOut_ = saDstAmountReq_.zeroed();

    // When processing, we don't want to complicate directory walking with
    // deletion.
    const std::uint64_t uQualityLimit = inputFlags.limitQuality ?
            getRate (saDstAmountReq_, saMaxAmountReq_) : 0;

    // Offers that became unfunded.
    boost::container::flat_set<uint256> unfundedOffersFromBestPaths;

    int iPass = 0;
    auto const dcSwitch = amendmentRIPD1141(view.info().parentCloseTime);

    while (resultCode == temUNCERTAIN)
    {
        int iBest = -1;
        int iDry = 0;

        // True, if ever computed multi-quality.
        bool multiQuality = false;

        if (flowDebugInfo) flowDebugInfo->newLiquidityPass();
        // Find the best path.
        for (auto pathState : pathStateList_)
        {
            if (pathState->quality())
                // Only do active paths.
            {
                // If computing the only non-dry path, and not limiting quality,
                // compute multi-quality.
                multiQuality = dcSwitch
                    ? !inputFlags.limitQuality &&
                        ((pathStateList_.size () - iDry) == 1)
                    : ((pathStateList_.size () - iDry) == 1);

                // Update to current amount processed.
                pathState->reset (actualAmountIn_, actualAmountOut_);

                // Error if done, output met.
                PathCursor pc(*this, *pathState, multiQuality, j_);
                pc.nextIncrement ();

                // Compute increment.
                JLOG (j_.debug())
                    << "rippleCalc: AFTER:"
                    << " mIndex=" << pathState->index()
                    << " uQuality=" << pathState->quality()
                    << " rate=" << amountFromQuality (pathState->quality());

                if (flowDebugInfo)
                    flowDebugInfo->pushLiquiditySrc (
                        toEitherAmount (pathState->inPass ()),
                        toEitherAmount (pathState->outPass ()));

                if (!pathState->quality())
                {
                    // Path was dry.

                    ++iDry;
                }
                else if (pathState->outPass() == zero)
                {
                    // Path is not dry, but moved no funds
                    // This should never happen. Consider the path dry

                    JLOG (j_.warn())
                        << "rippleCalc: Non-dry path moves no funds";

                    assert (false);

                    pathState->setQuality (0);
                    ++iDry;
                }
                else
                {
                    if (!pathState->inPass() || !pathState->outPass())
                    {
                        JLOG (j_.debug())
                            << "rippleCalc: better:"
                            << " uQuality="
                            << amountFromQuality (pathState->quality())
                            << " inPass()=" << pathState->inPass()
                            << " saOutPass=" << pathState->outPass();
                    }

                    assert (pathState->inPass() && pathState->outPass());

                    JLOG (j_.debug())
                        << "Old flow iter (iter, in, out): "
                        << iPass << " "
                        << pathState->inPass() << " "
                        << pathState->outPass();

                    if ((!inputFlags.limitQuality ||
                         pathState->quality() <= uQualityLimit)
                        // Quality is not limited or increment has allowed
                        // quality.
                        && (iBest < 0
                            // Best is not yet set.
                            || PathState::lessPriority (
                                *pathStateList_[iBest], *pathState)))
                        // Current is better than set.
                    {
                        JLOG (j_.debug())
                            << "rippleCalc: better:"
                            << " mIndex=" << pathState->index()
                            << " uQuality=" << pathState->quality()
                            << " rate="
                            << amountFromQuality (pathState->quality())
                            << " inPass()=" << pathState->inPass()
                            << " saOutPass=" << pathState->outPass();

                        iBest   = pathState->index ();
                    }
                }
            }
        }

        ++iPass;

        if (auto stream = j_.debug())
        {
            stream
                << "rippleCalc: Summary:"
                << " Pass: " << iPass
                << " Dry: " << iDry
                << " Paths: " << pathStateList_.size ();
            for (auto pathState: pathStateList_)
            {
                stream
                    << "rippleCalc: "
                    << "Summary: " << pathState->index()
                    << " rate: "
                    << amountFromQuality (pathState->quality())
                    << " quality:" << pathState->quality()
                    << " best: " << (iBest == pathState->index ());
            }
        }

        if (iBest >= 0)
        {
            // Apply best path.
            auto pathState = pathStateList_[iBest];

            if (flowDebugInfo)
                flowDebugInfo->pushPass (toEitherAmount (pathState->inPass ()),
                    toEitherAmount (pathState->outPass ()),
                    pathStateList_.size () - iDry);

            JLOG (j_.debug ())
                << "rippleCalc: best:"
                << " uQuality=" << amountFromQuality (pathState->quality ())
                << " inPass()=" << pathState->inPass ()
                << " saOutPass=" << pathState->outPass () << " iBest=" << iBest;

            // Record best pass' offers that became unfunded for deletion on
            // success.

            unfundedOffersFromBestPaths.insert (
                pathState->unfundedOffers().begin (),
                pathState->unfundedOffers().end ());

            // Apply best pass' view
            pathState->view().apply(view);

            actualAmountIn_ += pathState->inPass();
            actualAmountOut_ += pathState->outPass();

            JLOG (j_.trace())
                    << "rippleCalc: best:"
                    << " uQuality="
                    << amountFromQuality (pathState->quality())
                    << " inPass()=" << pathState->inPass()
                    << " saOutPass=" << pathState->outPass()
                    << " actualIn=" << actualAmountIn_
                    << " actualOut=" << actualAmountOut_
                    << " iBest=" << iBest;

            if (multiQuality)
            {
                ++iDry;
                pathState->setQuality(0);
            }

            if (actualAmountOut_ == saDstAmountReq_)
            {
                // Done. Delivered requested amount.

                resultCode   = tesSUCCESS;
            }
            else if (actualAmountOut_ > saDstAmountReq_)
            {
                JLOG (j_.fatal())
                    << "rippleCalc: TOO MUCH:"
                    << " actualAmountOut_:" << actualAmountOut_
                    << " saDstAmountReq_:" << saDstAmountReq_;

                return tefEXCEPTION;  // TEMPORARY
                assert (false);
            }
            else if (actualAmountIn_ != saMaxAmountReq_ &&
                     iDry != pathStateList_.size ())
            {
                // Have not met requested amount or max send, try to do
                // more. Prepare for next pass.
                //
                // Merge best pass' umReverse.
                mumSource_.insert (
                    pathState->reverse().begin (), pathState->reverse().end ());

                if (iPass >= PAYMENT_MAX_LOOPS)
                {
                    // This payment is taking too many passes

                    JLOG (j_.error())
                       << "rippleCalc: pass limit";

                    resultCode = telFAILED_PROCESSING;
                }

            }
            else if (!inputFlags.partialPaymentAllowed)
            {
                // Have sent maximum allowed. Partial payment not allowed.

                resultCode   = tecPATH_PARTIAL;
            }
            else
            {
                // Have sent maximum allowed. Partial payment allowed.  Success.

                resultCode   = tesSUCCESS;
            }
        }
        // Not done and ran out of paths.
        else if (!inputFlags.partialPaymentAllowed)
        {
            // Partial payment not allowed.
            resultCode = tecPATH_PARTIAL;
        }
        // Partial payment ok.
        else if (!actualAmountOut_)
        {
            // No payment at all.
            resultCode = tecPATH_DRY;
        }
        else
        {
            // Don't apply any payment increments
            resultCode   = tesSUCCESS;
        }
    }

    if (resultCode == tesSUCCESS)
    {
        auto viewJ = logs_.journal ("View");
        resultCode = deleteOffers(view, unfundedOffersFromBestPaths, viewJ);
        if (resultCode == tesSUCCESS)
            resultCode = deleteOffers(view, permanentlyUnfundedOffers_, viewJ);
    }

    // If isOpenLedger, then ledger is not final, can vote no.
    if (resultCode == telFAILED_PROCESSING && !inputFlags.isLedgerOpen)
        return tecFAILED_PROCESSING;
    return resultCode;
}

} // path
} // ripple
