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
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/Quality.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

class DirectStepI : public StepImp<IOUAmount, IOUAmount, DirectStepI>
{
  private:
    AccountID src_;
    AccountID dst_;
    Currency currency_;
    bool isLast_ = false;

    // Charge transfer fees when the prev step redeems
    Step const* const prevStep_ = nullptr;

    beast::Journal j_;

    struct Cache
    {
        IOUAmount in;
        IOUAmount srcToDst;
        IOUAmount out;
        bool srcRedeems;

        Cache (
            IOUAmount const& in_,
            IOUAmount const& srcToDst_,
            IOUAmount const& out_,
            bool srcRedeems_)
            : in(in_)
            , srcToDst(srcToDst_)
            , out(out_)
            , srcRedeems(srcRedeems_)
        {}
    };

    boost::optional<Cache> cache_;

    // Returns srcQOut, dstQIn
    std::pair <std::uint32_t, std::uint32_t>
    qualities (
        PaymentSandbox& sb,
        bool srcRedeems,
        bool fwd) const;

  public:
    DirectStepI (
        AccountID const& src,
        AccountID const& dst,
        Currency const& c,
        Step const* prevStep,
        bool isLast,
        beast::Journal j)
            :src_(src)
            , dst_(dst)
            , currency_ (c)
            , isLast_ (isLast)
            , prevStep_ (prevStep)
            , j_ (j)
    {}

    AccountID const& src () const
    {
        return src_;
    }
    AccountID const& dst () const
    {
        return dst_;
    }
    Currency const& currency () const
    {
        return currency_;
    }

    boost::optional<EitherAmount> cachedIn () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (cache_->in);
    }

    boost::optional<EitherAmount>
    cachedOut () const override
    {
        if (!cache_)
            return boost::none;
        return EitherAmount (cache_->out);
    }

    boost::optional<AccountID>
    directStepSrcAcct () const override
    {
        return src_;
    }

    boost::optional<std::pair<AccountID,AccountID>>
    directStepAccts () const override
    {
        return std::make_pair(src_, dst_);
    }

    bool
    redeems (ReadView const& sb, bool fwd) const override;

    std::uint32_t
    lineQualityIn (ReadView const& v) const override;

    std::pair<IOUAmount, IOUAmount>
    revImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        IOUAmount const& out);

    std::pair<IOUAmount, IOUAmount>
    fwdImp (
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        IOUAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd (
        PaymentSandbox& sb,
        ApplyView& afView,
        EitherAmount const& in) override;

    // Check for error, existing liquidity, and violations of auth/frozen
    // constraints.
    TER check (StrandContext const& ctx) const;

    void setCacheLimiting (
        IOUAmount const& fwdIn,
        IOUAmount const& fwdSrcToDst,
        IOUAmount const& fwdOut,
        bool srcRedeems);

    friend bool operator==(DirectStepI const& lhs, DirectStepI const& rhs)
    {
        return lhs.src_ == rhs.src_ &&
            lhs.dst_ == rhs.dst_ &&
            lhs.currency_ == rhs.currency_;
    }

    friend bool operator!=(DirectStepI const& lhs, DirectStepI const& rhs)
    {
        return ! (lhs == rhs);
    }

  private:
    bool equal (Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<DirectStepI const*> (&rhs))
        {
            return *this == *ds;
        }
        return false;
    }

    std::string logString () const override
    {
        std::ostringstream ostr;
        ostr <<
            "DirectStepI: " <<
            "\nSrc: " << src_ <<
            "\nDst: " << dst_;
        return ostr.str ();
    }
};

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
    auto const srcOwed = toAmount<IOUAmount> (
        accountHolds (sb, src, cur, dst, fhIGNORE_FREEZE, beast::Journal{}));

    if (srcOwed.signum () > 0)
        return {srcOwed, true};

    // srcOwed is negative or zero
    return {creditLimit2 (sb, dst, src, cur) + srcOwed, false};
}

bool
DirectStepI::redeems (ReadView const& sb, bool fwd) const
{
    if (!fwd)
    {
        auto const srcOwed = accountHolds (
            sb, src_, currency_, dst_, fhIGNORE_FREEZE, beast::Journal{});
        return srcOwed.signum () > 0;
    }
    else
    {
        if (!cache_)
        {
            assert (0);
            return false;
        }
        return cache_->srcRedeems;
    }
}

std::pair<IOUAmount, IOUAmount>
DirectStepI::revImp (
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    IOUAmount const& out)
{
    cache_.reset ();

    bool srcRedeems;
    IOUAmount maxSrcToDst;
    std::tie (maxSrcToDst, srcRedeems) =
        maxFlow (sb, src_, dst_, currency_);

    std::uint32_t srcQOut, dstQIn;
    std::tie (srcQOut, dstQIn) = qualities (sb, srcRedeems, false);

    Issue const srcToDstIss (currency_, srcRedeems ? dst_ : src_);

    JLOG (j_.trace()) <<
        "DirectStepI::rev" <<
        " srcRedeems: " << srcRedeems <<
        " outReq: " << to_string (out) <<
        " maxSrcToDst: " << to_string (maxSrcToDst) <<
        " srcQOut: " << srcQOut <<
        " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum () <= 0)
    {
        JLOG (j_.trace()) << "DirectStepI::rev: dry";
        cache_.emplace (
            IOUAmount (beast::zero),
            IOUAmount (beast::zero),
            IOUAmount (beast::zero),
            srcRedeems);
        return {beast::zero, beast::zero};
    }

    IOUAmount const srcToDst = mulRatio (
        out, QUALITY_ONE, dstQIn, /*roundUp*/ true);

    if (srcToDst <= maxSrcToDst)
    {

        IOUAmount const in = mulRatio (
            srcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        cache_.emplace (in, srcToDst, out, srcRedeems);
        rippleCredit (sb,
                      src_, dst_, toSTAmount (srcToDst, srcToDstIss),
                      /*checkIssuer*/ true, j_);
        JLOG (j_.trace()) <<
            "DirectStepI::rev: Non-limiting" <<
            " srcRedeems: " << srcRedeems <<
            " in: " << to_string (in) <<
            " srcToDst: " << to_string (srcToDst) <<
            " out: " << to_string (out);
        return {in, out};
    }

    // limiting node
    IOUAmount const in = mulRatio (
        maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
    IOUAmount const actualOut = mulRatio (
        maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
    cache_.emplace (in, maxSrcToDst, actualOut, srcRedeems);
    rippleCredit (sb,
                  src_, dst_, toSTAmount (maxSrcToDst, srcToDstIss),
                  /*checkIssuer*/ true, j_);
    JLOG (j_.trace()) <<
        "DirectStepI::rev: Limiting" <<
        " srcRedeems: " << srcRedeems <<
        " in: " << to_string (in) <<
        " srcToDst: " << to_string (maxSrcToDst) <<
        " out: " << to_string (out);
    return {in, actualOut};
}

// The forward pass should never have more liquidity than the reverse
// pass. But sometimes rounding differences cause the forward pass to
// deliver more liquidity. Use the cached values from the reverse pass
// to prevent this.
void
DirectStepI::setCacheLimiting (
    IOUAmount const& fwdIn,
    IOUAmount const& fwdSrcToDst,
    IOUAmount const& fwdOut,
    bool srcRedeems)
{
    if (cache_->in < fwdIn)
    {
        IOUAmount const smallDiff(1, -9);
        auto const diff = fwdIn - cache_->in;
        if (diff > smallDiff)
        {
            if (fwdIn.exponent () != cache_->in.exponent () ||
                !cache_->in.mantissa () ||
                (double(fwdIn.mantissa ()) /
                    double(cache_->in.mantissa ())) > 1.01)
            {
                // Detect large diffs on forward pass so they may be investigated
                JLOG (j_.warn())
                    << "DirectStepI::fwd: setCacheLimiting"
                    << " fwdIn: " << to_string (fwdIn)
                    << " cacheIn: " << to_string (cache_->in)
                    << " fwdSrcToDst: " << to_string (fwdSrcToDst)
                    << " cacheSrcToDst: " << to_string (cache_->srcToDst)
                    << " fwdOut: " << to_string (fwdOut)
                    << " cacheOut: " << to_string (cache_->out);
                cache_.emplace (fwdIn, fwdSrcToDst, fwdOut, srcRedeems);
                return;
            }
        }
    }
    cache_->in = fwdIn;
    if (fwdSrcToDst < cache_->srcToDst)
        cache_->srcToDst = fwdSrcToDst;
    if (fwdOut < cache_->out)
        cache_->out = fwdOut;
    cache_->srcRedeems = srcRedeems;
};

std::pair<IOUAmount, IOUAmount>
DirectStepI::fwdImp (
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    IOUAmount const& in)
{
    assert (cache_);

    bool srcRedeems;
    IOUAmount maxSrcToDst;
    std::tie (maxSrcToDst, srcRedeems) =
        maxFlow (sb, src_, dst_, currency_);

    std::uint32_t srcQOut, dstQIn;
    std::tie (srcQOut, dstQIn) = qualities (sb, srcRedeems, true);

    Issue const srcToDstIss (currency_, srcRedeems ? dst_ : src_);

    JLOG (j_.trace()) <<
            "DirectStepI::fwd" <<
            " srcRedeems: " << srcRedeems <<
            " inReq: " << to_string (in) <<
            " maxSrcToDst: " << to_string (maxSrcToDst) <<
            " srcQOut: " << srcQOut <<
            " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum () <= 0)
    {
        JLOG (j_.trace()) << "DirectStepI::fwd: dry";
        cache_.emplace (
            IOUAmount (beast::zero),
            IOUAmount (beast::zero),
            IOUAmount (beast::zero),
            srcRedeems);
        return {beast::zero, beast::zero};
    }

    IOUAmount const srcToDst = mulRatio (
        in, QUALITY_ONE, srcQOut, /*roundUp*/ false);

    if (srcToDst <= maxSrcToDst)
    {
        IOUAmount const out = mulRatio (
            srcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting (in, srcToDst, out, srcRedeems);
        rippleCredit (sb,
            src_, dst_, toSTAmount (cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ true, j_);
        JLOG (j_.trace()) <<
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
        setCacheLimiting (actualIn, maxSrcToDst, out, srcRedeems);
        rippleCredit (sb,
            src_, dst_, toSTAmount (cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ true, j_);
        JLOG (j_.trace()) <<
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
        JLOG (j_.trace()) << "Expected valid cache in validFwd";
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
        boost::container::flat_set<uint256> dummy;
        fwdImp (sb, afView, dummy, in.iou);  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount (IOUAmount (beast::zero))};
    }

    if (maxSrcToDst < cache_->srcToDst)
    {
        JLOG (j_.error()) <<
            "DirectStepI: Strand re-execute check failed." <<
            " Exceeded max src->dst limit" <<
            " max src->dst: " << to_string (maxSrcToDst) <<
            " actual src->dst: " << to_string (cache_->srcToDst);
        return {false, EitherAmount(cache_->out)};
    }

    if (!(checkNear (savCache.in, cache_->in) &&
          checkNear (savCache.out, cache_->out)))
    {
        JLOG (j_.error()) <<
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
    ReadView const& sb,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency,
    // set true for dst quality in, false for src quality out
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
        if (qin)
        {
            // compute dst quality in
            if (dst < src)
                return sfLowQualityIn;
            else
                return sfHighQualityIn;
        }
        else
        {
            // compute src quality out
            if (src < dst)
                return sfLowQualityOut;
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
    bool srcRedeems,
    bool fwd) const
{
    if (srcRedeems)
    {
        if (!prevStep_)
            return {QUALITY_ONE, QUALITY_ONE};

        auto const prevStepQIn = prevStep_->lineQualityIn (sb);
        auto srcQOut = quality (sb, src_, dst_, currency_, false);
        if (prevStepQIn > srcQOut)
            srcQOut = prevStepQIn;
        return {srcQOut, QUALITY_ONE};
    }
    else
    {
        // Charge a transfer rate when issuing and previous step redeems
        auto const prevStepRedeems = prevStep_ && prevStep_->redeems (sb, fwd);
        std::uint32_t const srcQOut =
            prevStepRedeems ? transferRate (sb, src_).value : QUALITY_ONE;
        auto dstQIn = quality (sb, src_, dst_, currency_, true);
        if (isLast_ && dstQIn > QUALITY_ONE)
            dstQIn = QUALITY_ONE;
        return {srcQOut, dstQIn};
    }
}

std::uint32_t
DirectStepI::lineQualityIn (ReadView const& v) const
{
    // dst quality in
    return quality (v, src_, dst_, currency_, true);
}

TER DirectStepI::check (StrandContext const& ctx) const
{
    if (!src_ || !dst_)
    {
        JLOG (j_.debug()) << "DirectStepI: specified bad account.";
        return temBAD_PATH;
    }

    if (src_ == dst_)
    {
        JLOG (j_.debug()) << "DirectStepI: same src and dst.";
        return temBAD_PATH;
    }

    auto sleLine = ctx.view.read (keylet::line (src_, dst_, currency_));
    {
        auto sleSrc = ctx.view.read (keylet::account (src_));
        if (!sleSrc)
        {
            JLOG (j_.warn())
                    << "DirectStepI: can't receive IOUs from non-existent issuer: "
                    << src_;
            return terNO_ACCOUNT;
        }

        if (!sleLine)
        {
            JLOG (j_.trace()) << "DirectStepI: No credit line. " << *this;
            return terNO_LINE;
        }

        auto const authField = (src_ > dst_) ? lsfHighAuth : lsfLowAuth;

        if (((*sleSrc)[sfFlags] & lsfRequireAuth) &&
            !((*sleLine)[sfFlags] & authField) &&
            (*sleLine)[sfBalance] == zero)
        {
            JLOG (j_.warn())
                << "DirectStepI: can't receive IOUs from issuer without auth."
                << " src: " << src_;
            return terNO_AUTH;
        }
    }

    // pure issue/redeem can't be frozen
    if (! (ctx.isLast && ctx.isFirst))
    {
        auto const ter = checkFreeze (ctx.view, src_, dst_, currency_);
        if (ter != tesSUCCESS)
            return ter;
    }

    if (ctx.prevStep)
    {
        if (auto prevSrc = ctx.prevStep->directStepSrcAcct ())
        {
            auto const ter =
                checkNoRipple (ctx.view, *prevSrc, src_, dst_, currency_, j_);
            if (ter != tesSUCCESS)
                return ter;
        }

        if (fix1449(ctx.view.info().parentCloseTime))
        {
            if (ctx.prevStep->bookStepBook())
            {
                auto const noRippleSrcToDst =
                    ((*sleLine)[sfFlags] &
                     ((src_ > dst_) ? lsfHighNoRipple : lsfLowNoRipple));
                if (noRippleSrcToDst)
                    return terNO_RIPPLE;
            }
        }
    }

    {
        Issue const srcIssue{currency_, src_};
        Issue const dstIssue{currency_, dst_};

        if (ctx.seenBookOuts.count (srcIssue))
        {
            if (!ctx.prevStep)
            {
                assert(0); // prev seen book without a prev step!?!
                return temBAD_PATH_LOOP;
            }

            // This is OK if the previous step is a book step that outputs this issue
            if (auto book = ctx.prevStep->bookStepBook())
            {
                if (book->out != srcIssue)
                    return temBAD_PATH_LOOP;
            }
        }

        if (!ctx.seenDirectIssues[0].insert (srcIssue).second ||
            !ctx.seenDirectIssues[1].insert (dstIssue).second)
        {
            JLOG (j_.debug ())
                << "DirectStepI: loop detected: Index: " << ctx.strandSize
                << ' ' << *this;
            return temBAD_PATH_LOOP;
        }
    }

    {
        auto const owed = creditBalance (ctx.view, dst_, src_, currency_);
        if (owed <= zero)
        {
            auto const limit = creditLimit (ctx.view, dst_, src_, currency_);
            if (-owed >= limit)
            {
                JLOG (j_.debug())
                        << "DirectStepI: dry: owed: " << owed << " limit: " << limit;
                return tecPATH_DRY;
            }
        }
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

namespace test
{
// Needed for testing
bool directStepEqual (Step const& step,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    if (auto ds = dynamic_cast<DirectStepI const*> (&step))
    {
        return ds->src () == src && ds->dst () == dst &&
            ds->currency () == currency;
    }
    return false;
}
}  // test

//------------------------------------------------------------------------------

std::pair<TER, std::unique_ptr<Step>>
make_DirectStepI (
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    Currency const& c)
{
    // Only charge a transfer fee if the previous step redeems
    auto r = std::make_unique<DirectStepI> (
        src, dst, c, ctx.prevStep, ctx.isLast, ctx.j);
    auto ter = r->check (ctx);
    if (ter != tesSUCCESS)
        return {ter, nullptr};
    return {tesSUCCESS, std::move (r)};
}

} // ripple
