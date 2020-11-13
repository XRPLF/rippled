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

#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/impl/StrandFlow.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/XRPAmount.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template <class FlowResult>
static auto
finishFlow(
    PaymentSandbox& sb,
    Issue const& srcIssue,
    Issue const& dstIssue,
    FlowResult&& f)
{
    path::RippleCalc::Output result;
    if (f.ter == tesSUCCESS)
        f.sandbox->apply(sb);
    else
        result.removableOffers = std::move(f.removableOffers);

    result.setResult(f.ter);
    result.actualAmountIn = toSTAmount(f.in, srcIssue);
    result.actualAmountOut = toSTAmount(f.out, dstIssue);

    return result;
};

path::RippleCalc::Output
flow(
    PaymentSandbox& sb,
    STAmount const& deliver,
    AccountID const& src,
    AccountID const& dst,
    STPathSet const& paths,
    bool defaultPaths,
    bool partialPayment,
    bool ownerPaysTransferFee,
    bool offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMax,
    beast::Journal j,
    path::detail::FlowDebugInfo* flowDebugInfo)
{
    Issue const srcIssue = [&] {
        if (sendMax)
            return sendMax->issue();
        if (!isXRP(deliver.issue().currency))
            return Issue(deliver.issue().currency, src);
        return xrpIssue();
    }();

    Issue const dstIssue = deliver.issue();

    std::optional<Issue> sendMaxIssue;
    if (sendMax)
        sendMaxIssue = sendMax->issue();

    // convert the paths to a collection of strands. Each strand is the
    // collection of account->account steps and book steps that may be used in
    // this payment.
    auto [toStrandsTer, strands] = toStrands(
        sb,
        src,
        dst,
        dstIssue,
        limitQuality,
        sendMaxIssue,
        paths,
        defaultPaths,
        ownerPaysTransferFee,
        offerCrossing,
        j);

    if (toStrandsTer != tesSUCCESS)
    {
        path::RippleCalc::Output result;
        result.setResult(toStrandsTer);
        return result;
    }

    if (j.trace())
    {
        j.trace() << "\nsrc: " << src << "\ndst: " << dst
                  << "\nsrcIssue: " << srcIssue << "\ndstIssue: " << dstIssue;
        j.trace() << "\nNumStrands: " << strands.size();
        for (auto const& curStrand : strands)
        {
            j.trace() << "NumSteps: " << curStrand.size();
            for (auto const& step : curStrand)
            {
                j.trace() << '\n' << *step << '\n';
            }
        }
    }

    const bool srcIsXRP = isXRP(srcIssue.currency);
    const bool dstIsXRP = isXRP(dstIssue.currency);

    auto const asDeliver = toAmountSpec(deliver);

    // The src account may send either xrp or iou. The dst account may receive
    // either xrp or iou. Since XRP and IOU amounts are represented by different
    // types, use templates to tell `flow` about the amount types.
    if (srcIsXRP && dstIsXRP)
    {
        return finishFlow(
            sb,
            srcIssue,
            dstIssue,
            flow<XRPAmount, XRPAmount>(
                sb,
                strands,
                asDeliver.xrp,
                partialPayment,
                offerCrossing,
                limitQuality,
                sendMax,
                j,
                flowDebugInfo));
    }

    if (srcIsXRP && !dstIsXRP)
    {
        return finishFlow(
            sb,
            srcIssue,
            dstIssue,
            flow<XRPAmount, IOUAmount>(
                sb,
                strands,
                asDeliver.iou,
                partialPayment,
                offerCrossing,
                limitQuality,
                sendMax,
                j,
                flowDebugInfo));
    }

    if (!srcIsXRP && dstIsXRP)
    {
        return finishFlow(
            sb,
            srcIssue,
            dstIssue,
            flow<IOUAmount, XRPAmount>(
                sb,
                strands,
                asDeliver.xrp,
                partialPayment,
                offerCrossing,
                limitQuality,
                sendMax,
                j,
                flowDebugInfo));
    }

    assert(!srcIsXRP && !dstIsXRP);
    return finishFlow(
        sb,
        srcIssue,
        dstIssue,
        flow<IOUAmount, IOUAmount>(
            sb,
            strands,
            asDeliver.iou,
            partialPayment,
            offerCrossing,
            limitQuality,
            sendMax,
            j,
            flowDebugInfo));
}

}  // namespace ripple
