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
#include <ripple/app/paths/impl/StepChecks.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/app/paths/impl/DirectStep.h>
#include <ripple/protocol/Quality.h>

#include <numeric>
#include <sstream>

namespace ripple {

// Compute the maximum value that can flow from src->dst at
// the best available quality.
// return: first element is max amount that can flow,
//         second is if src redeems to dst
static
std::pair<IOUAmount, bool>
maxFlow (
    PaymentSandbox const& sb,
    AccountID const& src,
    AccountID const& dst,
    Currency const& cur)
{
    auto const srcOwed = creditBalance2 (sb, dst, src, cur);

    if (srcOwed.signum () > 0)
        return {srcOwed, true};

    // srcOwed is negative or zero
    return {creditLimit2 (sb, dst, src, cur) + srcOwed, false};
}

std::pair<IOUAmount, IOUAmount>
DirectStepI::revImp (
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    std::vector<uint256>& /*ofrsToRm*/,
    IOUAmount const& out)
{
    cache_.reset ();

    bool srcRedeems;
    IOUAmount maxSrcToDst;
    std::tie (maxSrcToDst, srcRedeems) =
        maxFlow (sb, src_, dst_, currency_);

    std::uint32_t srcQOut, dstQIn;
    std::tie (srcQOut, dstQIn) = qualities (sb, srcRedeems);

    Issue const srcToDstIss (currency_, srcRedeems ? dst_ : src_);

    JLOG (j_.trace) <<
        "DirectStepI::rev" <<
        " srcRedeems: " << srcRedeems <<
        " outReq: " << to_string (out) <<
        " maxSrcToDst: " << to_string (maxSrcToDst) <<
        " srcQOut: " << srcQOut <<
        " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum () <= 0)
    {
        JLOG (j_.trace) << "DirectStepI::rev: dry";
        cache_.emplace (
            IOUAmount (beast::zero),
            IOUAmount (beast::zero),
            IOUAmount (beast::zero));
        return {beast::zero, beast::zero};
    }

    IOUAmount const srcToDst = mulRatio (
        out, QUALITY_ONE, dstQIn, /*roundUp*/ true);

    if (srcToDst <= maxSrcToDst)
    {

        IOUAmount const in = mulRatio (
            srcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        cache_.emplace (in, srcToDst, out);
        rippleCredit (sb,
                      src_, dst_, toSTAmount (srcToDst, srcToDstIss),
                      /*checkIssuer*/ true, l_.journal ("View"));
        JLOG (j_.trace) <<
            "DirectStepI::rev: Non-limiting" <<
            " srcRedeems: " << srcRedeems <<
            " in: " << to_string (in) <<
            " srcToDst: " << to_string (srcToDst) <<
            " out: " << to_string (out);
        return {in, out};
    }
    else
    {
        // limiting node
        IOUAmount const in = mulRatio (
            maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        IOUAmount const actualOut = mulRatio (
            maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        cache_.emplace (in, maxSrcToDst, actualOut);
        rippleCredit (sb,
                      src_, dst_, toSTAmount (maxSrcToDst, srcToDstIss),
                      /*checkIssuer*/ true, l_.journal ("View"));
        JLOG (j_.trace) <<
                "DirectStepI::rev: Limiting" <<
                " srcRedeems: " << srcRedeems <<
                " in: " << to_string (in) <<
                " srcToDst: " << to_string (maxSrcToDst) <<
                " out: " << to_string (out);
        return {in, actualOut};
    }
}

std::pair<IOUAmount, IOUAmount>
DirectStepI::fwdImp (
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    std::vector<uint256>& /*ofrsToRm*/,
    IOUAmount const& in)
{
    assert (cache_);

    bool srcRedeems;
    IOUAmount maxSrcToDst;
    std::tie (maxSrcToDst, srcRedeems) =
        maxFlow (sb, src_, dst_, currency_);

    std::uint32_t srcQOut, dstQIn;
    std::tie (srcQOut, dstQIn) = qualities (sb, srcRedeems);

    Issue const srcToDstIss (currency_, srcRedeems ? dst_ : src_);

    JLOG (j_.trace) <<
            "DirectStepI::fwd" <<
            " srcRedeems: " << srcRedeems <<
            " inReq: " << to_string (in) <<
            " maxSrcToDst: " << to_string (maxSrcToDst) <<
            " srcQOut: " << srcQOut <<
            " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum () <= 0)
    {
        JLOG (j_.trace) << "DirectStepI::fwd: dry";
        cache_.emplace (
            IOUAmount (beast::zero),
            IOUAmount (beast::zero),
            IOUAmount (beast::zero));
        return {beast::zero, beast::zero};
    }

    IOUAmount const srcToDst = mulRatio (
        in, QUALITY_ONE, srcQOut, /*roundUp*/ false);

    // The forward pass should never have more liquidity than the reverse
    // pass. But sometime rounding differences cause the forward pass to
    // deliver more liquidity. Use the cached values from the reverse pass
    // to prevent this.
    auto setCacheLimiting = [&cache = cache_, &j = j_](IOUAmount const& fwdIn,
        IOUAmount const& fwdSrcToDst, IOUAmount const& fwdOut)
    {
        if (cache->in < fwdIn)
        {
            IOUAmount const smallDiff(1, -9);
            auto const diff = fwdIn - cache->in;
            if (diff > smallDiff)
            {
                if (fwdIn.exponent () != cache->in.exponent () ||
                    !cache->in.mantissa () ||
                    (double(fwdIn.mantissa ()) /
                        double(cache->in.mantissa ())) > 1.01)
                {
                    // Detect large diffs on forward pass so they may be investigated
                    JLOG (j.warning)
                        << "DirectStepI::fwd: setCacheLimiting"
                        << " fwdIn: " << to_string (fwdIn)
                        << " cacheIn: " << to_string (cache->in)
                        << " fwdSrcToDst: " << to_string (fwdSrcToDst)
                        << " cacheSrcToDst: " << to_string (cache->srcToDst)
                        << " fwdOut: " << to_string (fwdOut)
                        << " cacheOut: " << to_string (cache->out);
                    cache.emplace (fwdIn, fwdSrcToDst, fwdOut);
                    return;
                }
            }
        }
        cache->in = fwdIn;
        if (fwdSrcToDst < cache->srcToDst)
            cache->srcToDst = fwdSrcToDst;
        if (fwdOut < cache->out)
            cache->out = fwdOut;
    };

    if (srcToDst <= maxSrcToDst)
    {
        IOUAmount const out = mulRatio (
            srcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting (in, srcToDst, out);
        rippleCredit (sb,
            src_, dst_, toSTAmount (cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ true, l_.journal ("View"));
        JLOG (j_.trace) <<
                "DirectStepI::fwd: Non-limiting" <<
                " srcRedeems: " << srcRedeems <<
                " in: " << to_string (in) <<
                " srcToDst: " << to_string (srcToDst) <<
                " out: " << to_string (out);
    }
    else
    {
        // limiting node
        IOUAmount const actualIn = mulRatio (
            maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        IOUAmount const out = mulRatio (
            maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting (actualIn, maxSrcToDst, out);
        rippleCredit (sb,
            src_, dst_, toSTAmount (cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ true, l_.journal ("View"));
        JLOG (j_.trace) <<
                "DirectStepI::rev: Limiting" <<
                " srcRedeems: " << srcRedeems <<
                " in: " << to_string (actualIn) <<
                " srcToDst: " << to_string (srcToDst) <<
                " out: " << to_string (out);
    }
    return {cache_->in, cache_->out};
}

std::pair<bool, EitherAmount>
DirectStepI::validFwd (
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG (j_.trace) << "Expected valid cache in validFwd";
        return {false, EitherAmount (IOUAmount (beast::zero))};
    }


    auto const savCache = *cache_;

    assert (!in.native);

    bool srcRedeems;
    IOUAmount maxSrcToDst;
    std::tie (maxSrcToDst, srcRedeems) =
            maxFlow (sb, src_, dst_, currency_);

    try
    {
        std::vector<uint256> dummy;
        fwdImp (sb, afView, dummy, in.iou);  // changes cache
    }
    catch (StepError const&)
    {
        return {false, EitherAmount (IOUAmount (beast::zero))};
    }

    if (maxSrcToDst < cache_->srcToDst)
    {
        JLOG (j_.trace) <<
            "DirectStepI: Strand re-execute check failed." <<
            " Exceeded max src->dst limit" <<
            " max src->dst: " << to_string (maxSrcToDst) <<
            " actual src->dst: " << to_string (cache_->srcToDst);
        return {false, EitherAmount(cache_->out)};
    }

    if (!(checkEqual (savCache.in, cache_->in) &&
          checkEqual (savCache.out, cache_->out)))
    {
        JLOG (j_.trace) <<
            "DirectStepI: Strand re-execute check failed." <<
            " ExpectedIn: " << to_string (savCache.in) <<
            " CachedIn: " << to_string (cache_->in) <<
            " ExpectedOut: " << to_string (savCache.out) <<
            " CachedOut: " << to_string (cache_->out);
        return {false, EitherAmount (cache_->out)};
    }
    return {true, EitherAmount (cache_->out)};
}

static
std::uint32_t
quality (
    PaymentSandbox const& sb,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency,
    // set true for quality in, false for quality out
    bool qin)
{
    if (src == dst)
        return QUALITY_ONE;

    auto const sle = sb.read (
        keylet::line (dst, src, currency));

    if (!sle)
        return QUALITY_ONE;

    auto const& field = [&]() -> SF_U32 const&
    {
        if (dst < src)
        {
            if (qin)
                return sfLowQualityIn;
            else
                return sfLowQualityOut;
        }
        else
        {
            if (qin)
                return sfHighQualityIn;
            else
                return sfHighQualityOut;
        }
    }();

    if (! sle->isFieldPresent (field))
        return QUALITY_ONE;

    auto const q = (*sle)[field];
    if (!q)
        return QUALITY_ONE;
    return q;
}

// Returns srcQOut, dstQIn
std::pair<std::uint32_t, std::uint32_t>
DirectStepI::qualities (
    PaymentSandbox& sb,
    bool srcRedeems) const
{
    if (srcRedeems)
    {
        return std::make_pair(
            quality (
                sb, src_, dst_, currency_,
                /*qin*/false),
            QUALITY_ONE);
    }
    else
    {
        // Charge a transfer rate when issuing, unless this is the first step.
        std::uint32_t const srcQOut =
            noTransferFee_ ? QUALITY_ONE : rippleTransferRate (sb, src_);
        return std::make_pair(
            srcQOut,
            quality ( // dst quality in
                sb, src_, dst_, currency_,
                /*qin*/true));
    }
}

TER DirectStepI::check (StrandContext const& ctx) const
{
    if (!src_ || !dst_)
    {
        JLOG (j_.debug) << "DirectStepI: specified bad account.";
        return temBAD_PATH;
    }

    {
        auto sleSrc = ctx.view.read (keylet::account (src_));
        if (!sleSrc)
        {
            JLOG (j_.warning)
                    << "DirectStepI: can't receive IOUs from non-existent issuer: "
                    << src_;
            return terNO_ACCOUNT;
        }

        auto sleLine = ctx.view.read (keylet::line (src_, dst_, currency_));

        if (!sleLine)
        {
            JLOG (j_.trace) << "DirectStepI: No credit line. " << *this;
            return terNO_LINE;
        }

        auto const authField = (src_ > dst_) ? lsfHighAuth : lsfLowAuth;

        if (((*sleSrc)[sfFlags] & lsfRequireAuth) &&
            !((*sleLine)[sfFlags] & authField) &&
            (*sleLine)[sfBalance] == zero)
        {
            JLOG (j_.warning)
                << "DirectStepI: can't receive IOUs from issuer without auth."
                << " src: " << src_;
            return terNO_AUTH;
        }
    }

    {
        auto const owed = creditBalance (ctx.view, dst_, src_, currency_);
        if (owed <= zero)
        {
            auto const limit = creditLimit (ctx.view, dst_, src_, currency_);
            if (-owed >= limit)
            {
                JLOG (j_.debug)
                    << "DirectStepI: dry: owed: " << owed << " limit: " << limit;
                return tecPATH_DRY;
            }
        }
    }

    // pure issue/redeem can't be frozen
    if (! (ctx.isLast && ctx.isFirst))
    {
        auto const ter = checkFreeze (ctx.view, src_, dst_, currency_);
        if (ter != tesSUCCESS)
            return ter;
    }

    if (ctx.prevDSSrc)
    {
        auto const ter =
            checkNoRipple (ctx.view, *ctx.prevDSSrc, src_, dst_, currency_, j_);
        if (ter != tesSUCCESS)
            return ter;
    }

    if (!ctx.seenDirectIssues[0].insert (Issue{currency_, src_}).second ||
        !ctx.seenDirectIssues[1].insert (Issue{currency_, dst_}).second)
    {
        JLOG (j_.debug) << "DirectStepI: loop detected: Index: "
                        << ctx.strandSize << ' ' << *this;
        return temBAD_PATH_LOOP;
    }

    return tesSUCCESS;
}

std::pair<TER, std::unique_ptr<Step>> make_DirectStepI (
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    Currency const& c)
{
    auto r = std::make_unique<DirectStepI> (
        src, dst, c, /* noTransferFee */ ctx.isFirst, ctx.logs);
    auto ter = r->check (ctx);
    if (ter != tesSUCCESS)
        return {ter, nullptr};
    return {tesSUCCESS, std::move (r)};
}

} // ripple
