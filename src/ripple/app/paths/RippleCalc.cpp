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

#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/paths/Tuning.h>
#include <ripple/app/paths/impl/FlowDebugInfo.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>

namespace ripple {
namespace path {

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
    auto const inNative = saMaxAmountReq.native();
    auto const outNative = saDstAmountReq.native();

    Output flowV2Out;
    PaymentSandbox flowV2SB (&view);
    detail::FlowDebugInfo flowV2FlowDebugInfo (inNative, outNative);
    auto j = l.journal ("Flow");
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
                ownerPaysTransferFee, /* offerCrossing */ false, limitQuality, sendMax, j,
                nullptr);
        }
        catch (std::exception& e)
        {
            JLOG (j.error()) << "Exception from flow: " << e.what ();
            {
                // return a tec so the tx is stored
                path::RippleCalc::Output exceptResult;
                exceptResult.setResult(tecINTERNAL);
                return exceptResult;
            }
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
                (outputBalanceDiffs && balanceDiffs
                 ? ", " + detail::balanceDiffsToString(balanceDiffs)  : "") <<
                ", algo: " << algoName;
        };
        bool outputPassInfo = false;
        bool outputBalanceDiffs = false;
        boost::optional<BalanceDiffs> bdV2;

        logResult ("V2", flowV2Out, flowV2FlowDebugInfo, bdV2,
            outputPassInfo, outputBalanceDiffs);
    }

    flowV2SB.apply(view);
    return flowV2Out;
}

} // path
} // ripple
