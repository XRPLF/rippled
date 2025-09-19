//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/misc/MPTUtils.h>
#include <xrpld/app/paths/Credit.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/Quality.h>

#include <boost/container/flat_set.hpp>

#include <numeric>
#include <sstream>

namespace ripple {

template <class TDerived>
class MPTEndpointStep
    : public StepImp<MPTAmount, MPTAmount, MPTEndpointStep<TDerived>>
{
protected:
    AccountID const src_;
    AccountID const dst_;
    MPTIssue const mptIssue_;

    // Charge transfer fees when the prev step redeems
    Step const* const prevStep_ = nullptr;
    bool const isLast_;
    // Direct payment between the holders
    // Used by maxFlow's last step.
    bool const isDirectBetweenHolders_;
    beast::Journal const j_;

    struct Cache
    {
        MPTAmount in;
        MPTAmount srcToDst;
        MPTAmount out;
        DebtDirection srcDebtDir;

        Cache(
            MPTAmount const& in_,
            MPTAmount const& srcToDst_,
            MPTAmount const& out_,
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
    std::pair<MPTAmount, DebtDirection>
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

    void
    resetCache(DebtDirection dir);

public:
    MPTEndpointStep(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        MPTID const& mpt)
        : src_(src)
        , dst_(dst)
        , mptIssue_(mpt)
        , prevStep_(ctx.prevStep)
        , isLast_(ctx.isLast)
        , isDirectBetweenHolders_(
              mptIssue_ == ctx.strandDeliver &&
              ctx.strandSrc != mptIssue_.getIssuer() &&
              ctx.strandDst != mptIssue_.getIssuer() &&
              (ctx.isFirst || (ctx.prevStep && !ctx.prevStep->bookStepBook())))
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
    MPTID const&
    mptID() const
    {
        return mptIssue_.getMptID();
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

    std::pair<MPTAmount, MPTAmount>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        MPTAmount const& out);

    std::pair<MPTAmount, MPTAmount>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        MPTAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in)
        override;

    // Check for error, existing liquidity, and violations of auth/frozen
    // constraints.
    TER
    check(StrandContext const& ctx) const;

    void
    setCacheLimiting(
        MPTAmount const& fwdIn,
        MPTAmount const& fwdSrcToDst,
        MPTAmount const& fwdOut,
        DebtDirection srcDebtDir);

    friend bool
    operator==(MPTEndpointStep const& lhs, MPTEndpointStep const& rhs)
    {
        return lhs.src_ == rhs.src_ && lhs.dst_ == rhs.dst_ &&
            lhs.mptIssue_ == rhs.mptIssue_;
    }

    friend bool
    operator!=(MPTEndpointStep const& lhs, MPTEndpointStep const& rhs)
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
        if (auto ds = dynamic_cast<MPTEndpointStep const*>(&rhs))
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

// Payment MPTEndpointStep class (not offer crossing).
class MPTEndpointPaymentStep : public MPTEndpointStep<MPTEndpointPaymentStep>
{
public:
    using MPTEndpointStep<MPTEndpointPaymentStep>::MPTEndpointStep;
    using MPTEndpointStep<MPTEndpointPaymentStep>::check;

    bool
    verifyPrevStepDebtDirection(DebtDirection) const
    {
        // A payment doesn't care regardless of prevStepRedeems.
        return true;
    }

    // Verify the consistency of the step.  These checks are specific to
    // payments and assume that general checks were already performed.
    TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc)
        const;

    std::string
    logString() const override
    {
        return logStringImpl("MPTEndpointPaymentStep");
    }

    // Not applicable for payment
    TER
    checkCreateMPT(ApplyView&, DebtDirection)
    {
        return tesSUCCESS;
    }
};

// Offer crossing MPTEndpointStep class (not a payment).
class MPTEndpointOfferCrossingStep
    : public MPTEndpointStep<MPTEndpointOfferCrossingStep>
{
public:
    using MPTEndpointStep<MPTEndpointOfferCrossingStep>::MPTEndpointStep;
    using MPTEndpointStep<MPTEndpointOfferCrossingStep>::check;

    bool
    verifyPrevStepDebtDirection(DebtDirection prevStepDir) const
    {
        // During offer crossing we rely on the fact that prevStepRedeems
        // will *always* issue.  That's because:
        //  o If there's a prevStep_, it will always be a BookStep.
        //  o BookStep::debtDirection() always returns `issues` when offer
        //  crossing.
        // An assert based on this return value will tell us if that
        // behavior changes.
        return issues(prevStepDir);
    }

    // Verify the consistency of the step.  These checks are specific to
    // offer crossing and assume that general checks were already performed.
    TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc)
        const;

    std::string
    logString() const override
    {
        return logStringImpl("MPTEndpointOfferCrossingStep");
    }

    // Can be created in rev or fwd (if limiting step) direction.
    TER
    checkCreateMPT(ApplyView& view, DebtDirection srcDebtDir);
};

//------------------------------------------------------------------------------

TER
MPTEndpointPaymentStep::check(
    StrandContext const& ctx,
    std::shared_ptr<const SLE> const& sleSrc) const
{
    // Since this is a payment, MPToken must be present.  Perform all
    // MPToken related checks.

    // requireAuth checks if MPTIssuance exist. Note that issuer to issuer
    // payment is invalid
    auto const& issuer = mptIssue_.getIssuer();
    if (src_ != issuer)
    {
        if (auto const ter = requireAuth(ctx.view, mptIssue_, src_);
            ter != tesSUCCESS)
            return ter;
    }

    if (dst_ != issuer)
    {
        if (auto const ter = requireAuth(ctx.view, mptIssue_, dst_);
            ter != tesSUCCESS)
            return ter;
    }

    // Direct MPT payment, no DEX
    if (mptIssue_ == ctx.strandDeliver &&
        (ctx.isFirst || (ctx.prevStep && !ctx.prevStep->bookStepBook())))
    {
        // Between holders
        if (isDirectBetweenHolders_)
        {
            auto const& holder = ctx.isFirst ? src_ : dst_;
            // Payment between the holders
            if (isFrozen(ctx.view, holder, mptIssue_))
                return tecLOCKED;

            if (auto const ter =
                    canTransfer(ctx.view, mptIssue_, holder, ctx.strandDst);
                ter != tesSUCCESS)
                return ter;
        }
        // Don't need to check if a payment is between issuer and holder
        // in either direction
    }
    // Cross-token MPT payment via DEX
    else
    {
        if (ctx.isFirst && src_ != mptIssue_.getIssuer())
        {
            if (auto const ter =
                    checkMPTDEXAllowed(ctx.view, mptIssue_, src_, std::nullopt);
                ter != tesSUCCESS)
                return ter;
        }
        else if (
            ctx.prevStep && ctx.prevStep->bookStepBook() &&
            dst_ != mptIssue_.getIssuer())
        {
            if (auto const ter =
                    checkMPTDEXAllowed(ctx.view, mptIssue_, dst_, std::nullopt);
                ter != tesSUCCESS)
                return ter;
        }
    }

    // Can't check for creditBalance/Limit unless it's the first step.
    // Otherwise, even if OutstandingAmount is equal to MaximumAmount
    // a payment can still be successful. For instance, when a balance
    // is shifted from one holder to another.

    if (!prevStep_)
    {
        auto const owed = accountHolds(
            ctx.view, src_, mptIssue_, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);
        // Already at MaximumAmount
        if (owed <= beast::zero)
            return tecPATH_DRY;
    }

    return tesSUCCESS;
}

TER
MPTEndpointOfferCrossingStep::check(
    StrandContext const& ctx,
    std::shared_ptr<const SLE> const&) const
{
    auto const& holder = ctx.isFirst ? src_ : dst_;
    auto const& issuer = mptIssue_.getIssuer();
    if (holder != issuer)
    {
        if (auto const ter =
                checkMPTDEXAllowed(ctx.view, mptIssue_, holder, issuer);
            ter != tesSUCCESS)
            return ter;
    }
    return tesSUCCESS;
}

TER
MPTEndpointOfferCrossingStep::checkCreateMPT(
    ApplyView& view,
    ripple::DebtDirection srcDebtDir)
{
    // TakerPays is the last step if offer crossing
    if (isLast_)
    {
        // Create MPToken for the offer's owner. No need to check
        // for the reserve since the offer doesn't go on the books
        // if crossed. Insufficient reserve is allowed if the offer
        // crossed. See CreateOffer::applyGuts() for reserve check.
        if (auto const err =
                MPTokenAuthorize::checkCreateMPT(view, mptIssue_, dst_, j_);
            err != tesSUCCESS)
        {
            JLOG(j_.trace())
                << "MPTEndpointStep::checkCreateMPT: failed create MPT";
            resetCache(srcDebtDir);
            return err;
        }
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

template <class TDerived>
std::pair<MPTAmount, DebtDirection>
MPTEndpointStep<TDerived>::maxPaymentFlow(ReadView const& sb) const
{
    auto const maxFlow =
        accountHolds(sb, src_, mptIssue_, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);

    // From a holder to an issuer
    if (src_ != mptIssue_.getIssuer())
        return {toAmount<MPTAmount>(maxFlow), DebtDirection::redeems};

    // From an issuer to a holder
    if (auto const sle = sb.read(keylet::mptIssuance(mptIssue_)))
    {
        // If issuer is the source account, and it is direct payment then
        // MPTEndpointStep is the only step. Provide available maxFlow.
        if (!prevStep_)
            return {toAmount<MPTAmount>(maxFlow), DebtDirection::issues};

        // MPTEndpointStep is the last step. It's always issuing in
        // this case. Can't infer at this point what the maxFlow is, because
        // the previous step may issue or redeem. Allow OutstandingAmount
        // to temporarily overflow. Let the previous step figure out how
        // to limit the flow.
        std::int64_t const maxAmount = maxMPTAmount(*sle);
        return {MPTAmount{maxAmount}, DebtDirection::issues};
    }

    return {MPTAmount{0}, DebtDirection::issues};
}

template <class TDerived>
DebtDirection
MPTEndpointStep<TDerived>::debtDirection(
    ReadView const& sb,
    StrandDirection dir) const
{
    if (dir == StrandDirection::forward && cache_)
        return cache_->srcDebtDir;

    return (src_ == mptIssue_.getIssuer()) ? DebtDirection::issues
                                           : DebtDirection::redeems;
}

template <class TDerived>
std::pair<MPTAmount, MPTAmount>
MPTEndpointStep<TDerived>::revImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    MPTAmount const& out)
{
    cache_.reset();

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxPaymentFlow(sb);

    auto const [srcQOut, dstQIn] =
        qualities(sb, srcDebtDir, StrandDirection::reverse);
    (void)dstQIn;

    MPTIssue const srcToDstIss(mptIssue_);

    JLOG(j_.trace()) << "MPTEndpointStep::rev"
                     << " srcRedeems: " << redeems(srcDebtDir)
                     << " outReq: " << to_string(out)
                     << " maxSrcToDst: " << to_string(maxSrcToDst)
                     << " srcQOut: " << srcQOut << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "MPTEndpointStep::rev: dry";
        resetCache(srcDebtDir);
        return {beast::zero, beast::zero};
    }

    if (auto const err =
            static_cast<TDerived*>(this)->checkCreateMPT(sb, srcDebtDir);
        err != tesSUCCESS)
        return {beast::zero, beast::zero};

    // Don't have to factor in dstQIn since it is always QUALITY_ONE
    MPTAmount const srcToDst = out;

    if (srcToDst <= maxSrcToDst)
    {
        MPTAmount const in =
            mulRatio(srcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        cache_.emplace(in, srcToDst, srcToDst, srcDebtDir);
        auto const ter = rippleCredit(
            sb,
            src_,
            dst_,
            toSTAmount(srcToDst, srcToDstIss),
            /*checkIssuer*/ false,
            j_);
        if (ter != tesSUCCESS)
        {
            JLOG(j_.trace()) << "MPTEndpointStep::rev: error " << ter;
            resetCache(srcDebtDir);
            return {beast::zero, beast::zero};
        }
        JLOG(j_.trace()) << "MPTEndpointStep::rev: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir)
                         << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst)
                         << " out: " << to_string(out);
        return {in, out};
    }

    // limiting node
    MPTAmount const in =
        mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
    // Don't have to factor in dsqQIn since it's always QUALITY_ONE
    MPTAmount const actualOut = maxSrcToDst;
    cache_.emplace(in, maxSrcToDst, actualOut, srcDebtDir);

    auto const ter = rippleCredit(
        sb,
        src_,
        dst_,
        toSTAmount(maxSrcToDst, srcToDstIss),
        /*checkIssuer*/ false,
        j_);
    if (ter != tesSUCCESS)
    {
        JLOG(j_.trace()) << "MPTEndpointStep::rev: error " << ter;
        resetCache(srcDebtDir);
        return {beast::zero, beast::zero};
    }
    JLOG(j_.trace()) << "MPTEndpointStep::rev: Limiting"
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
MPTEndpointStep<TDerived>::setCacheLimiting(
    MPTAmount const& fwdIn,
    MPTAmount const& fwdSrcToDst,
    MPTAmount const& fwdOut,
    DebtDirection srcDebtDir)
{
    if (cache_->in < fwdIn)
    {
        MPTAmount const smallDiff(1);
        auto const diff = fwdIn - cache_->in;
        if (diff > smallDiff)
        {
            if (!cache_->in.value() ||
                (Number(fwdIn.value()) / Number(cache_->in.value())) >
                    Number(101, -2))
            {
                // Detect large diffs on forward pass so they may be
                // investigated
                JLOG(j_.warn())
                    << "MPTEndpointStep::fwd: setCacheLimiting"
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
std::pair<MPTAmount, MPTAmount>
MPTEndpointStep<TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    MPTAmount const& in)
{
    XRPL_ASSERT(cache_, "MPTEndpointStep<TDerived>::fwdImp : valid cache");

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxPaymentFlow(sb);

    auto const [srcQOut, dstQIn] =
        qualities(sb, srcDebtDir, StrandDirection::forward);
    (void)dstQIn;

    MPTIssue const srcToDstIss(mptIssue_);

    JLOG(j_.trace()) << "MPTEndpointStep::fwd"
                     << " srcRedeems: " << redeems(srcDebtDir)
                     << " inReq: " << to_string(in)
                     << " maxSrcToDst: " << to_string(maxSrcToDst)
                     << " srcQOut: " << srcQOut << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "MPTEndpointStep::fwd: dry";
        resetCache(srcDebtDir);
        return {beast::zero, beast::zero};
    }

    if (auto const err =
            static_cast<TDerived*>(this)->checkCreateMPT(sb, srcDebtDir);
        err != tesSUCCESS)
        return {beast::zero, beast::zero};

    MPTAmount const srcToDst =
        mulRatio(in, QUALITY_ONE, srcQOut, /*roundUp*/ false);

    if (srcToDst <= maxSrcToDst)
    {
        // Don't have to factor in dstQIn since it's always QUALITY_ONE
        MPTAmount const out = srcToDst;
        setCacheLimiting(in, srcToDst, out, srcDebtDir);
        auto const ter = rippleCredit(
            sb,
            src_,
            dst_,
            toSTAmount(cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ false,
            j_);
        if (ter != tesSUCCESS)
        {
            JLOG(j_.trace()) << "MPTEndpointStep::fwd: error " << ter;
            resetCache(srcDebtDir);
            return {beast::zero, beast::zero};
        }
        JLOG(j_.trace()) << "MPTEndpointStep::fwd: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir)
                         << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst)
                         << " out: " << to_string(out);
    }
    else
    {
        // limiting node
        MPTAmount const actualIn =
            mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        // Don't have to factor in dstQIn since it's always QUALITY_ONE
        MPTAmount const out = maxSrcToDst;
        setCacheLimiting(actualIn, maxSrcToDst, out, srcDebtDir);
        auto const ter = rippleCredit(
            sb,
            src_,
            dst_,
            toSTAmount(cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ false,
            j_);
        if (ter != tesSUCCESS)
        {
            JLOG(j_.trace()) << "MPTEndpointStep::fwd: error " << ter;
            resetCache(srcDebtDir);
            return {beast::zero, beast::zero};
        }
        JLOG(j_.trace()) << "MPTEndpointStep::rev: Limiting"
                         << " srcRedeems: " << redeems(srcDebtDir)
                         << " in: " << to_string(actualIn)
                         << " srcToDst: " << to_string(srcToDst)
                         << " out: " << to_string(out);
    }
    return {cache_->in, cache_->out};
}

template <class TDerived>
std::pair<bool, EitherAmount>
MPTEndpointStep<TDerived>::validFwd(
    PaymentSandbox& sb,
    ApplyView& afView,
    EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG(j_.trace()) << "Expected valid cache in validFwd";
        return {false, EitherAmount(MPTAmount(beast::zero))};
    }

    auto const savCache = *cache_;

    XRPL_ASSERT(
        in.holds<MPTAmount>(), "MPTEndpoint<TDerived>::validFwd : is MPT");

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxPaymentFlow(sb);
    (void)srcDebtDir;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp(sb, afView, dummy, in.get<MPTAmount>());  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount(MPTAmount(beast::zero))};
    }

    if (maxSrcToDst < cache_->srcToDst)
    {
        JLOG(j_.warn()) << "MPTEndpointStep: Strand re-execute check failed."
                        << " Exceeded max src->dst limit"
                        << " max src->dst: " << to_string(maxSrcToDst)
                        << " actual src->dst: " << to_string(cache_->srcToDst);
        return {false, EitherAmount(cache_->out)};
    }

    if (!(checkNear(savCache.in, cache_->in) &&
          checkNear(savCache.out, cache_->out)))
    {
        JLOG(j_.warn()) << "MPTEndpointStep: Strand re-execute check failed."
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
MPTEndpointStep<TDerived>::qualitiesSrcRedeems(ReadView const& sb) const
{
    if (!prevStep_)
        return {QUALITY_ONE, QUALITY_ONE};

    auto const prevStepQIn = prevStep_->lineQualityIn(sb);
    // Unlike trustline MPT doesn't have line quality field
    auto srcQOut = QUALITY_ONE;

    if (prevStepQIn > srcQOut)
        srcQOut = prevStepQIn;
    return {srcQOut, QUALITY_ONE};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
MPTEndpointStep<TDerived>::qualitiesSrcIssues(
    ReadView const& sb,
    DebtDirection prevStepDebtDirection) const
{
    // Charge a transfer rate when issuing and previous step redeems

    XRPL_ASSERT(
        static_cast<TDerived const*>(this)->verifyPrevStepDebtDirection(
            prevStepDebtDirection),
        "MPTEndpointStep<TDerived>::qualitiesSrcIssues : verify prev step debt "
        "direction");

    std::uint32_t const srcQOut = redeems(prevStepDebtDirection)
        ? transferRate(sb, mptIssue_.getMptID()).value
        : QUALITY_ONE;

    // Unlike trustline, MPT doesn't have line quality field
    return {srcQOut, QUALITY_ONE};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
MPTEndpointStep<TDerived>::qualities(
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
MPTEndpointStep<TDerived>::lineQualityIn(ReadView const& v) const
{
    // dst quality in
    return QUALITY_ONE;
}

template <class TDerived>
std::pair<std::optional<Quality>, DebtDirection>
MPTEndpointStep<TDerived>::qualityUpperBound(
    ReadView const& v,
    DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::forward);

    auto const [srcQOut, dstQIn] = redeems(dir)
        ? qualitiesSrcRedeems(v)
        : qualitiesSrcIssues(v, prevStepDir);
    (void)dstQIn;

    MPTIssue const iss{mptIssue_};
    // Be careful not to switch the parameters to `getRate`. The
    // `getRate(offerOut, offerIn)` function is usually used for offers. It
    // returns offerIn/offerOut. For a direct step, the rate is srcQOut/dstQIn
    // (Input*dstQIn/srcQOut = Output; So rate = srcQOut/dstQIn). Although the
    // first parameter is called `offerOut`, it should take the `dstQIn`
    // variable.
    return {
        Quality(getRate(STAmount(iss, QUALITY_ONE), STAmount(iss, srcQOut))),
        dir};
}

template <class TDerived>
TER
MPTEndpointStep<TDerived>::check(StrandContext const& ctx) const
{
    // The following checks apply for both payments and offer crossing.
    if (!src_ || !dst_)
    {
        JLOG(j_.debug()) << "MPTEndpointStep: specified bad account.";
        return temBAD_PATH;
    }

    if (src_ == dst_)
    {
        JLOG(j_.debug()) << "MPTEndpointStep: same src and dst.";
        return temBAD_PATH;
    }

    auto const sleSrc = ctx.view.read(keylet::account(src_));
    if (!sleSrc)
    {
        JLOG(j_.warn())
            << "MPTEndpointStep: can't receive MPT from non-existent issuer: "
            << src_;
        return terNO_ACCOUNT;
    }

    if (ctx.seenBookOuts.count(mptIssue_))
    {
        if (!ctx.prevStep)
        {
            UNREACHABLE(
                "ripple::MPTEndpointStep::check : prev seen book without a "
                "prev step");
            return temBAD_PATH_LOOP;
        }

        // This is OK if the previous step is a book step that outputs this
        // issue
        if (auto book = ctx.prevStep->bookStepBook())
        {
            if (book->out.get<MPTIssue>() != mptIssue_)
                return temBAD_PATH_LOOP;
        }
    }

    if ((ctx.isFirst && !ctx.seenDirectAssets[0].insert(mptIssue_).second) ||
        (ctx.isLast && !ctx.seenDirectAssets[1].insert(mptIssue_).second))
    {
        JLOG(j_.debug()) << "MPTEndpointStep: loop detected: Index: "
                         << ctx.strandSize << ' ' << *this;
        return temBAD_PATH_LOOP;
    }

    // MPT can only be an endpoint
    if (!ctx.isLast && !ctx.isFirst)
    {
        JLOG(j_.warn()) << "MPTEndpointStep: MPT can only be an endpoint";
        return temBAD_PATH;
    }

    auto const& issuer = mptIssue_.getIssuer();
    if ((src_ != issuer && dst_ != issuer) ||
        (src_ == issuer && dst_ == issuer))
    {
        JLOG(j_.warn()) << "MPTEndpointStep: invalid src/dst";
        return temBAD_PATH;
    }

    return static_cast<TDerived const*>(this)->check(ctx, sleSrc);
}

template <class TDerived>
void
MPTEndpointStep<TDerived>::resetCache(ripple::DebtDirection dir)
{
    cache_.emplace(
        MPTAmount(beast::zero),
        MPTAmount(beast::zero),
        MPTAmount(beast::zero),
        dir);
}

//------------------------------------------------------------------------------

std::pair<TER, std::unique_ptr<Step>>
make_MPTEndpointStep(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    MPTID const& mpt)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing)
    {
        auto offerCrossingStep =
            std::make_unique<MPTEndpointOfferCrossingStep>(ctx, src, dst, mpt);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep =
            std::make_unique<MPTEndpointPaymentStep>(ctx, src, dst, mpt);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (ter != tesSUCCESS)
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

namespace test {
// Needed for testing
bool
mptEndpointStepEqual(
    Step const& step,
    AccountID const& src,
    AccountID const& dst,
    MPTID const& mptid)
{
    if (auto ds =
            dynamic_cast<MPTEndpointStep<MPTEndpointPaymentStep> const*>(&step))
    {
        return ds->src() == src && ds->dst() == dst && ds->mptID() == mptid;
    }
    return false;
}
}  // namespace test

}  // namespace ripple
