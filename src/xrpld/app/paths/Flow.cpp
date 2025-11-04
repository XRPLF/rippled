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
    OfferCrossing offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMax,
    std::optional<uint256> const& domainID,
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

    AMMContext ammContext(src, false);

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
        ammContext,
        domainID,
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

    bool const srcIsXRP = isXRP(srcIssue.currency);
    bool const dstIsXRP = isXRP(dstIssue.currency);

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
                ammContext,
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
                ammContext,
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
                ammContext,
                flowDebugInfo));
    }

    XRPL_ASSERT(!srcIsXRP && !dstIsXRP, "ripple::flow : neither is XRP");
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
            ammContext,
            flowDebugInfo));
}

}  // namespace ripple
