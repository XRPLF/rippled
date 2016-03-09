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
#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/app/paths/impl/StrandFlow.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/XRPAmount.h>

#include <numeric>
#include <sstream>

namespace ripple {

path::RippleCalc::Output
flow (
    PaymentSandbox& sb,
    STAmount const& deliver,
    AccountID const& src,
    AccountID const& dst,
    STPathSet const& paths,
    bool defaultPaths,
    bool partialPayment,
    boost::optional<Quality> const& limitQuality,
    boost::optional<STAmount> const& sendMax,
    beast::Journal j)
{
    path::RippleCalc::Output result;

    Issue const srcIssue =
        sendMax ? sendMax->issue () : Issue (deliver.issue ().currency, src);

    Issue const dstIssue = deliver.issue ();

    boost::optional<Issue> sendMaxIssue;
    if (sendMax)
        sendMaxIssue = sendMax->issue ();

    // convert the paths to a collection of strands. Each strand is the collection
    // of account->account steps and book steps that may be used in this payment.
    auto sr = toStrands (sb, src, dst, dstIssue, sendMaxIssue, paths,
        defaultPaths, j);

    if (sr.first != tesSUCCESS)
    {
        result.setResult (sr.first);
        return result;
    }

    auto& strands = sr.second;

    if (j.trace())
    {
        j.trace() << "\nsrc: " << src << "\ndst: " << dst
            << "\nsrcIssue: " << srcIssue << "\ndstIssue: " << dstIssue;
        j.trace() << "\nNumStrands: " << strands.size ();
        for (auto const& curStrand : strands)
        {
            j.trace() << "NumSteps: " << curStrand.size ();
            for (auto const& step : curStrand)
            {
                j.trace() << '\n' << *step << '\n';
            }
        }
    }

    const bool srcIsXRP = isXRP (srcIssue.currency);
    const bool dstIsXRP = isXRP (dstIssue.currency);

    auto const asDeliver = toAmountSpec (deliver);
    boost::optional<PaymentSandbox> strandSB;

    // The src account may send either xrp or iou. The dst account may receive
    // either xrp or iou. Since XRP and IOU amounts are represented by different
    // types, use templates to tell `flow` about the amount types.
    if (srcIsXRP && dstIsXRP)
    {
        auto f = flow<XRPAmount, XRPAmount> (
            sb, strands, asDeliver.xrp, defaultPaths, partialPayment, limitQuality, sendMax, j);

        if (f.ter == tesSUCCESS)
            strandSB.emplace (std::move (*f.sandbox));

        result.setResult (f.ter);
        result.actualAmountIn = toSTAmount (f.in);
        result.actualAmountOut = toSTAmount (f.out);
    }
    else if (srcIsXRP && !dstIsXRP)
    {
        auto f = flow<XRPAmount, IOUAmount> (
            sb, strands, asDeliver.iou, defaultPaths, partialPayment,
            limitQuality, sendMax, j);

        if (f.ter == tesSUCCESS)
            strandSB.emplace (std::move (*f.sandbox));

        result.setResult (f.ter);
        result.actualAmountIn = toSTAmount (f.in);
        result.actualAmountOut = toSTAmount (f.out, dstIssue);
    }
    else if (!srcIsXRP && dstIsXRP)
    {
        auto f = flow<IOUAmount, XRPAmount> (
            sb, strands, asDeliver.xrp, defaultPaths, partialPayment,
            limitQuality, sendMax, j);

        if (f.ter == tesSUCCESS)
            strandSB.emplace (std::move (*f.sandbox));

        result.setResult (f.ter);
        result.actualAmountIn = toSTAmount (f.in, srcIssue);
        result.actualAmountOut = toSTAmount (f.out);
    }
    else if (!srcIsXRP && !dstIsXRP)
    {
        auto f = flow<IOUAmount, IOUAmount> (
            sb, strands, asDeliver.iou, defaultPaths, partialPayment,
            limitQuality, sendMax, j);

        if (f.ter == tesSUCCESS)
            strandSB.emplace (std::move (*f.sandbox));

        result.setResult (f.ter);
        result.actualAmountIn = toSTAmount (f.in, srcIssue);
        result.actualAmountOut = toSTAmount (f.out, dstIssue);
    }

    // strandSB is only valid when flow was successful
    if (strandSB)
        strandSB->apply (sb);
    return result;
}

} // ripple
