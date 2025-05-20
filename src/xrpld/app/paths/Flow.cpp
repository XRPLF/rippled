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

#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/Credit.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/paths/detail/AmountSpec.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/app/paths/detail/StrandFlow.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

template <class FlowResult>
static auto
finishFlow(
    PaymentSandbox& sb,
    Asset const& srcAsset,
    Asset const& dstAsset,
    FlowResult&& f)
{
    path::RippleCalc::Output result;
    if (f.ter == tesSUCCESS)
        f.sandbox->apply(sb);
    else
        result.removableOffers = std::move(f.removableOffers);

    result.setResult(f.ter);
    result.actualAmountIn = toSTAmount(f.in, srcAsset);
    result.actualAmountOut = toSTAmount(f.out, dstAsset);

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
    OfferCrossing offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMax,
    beast::Journal j,
    path::detail::FlowDebugInfo* flowDebugInfo)
{
    Asset const srcAsset = [&]() -> Asset {
        if (sendMax)
            return sendMax->asset();
        if (isXRP(deliver))
            return xrpIssue();
        if (deliver.holds<Issue>())
            return Issue(deliver.get<Issue>().currency, src);
        return deliver.asset();
    }();

    Asset const dstAsset = deliver.asset();

    std::optional<Asset> sendMaxAsset;
    if (sendMax)
        sendMaxAsset = sendMax->asset();

    AMMContext ammContext(src, false);

    // convert the paths to a collection of strands. Each strand is the
    // collection of account->account steps and book steps that may be used in
    // this payment.
    auto [toStrandsTer, strands] = toStrands(
        sb,
        src,
        dst,
        dstAsset,
        limitQuality,
        sendMaxAsset,
        paths,
        defaultPaths,
        ownerPaysTransferFee,
        offerCrossing,
        ammContext,
        j);

    if (toStrandsTer != tesSUCCESS)
    {
        path::RippleCalc::Output result;
        result.setResult(toStrandsTer);
        return result;
    }

    ammContext.setMultiPath(strands.size() > 1);

    if (j.trace())
    {
        j.trace() << "\nsrc: " << src << "\ndst: " << dst
                  << "\nsrcAsset: " << srcAsset << "\ndstAsset: " << dstAsset;
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

    // The src account may send either xrp,iou,or mpt. The dst account may
    // receive either xrp,iou, or mpt. Since XRP, IOU, and MPT amounts are
    // represented by different types, use templates to tell `flow` about the
    // amount types.
    return std::visit(
        [&, &strands_ = strands]<typename TIn, typename TOut>(
            TIn const&, TOut const&) {
            using TIn_ = typename TIn::amount_type;
            using TOut_ = typename TOut::amount_type;
            return finishFlow(
                sb,
                srcAsset,
                dstAsset,
                flow<TIn_, TOut_>(
                    sb,
                    strands_,
                    get<TOut_>(deliver),
                    partialPayment,
                    offerCrossing,
                    limitQuality,
                    sendMax,
                    j,
                    ammContext,
                    flowDebugInfo));
        },
        srcAsset.getAmountType(),
        dstAsset.getAmountType());
}

}  // namespace ripple
