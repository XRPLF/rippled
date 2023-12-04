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
#include <ripple/app/paths/impl/StepChecks.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/CFTAmount.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Quality.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template <class TDerived>
class DirectStepCFT
    : public StepImp<CFTAmount, CFTAmount, DirectStepCFT<TDerived>>
{
protected:
    AccountID const src_;
    AccountID const dst_;
    AccountID const issuer_;
    uint256 cftID_;

    // Charge transfer fees when the prev step redeems
    Step const* const prevStep_ = nullptr;
    bool const isLast_;
    beast::Journal const j_;

    struct Cache
    {
        CFTAmount in;
        CFTAmount srcToDst;
        CFTAmount out;
        DebtDirection srcDebtDir;

        Cache(
            CFTAmount const& in_,
            CFTAmount const& srcToDst_,
            CFTAmount const& out_,
            DebtDirection srcDebtDir_)
            : in(in_), srcToDst(srcToDst_), out(out_), srcDebtDir(srcDebtDir_)
        {
        }
    };

    std::optional<Cache> cache_;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is the debt direction of the source w.r.t. the dst
    std::pair<CFTAmount, DebtDirection>
    maxPaymentFlow(ReadView const& sb) const;

    // Compute srcQOut and dstQIn when the source redeems.
    std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcRedeems(ReadView const& sb) const;

    // Compute srcQOut and dstQIn when the source issues.
    std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcIssues(ReadView const& sb, DebtDirection prevStepDebtDirection)
        const;

    // Returns srcQOut, dstQIn
    std::pair<std::uint32_t, std::uint32_t>
    qualities(
        ReadView const& sb,
        DebtDirection srcDebtDir,
        StrandDirection strandDir) const;

public:
    DirectStepCFT(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        uint256 const& asset)
        : src_(src)
        , dst_(dst)
        , issuer_(ctx.strandDeliver.account)
        , cftID_(asset)
        , prevStep_(ctx.prevStep)
        , isLast_(ctx.isLast)
        , j_(ctx.j)
    {
    }

    AccountID const&
    src() const
    {
        return src_;
    }
    AccountID const&
    dst() const
    {
        return dst_;
    }
    uint256 const&
    cftID() const
    {
        return cftID_;
    }

    std::optional<EitherAmount>
    cachedIn() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->in);
    }

    std::optional<EitherAmount>
    cachedOut() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->out);
    }

    std::optional<AccountID>
    directStepSrcAcct() const override
    {
        return src_;
    }

    std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const override
    {
        return std::make_pair(src_, dst_);
    }

    DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override;

    std::uint32_t
    lineQualityIn(ReadView const& v) const override;

    std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection dir) const override;

    std::pair<CFTAmount, CFTAmount>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        CFTAmount const& out);

    std::pair<CFTAmount, CFTAmount>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        CFTAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in)
        override;

    // Check for error, existing liquidity, and violations of auth/frozen
    // constraints.
    TER
    check(StrandContext const& ctx) const;

    void
    setCacheLimiting(
        CFTAmount const& fwdIn,
        CFTAmount const& fwdSrcToDst,
        CFTAmount const& fwdOut,
        DebtDirection srcDebtDir);

    friend bool
    operator==(DirectStepCFT const& lhs, DirectStepCFT const& rhs)
    {
        return lhs.src_ == rhs.src_ && lhs.dst_ == rhs.dst_ &&
            lhs.cftID_ == rhs.cftID_;
    }

    friend bool
    operator!=(DirectStepCFT const& lhs, DirectStepCFT const& rhs)
    {
        return !(lhs == rhs);
    }

protected:
    std::string
    logStringImpl(char const* name) const
    {
        std::ostringstream ostr;
        ostr << name << ": "
             << "\nSrc: " << src_ << "\nDst: " << dst_;
        return ostr.str();
    }

private:
    bool
    equal(Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<DirectStepCFT const*>(&rhs))
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
class DirectCFTPaymentStep : public DirectStepCFT<DirectCFTPaymentStep>
{
public:
    using DirectStepCFT<DirectCFTPaymentStep>::DirectStepCFT;
    using DirectStepCFT<DirectCFTPaymentStep>::check;

    bool verifyPrevStepDebtDirection(DebtDirection) const
    {
        // A payment doesn't care whether or not prevStepRedeems.
        return true;
    }

    bool
    verifyDstQualityIn(std::uint32_t dstQIn) const
    {
        // Payments have no particular expectations for what dstQIn will be.
        return true;
    }

    std::uint32_t
    quality(ReadView const& sb, QualityDirection qDir) const;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is the debt direction w.r.t. the source account
    std::pair<CFTAmount, DebtDirection>
    maxFlow(ReadView const& sb, CFTAmount const& desired) const;

    // Verify the consistency of the step.  These checks are specific to
    // payments and assume that general checks were already performed.
    TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc)
        const;

    std::string
    logString() const override
    {
        return logStringImpl("DirectCFTPaymentStep");
    }
};

// Offer crossing DirectStep class (not a payment).
class DirectCFTOfferCrossingStep
    : public DirectStepCFT<DirectCFTOfferCrossingStep>
{
public:
    using DirectStepCFT<DirectCFTOfferCrossingStep>::DirectStepCFT;
    using DirectStepCFT<DirectCFTOfferCrossingStep>::check;

    bool
    verifyPrevStepDebtDirection(DebtDirection prevStepDir) const
    {
        // During offer crossing we rely on the fact that prevStepRedeems
        // will *always* issue.  That's because:
        //  o If there's a prevStep_, it will always be a BookStep.
        //  o BookStep::debtDirection() aways returns `issues` when offer
        //  crossing.
        // An assert based on this return value will tell us if that
        // behavior changes.
        return issues(prevStepDir);
    }

    bool
    verifyDstQualityIn(std::uint32_t dstQIn) const
    {
        // Due to a couple of factors dstQIn is always QUALITY_ONE for
        // offer crossing.  If that changes we need to know.
        return dstQIn == QUALITY_ONE;
    }

    std::uint32_t
    quality(ReadView const& sb, QualityDirection qDir) const;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is the debt direction w.r.t the source
    std::pair<CFTAmount, DebtDirection>
    maxFlow(ReadView const& sb, CFTAmount const& desired) const;

    // Verify the consistency of the step.  These checks are specific to
    // offer crossing and assume that general checks were already performed.
    TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc)
        const;

    std::string
    logString() const override
    {
        return logStringImpl("DirectCFTOfferCrossingStep");
    }
};

//------------------------------------------------------------------------------

std::uint32_t
DirectCFTPaymentStep::quality(ReadView const& sb, QualityDirection qDir) const
{
    return QUALITY_ONE;
}

std::uint32_t
DirectCFTOfferCrossingStep::quality(ReadView const&, QualityDirection qDir)
    const
{
    // If offer crossing then ignore trust line Quality fields.  This
    // preserves a long-standing tradition.
    return QUALITY_ONE;
}

std::pair<CFTAmount, DebtDirection>
DirectCFTPaymentStep::maxFlow(ReadView const& sb, CFTAmount const&) const
{
    return maxPaymentFlow(sb);
}

std::pair<CFTAmount, DebtDirection>
DirectCFTOfferCrossingStep::maxFlow(
    ReadView const& sb,
    CFTAmount const& desired) const
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
        return {desired, DebtDirection::issues};

    return maxPaymentFlow(sb);
}

TER
DirectCFTPaymentStep::check(
    StrandContext const& ctx,
    std::shared_ptr<const SLE> const& sleSrc) const
{
    // Since this is a payment a CFToken must be present.  Perform all
    // CFToken related checks.
    // TODO
    if (!ctx.view.exists(keylet::cftIssuance(cftID_)))
        return tecOBJECT_NOT_FOUND;
    if (src_ != issuer_)
    {
        auto const cftokenID = keylet::cftoken(cftID_, src_);
        if (!ctx.view.exists(cftokenID))
            return tecOBJECT_NOT_FOUND;
    }
    if (dst_ != issuer_)
    {
        auto const cftokenID = keylet::cftoken(cftID_, dst_);
        if (!ctx.view.exists(cftokenID))
            return tecOBJECT_NOT_FOUND;
    }
#if 0
    // Since this is a payment a trust line must be present.  Perform all
    // trust line related checks.
    {
        auto const sleLine = ctx.view.read(keylet::line(src_, dst_, asset_));
        if (!sleLine)
        {
            JLOG(j_.trace()) << "DirectStepCFT: No credit line. " << *this;
            return terNO_LINE;
        }

        auto const authField = (src_ > dst_) ? lsfHighAuth : lsfLowAuth;

        if (((*sleSrc)[sfFlags] & lsfRequireAuth) &&
            !((*sleLine)[sfFlags] & authField) &&
            (*sleLine)[sfBalance] == beast::zero)
        {
            JLOG(j_.warn())
                << "DirectStepCFT: can't receive IOUs from issuer without auth."
                << " src: " << src_;
            return terNO_AUTH;
        }

        if (ctx.prevStep)
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
        auto const owed = creditBalance(ctx.view, dst_, src_, cftID_);
        if (owed <= beast::zero)
        {
            auto const limit = creditLimit(ctx.view, dst_, src_, cftID_);
            if (-owed >= limit)
            {
                JLOG(j_.debug()) << "DirectStepCFT: dry: owed: " << owed
                                 << " limit: " << limit;
                return tecPATH_DRY;
            }
        }
    }
#endif
    return tesSUCCESS;
}

TER
DirectCFTOfferCrossingStep::check(
    StrandContext const&,
    std::shared_ptr<const SLE> const&) const
{
    // The standard checks are all we can do because any remaining checks
    // require the existence of a trust line.  Offer crossing does not
    // require a pre-existing trust line.
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

template <class TDerived>
std::pair<CFTAmount, DebtDirection>
DirectStepCFT<TDerived>::maxPaymentFlow(ReadView const& sb) const
{
    // TODO
    if (src_ != issuer_)
    {
        auto const srcOwed = toAmount<CFTAmount>(
            accountHolds(sb, src_, cftID_, issuer_, fhIGNORE_FREEZE, j_));

        return {srcOwed, DebtDirection::redeems};
    }

    if (auto const sle = sb.read(keylet::cftIssuance(cftID_)))
    {
        std::int64_t const max =
            [&]() {
                auto const max = sle->getFieldU64(sfMaximumAmount);
                return max > 0 ? max : STAmount::cMaxNativeN;  // TODO
            }() -
            sle->getFieldU64(sfOutstandingAmount);
        return {CFTAmount{max}, DebtDirection::issues};
    }

    return {CFTAmount{0}, DebtDirection::issues};

#if 0
    // srcOwed is negative or zero
    return {
        creditLimit2(sb, dst_, src_, asset_) + srcOwed,
        DebtDirection::issues};
#endif
}

template <class TDerived>
DebtDirection
DirectStepCFT<TDerived>::debtDirection(ReadView const& sb, StrandDirection dir)
    const
{
    if (dir == StrandDirection::forward && cache_)
        return cache_->srcDebtDir;

    auto const srcOwed =
        accountHolds(sb, src_, cftID_, dst_, fhIGNORE_FREEZE, j_);
    return srcOwed.signum() > 0 ? DebtDirection::redeems
                                : DebtDirection::issues;
}

template <class TDerived>
std::pair<CFTAmount, CFTAmount>
DirectStepCFT<TDerived>::revImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    CFTAmount const& out)
{
    cache_.reset();

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxFlow(sb, out);

    auto const [srcQOut, dstQIn] =
        qualities(sb, srcDebtDir, StrandDirection::reverse);
    assert(static_cast<TDerived const*>(this)->verifyDstQualityIn(dstQIn));

    Issue const srcToDstIss(cftID_, redeems(srcDebtDir) ? dst_ : src_);

    JLOG(j_.trace()) << "DirectStepCFT::rev"
                     << " srcRedeems: " << redeems(srcDebtDir)
                     << " outReq: " << to_string(out)
                     << " maxSrcToDst: " << to_string(maxSrcToDst)
                     << " srcQOut: " << srcQOut << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "DirectStepCFT::rev: dry";
        cache_.emplace(
            CFTAmount(beast::zero),
            CFTAmount(beast::zero),
            CFTAmount(beast::zero),
            srcDebtDir);
        return {beast::zero, beast::zero};
    }

    CFTAmount const srcToDst =
        mulRatio(out, QUALITY_ONE, dstQIn, /*roundUp*/ true);

    if (srcToDst <= maxSrcToDst)
    {
        CFTAmount const in =
            mulRatio(srcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        cache_.emplace(in, srcToDst, srcToDst, srcDebtDir);
        rippleCFTCredit(sb, src_, dst_, toSTAmount(srcToDst, srcToDstIss), j_);
        JLOG(j_.trace()) << "DirectStepCFT::rev: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir)
                         << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst)
                         << " out: " << to_string(out);
        return {in, out};
    }

    // limiting node
    CFTAmount const in =
        mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
    CFTAmount const actualOut =
        mulRatio(maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
    cache_.emplace(in, maxSrcToDst, actualOut, srcDebtDir);

    rippleCFTCredit(sb, src_, dst_, toSTAmount(maxSrcToDst, srcToDstIss), j_);
    JLOG(j_.trace()) << "DirectStepCFT::rev: Limiting"
                     << " srcRedeems: " << redeems(srcDebtDir)
                     << " in: " << to_string(in)
                     << " srcToDst: " << to_string(maxSrcToDst)
                     << " out: " << to_string(out);
    return {in, actualOut};
}

// The forward pass should never have more liquidity than the reverse
// pass. But sometimes rounding differences cause the forward pass to
// deliver more liquidity. Use the cached values from the reverse pass
// to prevent this.
template <class TDerived>
void
DirectStepCFT<TDerived>::setCacheLimiting(
    CFTAmount const& fwdIn,
    CFTAmount const& fwdSrcToDst,
    CFTAmount const& fwdOut,
    DebtDirection srcDebtDir)
{
    if (cache_->in < fwdIn)
    {
        CFTAmount const smallDiff(1);
        auto const diff = fwdIn - cache_->in;
        if (diff > smallDiff)
        {
            if (!cache_->in.cft() ||
                (double(fwdIn.cft()) / double(cache_->in.cft())) > 1.01)
            {
                // Detect large diffs on forward pass so they may be
                // investigated
                JLOG(j_.warn())
                    << "DirectStepCFT::fwd: setCacheLimiting"
                    << " fwdIn: " << to_string(fwdIn)
                    << " cacheIn: " << to_string(cache_->in)
                    << " fwdSrcToDst: " << to_string(fwdSrcToDst)
                    << " cacheSrcToDst: " << to_string(cache_->srcToDst)
                    << " fwdOut: " << to_string(fwdOut)
                    << " cacheOut: " << to_string(cache_->out);
                cache_.emplace(fwdIn, fwdSrcToDst, fwdOut, srcDebtDir);
                return;
            }
        }
    }
    cache_->in = fwdIn;
    if (fwdSrcToDst < cache_->srcToDst)
        cache_->srcToDst = fwdSrcToDst;
    if (fwdOut < cache_->out)
        cache_->out = fwdOut;
    cache_->srcDebtDir = srcDebtDir;
};

template <class TDerived>
std::pair<CFTAmount, CFTAmount>
DirectStepCFT<TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    CFTAmount const& in)
{
    assert(cache_);

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxFlow(sb, cache_->srcToDst);

    auto const [srcQOut, dstQIn] =
        qualities(sb, srcDebtDir, StrandDirection::forward);

    Issue const srcToDstIss(cftID_, redeems(srcDebtDir) ? dst_ : src_);

    JLOG(j_.trace()) << "DirectStepCFT::fwd"
                     << " srcRedeems: " << redeems(srcDebtDir)
                     << " inReq: " << to_string(in)
                     << " maxSrcToDst: " << to_string(maxSrcToDst)
                     << " srcQOut: " << srcQOut << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "DirectStepCFT::fwd: dry";
        cache_.emplace(
            CFTAmount(beast::zero),
            CFTAmount(beast::zero),
            CFTAmount(beast::zero),
            srcDebtDir);
        return {beast::zero, beast::zero};
    }

    CFTAmount const srcToDst =
        mulRatio(in, QUALITY_ONE, srcQOut, /*roundUp*/ false);

    if (srcToDst <= maxSrcToDst)
    {
        CFTAmount const out =
            mulRatio(srcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting(in, srcToDst, out, srcDebtDir);
        rippleCFTCredit(
            sb, src_, dst_, toSTAmount(cache_->srcToDst, srcToDstIss), j_);
        JLOG(j_.trace()) << "DirectStepCFT::fwd: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir)
                         << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst)
                         << " out: " << to_string(out);
    }
    else
    {
        // limiting node
        CFTAmount const actualIn =
            mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        CFTAmount const out =
            mulRatio(maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting(actualIn, maxSrcToDst, out, srcDebtDir);
        rippleCFTCredit(
            sb, src_, dst_, toSTAmount(cache_->srcToDst, srcToDstIss), j_);
        JLOG(j_.trace()) << "DirectStepCFT::rev: Limiting"
                         << " srcRedeems: " << redeems(srcDebtDir)
                         << " in: " << to_string(actualIn)
                         << " srcToDst: " << to_string(srcToDst)
                         << " out: " << to_string(out);
    }
    return {cache_->in, cache_->out};
}

template <class TDerived>
std::pair<bool, EitherAmount>
DirectStepCFT<TDerived>::validFwd(
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG(j_.trace()) << "Expected valid cache in validFwd";
        return {false, EitherAmount(CFTAmount(beast::zero))};
    }

    auto const savCache = *cache_;

    assert(!in.native);

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxFlow(sb, cache_->srcToDst);
    (void)srcDebtDir;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp(sb, afView, dummy, in.cft);  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount(CFTAmount(beast::zero))};
    }

    if (maxSrcToDst < cache_->srcToDst)
    {
        JLOG(j_.warn()) << "DirectStepCFT: Strand re-execute check failed."
                        << " Exceeded max src->dst limit"
                        << " max src->dst: " << to_string(maxSrcToDst)
                        << " actual src->dst: " << to_string(cache_->srcToDst);
        return {false, EitherAmount(cache_->out)};
    }

    if (!(checkNear(savCache.in, cache_->in) &&
          checkNear(savCache.out, cache_->out)))
    {
        JLOG(j_.warn()) << "DirectStepCFT: Strand re-execute check failed."
                        << " ExpectedIn: " << to_string(savCache.in)
                        << " CachedIn: " << to_string(cache_->in)
                        << " ExpectedOut: " << to_string(savCache.out)
                        << " CachedOut: " << to_string(cache_->out);
        return {false, EitherAmount(cache_->out)};
    }
    return {true, EitherAmount(cache_->out)};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepCFT<TDerived>::qualitiesSrcRedeems(ReadView const& sb) const
{
    if (!prevStep_)
        return {QUALITY_ONE, QUALITY_ONE};

    auto const prevStepQIn = prevStep_->lineQualityIn(sb);
    auto srcQOut =
        static_cast<TDerived const*>(this)->quality(sb, QualityDirection::out);

    if (prevStepQIn > srcQOut)
        srcQOut = prevStepQIn;
    return {srcQOut, QUALITY_ONE};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepCFT<TDerived>::qualitiesSrcIssues(
    ReadView const& sb,
    DebtDirection prevStepDebtDirection) const
{
    // Charge a transfer rate when issuing and previous step redeems

    assert(static_cast<TDerived const*>(this)->verifyPrevStepDebtDirection(
        prevStepDebtDirection));

    std::uint32_t const srcQOut = redeems(prevStepDebtDirection)
        ? transferRate(sb, src_).value
        : QUALITY_ONE;
    auto dstQIn =
        static_cast<TDerived const*>(this)->quality(sb, QualityDirection::in);

    if (isLast_ && dstQIn > QUALITY_ONE)
        dstQIn = QUALITY_ONE;
    return {srcQOut, dstQIn};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepCFT<TDerived>::qualities(
    ReadView const& sb,
    DebtDirection srcDebtDir,
    StrandDirection strandDir) const
{
    if (redeems(srcDebtDir))
    {
        return qualitiesSrcRedeems(sb);
    }
    else
    {
        auto const prevStepDebtDirection = [&] {
            if (prevStep_)
                return prevStep_->debtDirection(sb, strandDir);
            return DebtDirection::issues;
        }();
        return qualitiesSrcIssues(sb, prevStepDebtDirection);
    }
}

template <class TDerived>
std::uint32_t
DirectStepCFT<TDerived>::lineQualityIn(ReadView const& v) const
{
    // dst quality in
    return static_cast<TDerived const*>(this)->quality(v, QualityDirection::in);
}

template <class TDerived>
std::pair<std::optional<Quality>, DebtDirection>
DirectStepCFT<TDerived>::qualityUpperBound(
    ReadView const& v,
    DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::forward);

    if (!v.rules().enabled(fixQualityUpperBound))
    {
        std::uint32_t const srcQOut = [&]() -> std::uint32_t {
            if (redeems(prevStepDir) && issues(dir))
                return transferRate(v, src_).value;
            return QUALITY_ONE;
        }();
        auto dstQIn = static_cast<TDerived const*>(this)->quality(
            v, QualityDirection::in);

        if (isLast_ && dstQIn > QUALITY_ONE)
            dstQIn = QUALITY_ONE;
        Issue const iss{cftID_, src_};
        return {
            Quality(getRate(STAmount(iss, srcQOut), STAmount(iss, dstQIn))),
            dir};
    }

    auto const [srcQOut, dstQIn] = redeems(dir)
        ? qualitiesSrcRedeems(v)
        : qualitiesSrcIssues(v, prevStepDir);

    Issue const iss{cftID_, src_};
    // Be careful not to switch the parameters to `getRate`. The
    // `getRate(offerOut, offerIn)` function is usually used for offers. It
    // returns offerIn/offerOut. For a direct step, the rate is srcQOut/dstQIn
    // (Input*dstQIn/srcQOut = Output; So rate = srcQOut/dstQIn). Although the
    // first parameter is called `offerOut`, it should take the `dstQIn`
    // variable.
    return {
        Quality(getRate(STAmount(iss, dstQIn), STAmount(iss, srcQOut))), dir};
}

template <class TDerived>
TER
DirectStepCFT<TDerived>::check(StrandContext const& ctx) const
{
    // The following checks apply for both payments and offer crossing.
    if (!src_ || !dst_)
    {
        JLOG(j_.debug()) << "DirectStepCFT: specified bad account.";
        return temBAD_PATH;
    }

    if (src_ == dst_)
    {
        JLOG(j_.debug()) << "DirectStepCFT: same src and dst.";
        return temBAD_PATH;
    }

    auto const sleSrc = ctx.view.read(keylet::account(src_));
    if (!sleSrc)
    {
        JLOG(j_.warn())
            << "DirectStepCFT: can't receive IOUs from non-existent issuer: "
            << src_;
        return terNO_ACCOUNT;
    }

    // pure issue/redeem can't be frozen
    if (!(ctx.isLast && ctx.isFirst))
    {
        auto const ter = checkFreeze(ctx.view, src_, dst_, cftID_);
        if (ter != tesSUCCESS)
            return ter;
    }

    {
        Issue const srcIssue{cftID_, src_};
        Issue const dstIssue{cftID_, dst_};

        if (ctx.seenBookOuts.count(srcIssue))
        {
            if (!ctx.prevStep)
            {
                assert(0);  // prev seen book without a prev step!?!
                return temBAD_PATH_LOOP;
            }

            // This is OK if the previous step is a book step that outputs this
            // issue
            if (auto book = ctx.prevStep->bookStepBook())
            {
                if (book->out != srcIssue)
                    return temBAD_PATH_LOOP;
            }
        }

        if (!ctx.seenDirectIssues[0].insert(srcIssue).second ||
            !ctx.seenDirectIssues[1].insert(dstIssue).second)
        {
            JLOG(j_.debug())
                << "DirectStepCFT: loop detected: Index: " << ctx.strandSize
                << ' ' << *this;
            return temBAD_PATH_LOOP;
        }
    }

    return static_cast<TDerived const*>(this)->check(ctx, sleSrc);
}

//------------------------------------------------------------------------------

std::pair<TER, std::unique_ptr<Step>>
make_DirectStepCFT(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    uint256 const& a)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<DirectCFTOfferCrossingStep>(ctx, src, dst, a);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep =
            std::make_unique<DirectCFTPaymentStep>(ctx, src, dst, a);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

}  // namespace ripple
