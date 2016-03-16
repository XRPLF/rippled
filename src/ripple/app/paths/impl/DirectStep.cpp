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

template <class TDerived>
class DirectStepI : public StepImp<IOUAmount, IOUAmount, DirectStepI<TDerived>>
{
protected:
    AccountID src_;
    AccountID dst_;
    Currency currency_;

    // Charge transfer fees when the prev step redeems
    Step const* const prevStep_ = nullptr;
    bool const isLast_;
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

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is true if dst holds an iou from src.
    std::pair<IOUAmount, bool>
    maxPaymentFlow (
        ReadView const& sb) const;

    // Returns srcQOut, dstQIn
    std::pair <std::uint32_t, std::uint32_t>
    qualities (
        ReadView const& sb,
        bool srcRedeems,
        bool fwd) const;

public:
    DirectStepI (
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        Currency const& c)
            : src_(src)
            , dst_(dst)
            , currency_ (c)
            , prevStep_ (ctx.prevStep)
            , isLast_ (ctx.isLast)
            , j_ (ctx.j)
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

    boost::optional<Quality>
    qualityUpperBound(ReadView const& v, bool& redeems) const override;

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

protected:
    std::string logStringImpl (char const* name) const
    {
        std::ostringstream ostr;
        ostr <<
            name << ": " <<
            "\nSrc: " << src_ <<
            "\nDst: " << dst_;
        return ostr.str ();
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
};

//------------------------------------------------------------------------------

// Flow is used in two different circumstances for transferring funds:
//  o Payments, and
//  o Offer crossing.
// The rules for handling funds in these two cases are almost, but not
// quite, the same.

// Payment DirectStep class (not offer crossing).
class DirectIPaymentStep : public DirectStepI<DirectIPaymentStep>
{
public:
    using DirectStepI<DirectIPaymentStep>::DirectStepI;
    using DirectStepI<DirectIPaymentStep>::check;

    bool verifyPrevStepRedeems (bool) const
    {
        // A payment doesn't care whether or not prevStepRedeems.
        return true;
    }

    bool verifyDstQualityIn (std::uint32_t dstQIn) const
    {
        // Payments have no particular expectations for what dstQIn will be.
        return true;
    }

    std::uint32_t
    quality (ReadView const& sb,
        // set true for quality in, false for quality out
        bool qin) const;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is true if dst holds an iou from src.
    std::pair<IOUAmount, bool>
    maxFlow (ReadView const& sb, IOUAmount const& desired) const;

    // Verify the consistency of the step.  These checks are specific to
    // payments and assume that general checks were already performed.
    TER
    check (StrandContext const& ctx,
        std::shared_ptr<const SLE> const& sleSrc) const;

    std::string logString () const override
    {
        return logStringImpl ("DirectIPaymentStep");
    }
};

// Offer crossing DirectStep class (not a payment).
class DirectIOfferCrossingStep : public DirectStepI<DirectIOfferCrossingStep>
{
public:
    using DirectStepI<DirectIOfferCrossingStep>::DirectStepI;
    using DirectStepI<DirectIOfferCrossingStep>::check;

    bool verifyPrevStepRedeems (bool prevStepRedeems) const
    {
        // During offer crossing we rely on the fact that prevStepRedeems
        // will *always* be false.  That's because:
        //  o If there's a prevStep_, it will always be a BookStep.
        //  o BookStep::redeems() aways returns false when offer crossing.
        // An assert based on this return value will tell us if that
        // behavior changes.
        return !prevStepRedeems;
    }

    bool verifyDstQualityIn (std::uint32_t dstQIn) const
    {
        // Due to a couple of factors dstQIn is always QUALITY_ONE for
        // offer crossing.  If that changes we need to know.
        return dstQIn == QUALITY_ONE;
    }

    std::uint32_t
    quality (ReadView const& sb,
        // set true for quality in, false for quality out
        bool qin) const;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is true if dst holds an iou from src.
    std::pair<IOUAmount, bool>
    maxFlow (ReadView const& sb, IOUAmount const& desired) const;

    // Verify the consistency of the step.  These checks are specific to
    // offer crossing and assume that general checks were already performed.
    TER
    check (StrandContext const& ctx,
        std::shared_ptr<const SLE> const& sleSrc) const;


    std::string logString () const override
    {
        return logStringImpl ("DirectIOfferCrossingStep");
    }
};

//------------------------------------------------------------------------------

std::uint32_t
DirectIPaymentStep::quality (ReadView const& sb,
    // set true for quality in, false for quality out
    bool qin) const
{
    if (src_ == dst_)
        return QUALITY_ONE;

    auto const sle = sb.read (keylet::line (dst_, src_, currency_));

    if (!sle)
        return QUALITY_ONE;

    auto const& field = [this, qin]() -> SF_U32 const&
    {
        if (qin)
        {
            // compute dst quality in
            if (this->dst_ < this->src_)
                return sfLowQualityIn;
            else
                return sfHighQualityIn;
        }
        else
        {
            // compute src quality out
            if (this->src_ < this->dst_)
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

std::uint32_t
DirectIOfferCrossingStep::quality (ReadView const&,
    // set true for quality in, false for quality out
    bool) const
{
    // If offer crossing then ignore trust line Quality fields.  This
    // preserves a long-standing tradition.
    return QUALITY_ONE;
}

std::pair<IOUAmount, bool>
DirectIPaymentStep::maxFlow (ReadView const& sb, IOUAmount const&) const
{
    return maxPaymentFlow (sb);
}

std::pair<IOUAmount, bool>
DirectIOfferCrossingStep::maxFlow (
    ReadView const& sb, IOUAmount const& desired) const
{
    // When isLast and offer crossing then ignore trust line limits.  Offer
    // crossing has the ability to exceed the limit set by a trust line.
    // We presume that if someone is creating an offer then they intend to
    // fill as much of that offer as possible, even if the offer exceeds
    // the limit that a trust line sets.
    //
    // A note on using "out" as the desired parameter for maxFlow.  In some
    // circumstances during payments we end up needing a value larger than
    // "out" for "maxSrcToDst".  But as of now (June 2016) that never happens
    // during offer crossing.  That's because, due to a couple of factors,
    // "dstQIn" is always QUALITY_ONE for offer crossing.

    if (isLast_)
        return {desired, false};

    return maxPaymentFlow (sb);
}

TER
DirectIPaymentStep::check (
    StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc) const
{
    // Since this is a payment a trust line must be present.  Perform all
    // trust line related checks.
    {
        auto const sleLine = ctx.view.read (keylet::line (src_, dst_, currency_));
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

        if (ctx.prevStep &&
            fix1449(ctx.view.info().parentCloseTime))
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

TER
DirectIOfferCrossingStep::check (
    StrandContext const&, std::shared_ptr<const SLE> const&) const
{
    // The standard checks are all we can do because any remaining checks
    // require the existence of a trust line.  Offer crossing does not
    // require a pre-existing trust line.
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

template <class TDerived>
std::pair<IOUAmount, bool>
DirectStepI<TDerived>::maxPaymentFlow (ReadView const& sb) const
{
    auto const srcOwed = toAmount<IOUAmount> (
        accountHolds (sb, src_, currency_, dst_, fhIGNORE_FREEZE, j_));

    if (srcOwed.signum () > 0)
        return {srcOwed, true};

    // srcOwed is negative or zero
    return {creditLimit2 (sb, dst_, src_, currency_) + srcOwed, false};
}

template <class TDerived>
bool
DirectStepI<TDerived>::redeems (ReadView const& sb, bool fwd) const
{
    if (fwd && cache_)
        return cache_->srcRedeems;

    auto const srcOwed = accountHolds (
        sb, src_, currency_, dst_, fhIGNORE_FREEZE, j_);
    return srcOwed.signum () > 0;
}

template <class TDerived>
std::pair<IOUAmount, IOUAmount>
DirectStepI<TDerived>::revImp (
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    IOUAmount const& out)
{
    cache_.reset ();

    bool srcRedeems;
    IOUAmount maxSrcToDst;

    std::tie (maxSrcToDst, srcRedeems) =
        static_cast<TDerived const*>(this)->maxFlow (sb, out);

    std::uint32_t srcQOut, dstQIn;
    std::tie (srcQOut, dstQIn) = qualities (sb, srcRedeems, false);
    assert (static_cast<TDerived const*>(this)->verifyDstQualityIn (dstQIn));

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
template <class TDerived>
void
DirectStepI<TDerived>::setCacheLimiting (
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

template <class TDerived>
std::pair<IOUAmount, IOUAmount>
DirectStepI<TDerived>::fwdImp (
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    IOUAmount const& in)
{
    assert (cache_);

    bool srcRedeems;
    IOUAmount maxSrcToDst;
    std::tie (maxSrcToDst, srcRedeems) =
        static_cast<TDerived const*>(this)->maxFlow (sb, cache_->srcToDst);

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

template <class TDerived>
std::pair<bool, EitherAmount>
DirectStepI<TDerived>::validFwd (
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
        static_cast<TDerived const*>(this)->maxFlow (sb, cache_->srcToDst);

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

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepI<TDerived>::qualities (
    ReadView const& sb,
    bool srcRedeems,
    bool fwd) const
{
    if (srcRedeems)
    {
        if (!prevStep_)
            return {QUALITY_ONE, QUALITY_ONE};

        auto const prevStepQIn = prevStep_->lineQualityIn (sb);
        auto srcQOut = static_cast<TDerived const*>(this)->quality (
            sb, /* src quality out */ false);

        if (prevStepQIn > srcQOut)
            srcQOut = prevStepQIn;
        return {srcQOut, QUALITY_ONE};
    }
    else
    {
        // Charge a transfer rate when issuing and previous step redeems
        auto const prevStepRedeems = prevStep_ && prevStep_->redeems (sb, fwd);
        assert (static_cast<TDerived const*>(this)->verifyPrevStepRedeems (
            prevStepRedeems));

        std::uint32_t const srcQOut =
            prevStepRedeems ? transferRate (sb, src_).value : QUALITY_ONE;
        auto dstQIn = static_cast<TDerived const*>(this)->quality (
            sb, /* dst quality in */ true);

        if (isLast_ && dstQIn > QUALITY_ONE)
            dstQIn = QUALITY_ONE;
        return {srcQOut, dstQIn};
    }
}

template <class TDerived>
std::uint32_t
DirectStepI<TDerived>::lineQualityIn (ReadView const& v) const
{
    // dst quality in
    return static_cast<TDerived const*>(this)->quality (
        v, /* dst quality in */ true);
}

template <class TDerived>
boost::optional<Quality>
DirectStepI<TDerived>::qualityUpperBound(ReadView const& v, bool& redeems) const
{
    auto const prevRedeems = redeems;
    redeems = this->redeems(v, true);
    std::uint32_t const srcQOut =
        (prevRedeems && !redeems) ? transferRate(v, src_).value : QUALITY_ONE;
    auto dstQIn = static_cast<TDerived const*>(this)->quality (
        v, /* dst quality in */ true);

    if (isLast_ && dstQIn > QUALITY_ONE)
        dstQIn = QUALITY_ONE;
    Issue const iss{currency_, src_};
    return Quality(getRate(STAmount(iss, srcQOut), STAmount(iss, dstQIn)));
}

template <class TDerived>
TER DirectStepI<TDerived>::check (StrandContext const& ctx) const
{
    // The following checks apply for both payments and offer crossing.
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

    auto const sleSrc = ctx.view.read (keylet::account (src_));
    if (!sleSrc)
    {
        JLOG (j_.warn())
            << "DirectStepI: can't receive IOUs from non-existent issuer: "
            << src_;
        return terNO_ACCOUNT;
    }

    // pure issue/redeem can't be frozen
    if (!(ctx.isLast && ctx.isFirst))
    {
        auto const ter = checkFreeze(ctx.view, src_, dst_, currency_);
        if (ter != tesSUCCESS)
            return ter;
    }

    // If previous step was a direct step then we need to check
    // no ripple flags.
    if (ctx.prevStep)
    {
        if (auto prevSrc = ctx.prevStep->directStepSrcAcct())
        {
            auto const ter = checkNoRipple(
                ctx.view, *prevSrc, src_, dst_, currency_, j_);
            if (ter != tesSUCCESS)
                return ter;
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

    return static_cast<TDerived const*>(this)->check (ctx, sleSrc);
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
    if (auto ds =
        dynamic_cast<DirectStepI<DirectIPaymentStep> const*> (&step))
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
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<DirectIOfferCrossingStep> (ctx, src, dst, c);
        ter = offerCrossingStep->check (ctx);
        r = std::move (offerCrossingStep);
    }
    else // payment
    {
        auto paymentStep =
            std::make_unique<DirectIPaymentStep> (ctx, src, dst, c);
        ter = paymentStep->check (ctx);
        r = std::move (paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move (r)};
}

} // ripple
