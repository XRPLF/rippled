/** @file
 *  Public entry point for the XRP Ledger payment flow engine.
 *
 *  Bridges the high-level transaction processor (runtime `STAmount` and
 *  `Asset` values) into the fully type-parameterised inner loop in
 *  `StrandFlow.h`. Resolves source/destination asset types at runtime, builds
 *  execution strands via `toStrands()`, initialises the AMM context, then
 *  dispatches to the appropriately instantiated `flow<TIn, TOut>` template via
 *  `std::visit`. All iterative liquidity-seeking and sandbox management live
 *  in `StrandFlow.h`; this file is intentionally thin.
 */
#include <xrpl/tx/paths/Flow.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/tx/paths/RippleCalc.h>
#include <xrpl/tx/paths/detail/AmountSpec.h>
#include <xrpl/tx/paths/detail/Steps.h>
#include <xrpl/tx/paths/detail/StrandFlow.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <optional>
#include <variant>

namespace xrpl {

/** Translate a typed `FlowResult` into the public `RippleCalc::Output` type.
 *
 *  On success (`isTesSuccess(f.ter)`), the speculative `PaymentSandbox` inside
 *  the result is applied to `sb`, making all ledger mutations visible to the
 *  caller's transaction scope. On failure the sandbox is simply abandoned —
 *  those mutations evaporate — but `removableOffers` is still propagated so
 *  that expired or unfunded offers discovered during a failed payment attempt
 *  can still be cleaned from the ledger. The typed amounts `f.in` and `f.out`
 *  are materialised back to `STAmount` regardless of success or failure.
 *
 *  @tparam FlowResult A `FlowResult<TIn, TOut>` specialisation from
 *      `StrandFlow.h`.
 *  @param sb The caller's `PaymentSandbox`; mutated only on success.
 *  @param srcAsset Asset used to convert `f.in` back to `STAmount`.
 *  @param dstAsset Asset used to convert `f.out` back to `STAmount`.
 *  @param f The typed result from the inner `flow<TIn, TOut>` template.
 *  @return A `RippleCalc::Output` with amounts, TER, and removable offers.
 */
template <class FlowResult>
static auto
finishFlow(PaymentSandbox& sb, Asset const& srcAsset, Asset const& dstAsset, FlowResult&& f)
{
    path::RippleCalc::Output result;
    if (isTesSuccess(f.ter))
    {
        f.sandbox->apply(sb);  // NOLINT(bugprone-unchecked-optional-access) sandbox set on success
    }
    else
    {
        result.removableOffers = std::move(f.removableOffers);
    }

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
    std::optional<uint256> const& domainID,
    beast::Journal j,
    path::detail::FlowDebugInfo* flowDebugInfo)
{
    Asset const srcAsset = [&]() -> Asset {
        if (sendMax)
            return sendMax->asset();
        return deliver.asset().visit(
            [&](Issue const& issue) -> Asset {
                if (isXRP(issue))
                    return xrpIssue();
                return Issue(issue.currency, src);
            },
            [&](MPTIssue const&) { return deliver.asset(); });
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
        domainID,
        j);

    if (!isTesSuccess(toStrandsTer))
    {
        path::RippleCalc::Output result;
        result.setResult(toStrandsTer);
        return result;
    }

    ammContext.setMultiPath(strands.size() > 1);

    if (j.trace())
    {
        j.trace() << "\nsrc: " << src << "\ndst: " << dst << "\nsrcAsset: " << srcAsset
                  << "\ndstAsset: " << dstAsset;
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
        [&, &strands = strands]<typename TIn, typename TOut>(TIn const&, TOut const&) {
            using TIn_ = typename TIn::amount_type;
            using TOut_ = typename TOut::amount_type;
            return finishFlow(
                sb,
                srcAsset,
                dstAsset,
                flow<TIn_, TOut_>(
                    sb,
                    strands,
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

}  // namespace xrpl
