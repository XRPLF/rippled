#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/detail/EitherAmount.h>
#include <xrpl/tx/paths/detail/StepChecks.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <boost/container/flat_set.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace xrpl {

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
    beast::Journal const j_;

    struct Cache
    {
        IOUAmount in;
        IOUAmount srcToDst;
        IOUAmount out;
        DebtDirection srcDebtDir;

        Cache(
            IOUAmount const& in,
            IOUAmount const& srcToDst,
            IOUAmount const& out,
            DebtDirection srcDebtDir)
            : in(in), srcToDst(srcToDst), out(out), srcDebtDir(srcDebtDir)
        {
        }
    };

    std::optional<Cache> cache_;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is the debt direction of the source w.r.t. the dst
    [[nodiscard]] std::pair<IOUAmount, DebtDirection>
    maxPaymentFlow(ReadView const& sb) const;

    // Compute srcQOut and dstQIn when the source redeems.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcRedeems(ReadView const& sb) const;

    // Compute srcQOut and dstQIn when the source issues.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcIssues(ReadView const& sb, DebtDirection prevStepDebtDirection) const;

    // Returns srcQOut, dstQIn
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualities(ReadView const& sb, DebtDirection srcDebtDir, StrandDirection strandDir) const;

private:
    DirectStepI(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        Currency const& c)
        : src_(src)
        , dst_(dst)
        , currency_(c)
        , prevStep_(ctx.prevStep)
        , isLast_(ctx.isLast)
        , j_(ctx.j)
    {
    }

public:
    [[nodiscard]] AccountID const&
    src() const
    {
        return src_;
    }
    [[nodiscard]] AccountID const&
    dst() const
    {
        return dst_;
    }
    [[nodiscard]] Currency const&
    currency() const
    {
        return currency_;
    }

    [[nodiscard]] std::optional<EitherAmount>
    cachedIn() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->in);
    }

    [[nodiscard]] std::optional<EitherAmount>
    cachedOut() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->out);
    }

    [[nodiscard]] std::optional<AccountID>
    directStepSrcAcct() const override
    {
        return src_;
    }

    [[nodiscard]] std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const override
    {
        return std::make_pair(src_, dst_);
    }

    [[nodiscard]] DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override;

    [[nodiscard]] std::uint32_t
    lineQualityIn(ReadView const& v) const override;

    [[nodiscard]] std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection dir) const override;

    std::pair<IOUAmount, IOUAmount>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        IOUAmount const& out);

    std::pair<IOUAmount, IOUAmount>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        IOUAmount const& in);

    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) override;

    // Check for error, existing liquidity, and violations of auth/frozen
    // constraints.
    [[nodiscard]] TER
    check(StrandContext const& ctx) const;

    void
    setCacheLimiting(
        IOUAmount const& fwdIn,
        IOUAmount const& fwdSrcToDst,
        IOUAmount const& fwdOut,
        DebtDirection srcDebtDir);

    friend bool
    operator==(DirectStepI const& lhs, DirectStepI const& rhs)
    {
        return lhs.src_ == rhs.src_ && lhs.dst_ == rhs.dst_ && lhs.currency_ == rhs.currency_;
    }

    friend bool
    operator!=(DirectStepI const& lhs, DirectStepI const& rhs)
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
    [[nodiscard]] bool
    equal(Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<DirectStepI const*>(&rhs))
        {
            return *this == *ds;
        }
        return false;
    }

    friend TDerived;
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
    DirectIPaymentStep(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        Currency const& c)
        : DirectStepI<DirectIPaymentStep>(ctx, src, dst, c)
    {
    }

    using DirectStepI<DirectIPaymentStep>::check;

    static bool
    verifyPrevStepDebtDirection(DebtDirection)
    {
        // A payment doesn't care whether or not prevStepRedeems.
        return true;
    }

    static bool
    verifyDstQualityIn(std::uint32_t dstQIn)
    {
        // Payments have no particular expectations for what dstQIn will be.
        return true;
    }

    [[nodiscard]] std::uint32_t
    quality(ReadView const& sb, QualityDirection qDir) const;

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is the debt direction w.r.t. the source account
    [[nodiscard]] std::pair<IOUAmount, DebtDirection>
    maxFlow(ReadView const& sb, IOUAmount const& desired) const;

    // Verify the consistency of the step.  These checks are specific to
    // payments and assume that general checks were already performed.
    [[nodiscard]] TER
    check(StrandContext const& ctx, SLE::const_ref sleSrc) const;

    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("DirectIPaymentStep");
    }
};

// Offer crossing DirectStep class (not a payment).
class DirectIOfferCrossingStep : public DirectStepI<DirectIOfferCrossingStep>
{
public:
    DirectIOfferCrossingStep(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        Currency const& c)
        : DirectStepI<DirectIOfferCrossingStep>(ctx, src, dst, c)
    {
    }

    using DirectStepI<DirectIOfferCrossingStep>::check;

    static bool
    verifyPrevStepDebtDirection(DebtDirection prevStepDir)
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

    static bool
    verifyDstQualityIn(std::uint32_t dstQIn)
    {
        // Due to a couple of factors dstQIn is always QUALITY_ONE for
        // offer crossing.  If that changes we need to know.
        return dstQIn == QUALITY_ONE;
    }

    static std::uint32_t
    quality(ReadView const& sb, QualityDirection qDir);

    // Compute the maximum value that can flow from src->dst at
    // the best available quality.
    // return: first element is max amount that can flow,
    //         second is the debt direction w.r.t the source
    [[nodiscard]] std::pair<IOUAmount, DebtDirection>
    maxFlow(ReadView const& sb, IOUAmount const& desired) const;

    // Verify the consistency of the step.  These checks are specific to
    // offer crossing and assume that general checks were already performed.
    static TER
    check(StrandContext const& ctx, SLE::const_ref sleSrc);

    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("DirectIOfferCrossingStep");
    }
};

//------------------------------------------------------------------------------

std::uint32_t
DirectIPaymentStep::quality(ReadView const& sb, QualityDirection qDir) const
{
    if (src_ == dst_)
        return QUALITY_ONE;

    auto const sle = sb.read(keylet::trustLine(dst_, src_, currency_));

    if (!sle)
        return QUALITY_ONE;

    auto const& field = [&, this]() -> SF_UINT32 const& {
        if (qDir == QualityDirection::In)
        {
            // compute dst quality in
            if (this->dst_ < this->src_)
            {
                return sfLowQualityIn;
            }

            return sfHighQualityIn;
        }

        // compute src quality out
        if (this->src_ < this->dst_)
        {
            return sfLowQualityOut;
        }

        return sfHighQualityOut;
    }();

    if (!sle->isFieldPresent(field))
        return QUALITY_ONE;

    auto const q = (*sle)[field];
    if (q == 0u)
        return QUALITY_ONE;
    return q;
}

std::uint32_t
DirectIOfferCrossingStep::quality(ReadView const&, QualityDirection qDir)
{
    // If offer crossing then ignore trust line Quality fields.  This
    // preserves a long-standing tradition.
    return QUALITY_ONE;
}

std::pair<IOUAmount, DebtDirection>
DirectIPaymentStep::maxFlow(ReadView const& sb, IOUAmount const&) const
{
    auto const flow = maxPaymentFlow(sb);

    // check() lets a redeeming step through even when dst_ was never
    // authorized to hold src_'s issuance, because returning dst_'s own IOU
    // creates no unauthorized holding. check() runs once at strand
    // construction, though, while the flow engine re-evaluates maxFlow every
    // iteration: once the redeemed balance reaches zero maxPaymentFlow
    // switches to the Issues branch and would extend the unauthorized line
    // fresh credit, which is the zero-crossing mint behind XRPLF/rippled
    // issue #5450. Go dry instead, so such a line drains to zero and stops.
    // A pseudo-account dst_ is exempt for the reason given in check().
    if (issues(flow.second) && sb.rules().enabled(fixCleanup3_5_0) &&
        !isTesSuccess(requireAuth(sb, Issue{currency_, src_}, dst_, AuthType::StrongAuth)) &&
        !isPseudoAccount(sb, dst_))
    {
        return {IOUAmount(beast::kZero), DebtDirection::Issues};
    }

    return flow;
}

std::pair<IOUAmount, DebtDirection>
DirectIOfferCrossingStep::maxFlow(ReadView const& sb, IOUAmount const& desired) const
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
        return {desired, DebtDirection::Issues};

    return maxPaymentFlow(sb);
}

TER
DirectIPaymentStep::check(StrandContext const& ctx, SLE::const_ref sleSrc) const
{
    // Since this is a payment a trust line must be present.  Perform all
    // trust line related checks.
    {
        auto const sleLine = ctx.view.read(keylet::trustLine(src_, dst_, currency_));
        if (!sleLine)
        {
            JLOG(j_.trace()) << "DirectStepI: No credit line. " << *this;
            return terNO_LINE;
        }

        auto const authField = (src_ > dst_) ? lsfHighAuth : lsfLowAuth;

        // src_ holds dst_'s IOU exactly when the balance favors src_.
        int const balanceSign = (*sleLine)[sfBalance].signum();
        bool const srcHoldsDstIou = src_ < dst_ ? balanceSign > 0 : balanceSign < 0;

        if (sleSrc->isFlag(lsfRequireAuth) && !sleLine->isFlag(authField))
        {
            // Post fixCleanup3_5_0 an unauthorized line may not receive at
            // all: the zero-balance condition below was the historical
            // grandfather clause letting a line that already carried a
            // balance keep receiving, which is how an unauthorized position
            // could grow (see XRPLF/rippled issue #5450). The
            // ValidTrustLineAuth invariant remains the backstop for flows
            // that bypass the payment engine.
            //
            // srcHoldsDstIou excludes the opposite direction: there the
            // balance favors src_, so the step returns dst_'s own issuance
            // to it instead of handing dst_ a balance src_ never
            // authorized. That redemption is one of the two permitted
            // flows, and maxFlow caps it at the redeemable amount so it
            // cannot cross zero into an unauthorized holding.
            if (ctx.view.rules().enabled(fixCleanup3_5_0) && !srcHoldsDstIou)
            {
                JLOG(j_.debug()) << "DirectStepI: unauthorized line may not receive."
                                 << " src: " << src_;
                return tecNO_AUTH;
            }

            // A zero balance means !srcHoldsDstIou, so post amendment the
            // gate above has already returned: this branch is reachable
            // pre-amendment only.
            if ((*sleLine)[sfBalance] == beast::kZero)
            {
                JLOG(j_.debug()) << "DirectStepI: can't receive IOUs from issuer without auth."
                                 << " src: " << src_;
                return terNO_AUTH;
            }
        }

        // A balance the issuer never authorized may only be delivered to the
        // issuer itself. toStrand appends the transaction's Destination as
        // the final path element, so the step carrying ctx.isLast delivers
        // to dst_; dst_ being this line's issuer then makes the strand a
        // redemption. Any onward step spends the balance to a third party
        // or converts it, which only a redemption or a Clawback may do. The
        // ValidTrustLineAuth invariant remains the backstop for paths that
        // bypass the payment engine.
        if (!ctx.isLast && ctx.view.rules().enabled(fixCleanup3_5_0))
        {
            // A pseudo-account src_ is exempt: it cannot submit a TrustSet
            // to become authorized, mirroring the fixCleanup3_4_0 carve-out
            // inside requireAuth without depending on that amendment.
            if (srcHoldsDstIou && !isPseudoAccount(ctx.view, src_) &&
                !isTesSuccess(
                    requireAuth(ctx.view, Issue{currency_, dst_}, src_, AuthType::StrongAuth)))
            {
                JLOG(j_.debug()) << "DirectStepI: cannot spend an unauthorized balance. src: "
                                 << src_;
                return tecNO_AUTH;
            }
        }

        // src_ being an AMM account means this step delivers its LPToken to
        // dst_, handing dst_ exposure to both of the AMM's pool assets, so
        // dst_ must be authorized for both; see checkLPTokenAuthorization.
        // The reverse direction (dst_ is the AMM) is a return to the issuer
        // and stays legal, letting an unauthorized holder divest.
        if (ctx.view.rules().enabled(fixCleanup3_5_0) && sleSrc->isFieldPresent(sfAMMID))
        {
            if (auto const ter = checkLPTokenAuthorization(ctx.view, dst_, src_);
                !isTesSuccess(ter))
            {
                JLOG(j_.debug()) << "DirectStepI: cannot deliver an LPToken to an account not "
                                    "authorized for the AMM's assets. dst: "
                                 << dst_;
                return ter;
            }
        }

        if (ctx.prevStep != nullptr)
        {
            if (ctx.prevStep->bookStepBook())
            {
                if (sleLine->isFlag((src_ > dst_) ? lsfHighNoRipple : lsfLowNoRipple))
                    return terNO_RIPPLE;
            }
        }
    }

    {
        auto const owed = creditBalance(ctx.view, dst_, src_, currency_);
        if (owed <= beast::kZero)
        {
            auto const limit = creditLimit(ctx.view, dst_, src_, currency_);
            if (-owed >= limit)
            {
                JLOG(j_.debug()) << "DirectStepI: dry: owed: " << owed << " limit: " << limit;
                return tecPATH_DRY;
            }
        }
    }
    return tesSUCCESS;
}

TER
DirectIOfferCrossingStep::check(StrandContext const&, SLE::const_ref)
{
    // The standard checks are all we can do because any remaining checks
    // require the existence of a trust line.  Offer crossing does not
    // require a pre-existing trust line.
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

template <class TDerived>
std::pair<IOUAmount, DebtDirection>
DirectStepI<TDerived>::maxPaymentFlow(ReadView const& sb) const
{
    // IgnoreAuth: srcOwed also feeds redemption back to the issuer, the one
    // movement an unauthorized line is allowed. Strands that would spend
    // such a balance past the issuer are rejected in check(), and the
    // ValidTrustLineAuth invariant backstops everything else.
    auto const srcOwed = toAmount<IOUAmount>(accountHolds(
        sb, src_, currency_, dst_, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_));

    if (srcOwed.signum() > 0)
        return {srcOwed, DebtDirection::Redeems};

    // srcOwed is negative or zero
    return {creditLimit2(sb, dst_, src_, currency_) + srcOwed, DebtDirection::Issues};
}

template <class TDerived>
DebtDirection
DirectStepI<TDerived>::debtDirection(ReadView const& sb, StrandDirection dir) const
{
    if (dir == StrandDirection::Forward && cache_)
        return cache_->srcDebtDir;

    // IgnoreAuth: see maxPaymentFlow.
    auto const srcOwed = accountHolds(
        sb, src_, currency_, dst_, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_);
    return srcOwed.signum() > 0 ? DebtDirection::Redeems : DebtDirection::Issues;
}

template <class TDerived>
std::pair<IOUAmount, IOUAmount>
DirectStepI<TDerived>::revImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    IOUAmount const& out)
{
    cache_.reset();

    auto const [maxSrcToDst, srcDebtDir] = static_cast<TDerived const*>(this)->maxFlow(sb, out);

    auto const [srcQOut, dstQIn] = qualities(sb, srcDebtDir, StrandDirection::Reverse);
    XRPL_ASSERT(
        static_cast<TDerived const*>(this)->verifyDstQualityIn(dstQIn),
        "xrpl::DirectStepI : valid destination quality");

    Issue const srcToDstIss(currency_, redeems(srcDebtDir) ? dst_ : src_);

    JLOG(j_.trace()) << "DirectStepI::rev"
                     << " srcRedeems: " << redeems(srcDebtDir) << " outReq: " << to_string(out)
                     << " maxSrcToDst: " << to_string(maxSrcToDst) << " srcQOut: " << srcQOut
                     << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "DirectStepI::rev: dry";
        cache_.emplace(
            IOUAmount(beast::kZero), IOUAmount(beast::kZero), IOUAmount(beast::kZero), srcDebtDir);
        return {beast::kZero, beast::kZero};
    }

    IOUAmount const srcToDst = mulRatio(out, QUALITY_ONE, dstQIn, /*roundUp*/ true);

    if (srcToDst <= maxSrcToDst)
    {
        IOUAmount const in = mulRatio(srcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        cache_.emplace(in, srcToDst, out, srcDebtDir);
        directSendNoFee(
            sb,
            src_,
            dst_,
            toSTAmount(srcToDst, srcToDstIss),
            /*checkIssuer*/ true,
            j_);
        JLOG(j_.trace()) << "DirectStepI::rev: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst) << " out: " << to_string(out);
        return {in, out};
    }

    // limiting node
    IOUAmount const in = mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
    IOUAmount const actualOut = mulRatio(maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
    cache_.emplace(in, maxSrcToDst, actualOut, srcDebtDir);
    directSendNoFee(
        sb,
        src_,
        dst_,
        toSTAmount(maxSrcToDst, srcToDstIss),
        /*checkIssuer*/ true,
        j_);
    JLOG(j_.trace()) << "DirectStepI::rev: Limiting"
                     << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(in)
                     << " srcToDst: " << to_string(maxSrcToDst) << " out: " << to_string(out);
    return {in, actualOut};
}

// The forward pass should never have more liquidity than the reverse
// pass. But sometimes rounding differences cause the forward pass to
// deliver more liquidity. Use the cached values from the reverse pass
// to prevent this.
template <class TDerived>
void
DirectStepI<TDerived>::setCacheLimiting(
    IOUAmount const& fwdIn,
    IOUAmount const& fwdSrcToDst,
    IOUAmount const& fwdOut,
    DebtDirection srcDebtDir)
{
    // NOLINTBEGIN(bugprone-unchecked-optional-access) cache_ always set before setCacheLimiting is
    // called
    if (cache_->in < fwdIn)
    {
        IOUAmount const smallDiff(1, -9);
        auto const diff = fwdIn - cache_->in;
        if (diff > smallDiff)
        {
            if (fwdIn.exponent() != cache_->in.exponent() || !cache_->in.mantissa() ||
                (double(fwdIn.mantissa()) / double(cache_->in.mantissa())) > 1.01)
            {
                // Detect large diffs on forward pass so they may be
                // investigated
                JLOG(j_.warn()) << "DirectStepI::fwd: setCacheLimiting"
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
    // NOLINTEND(bugprone-unchecked-optional-access)
};

template <class TDerived>
std::pair<IOUAmount, IOUAmount>
DirectStepI<TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    IOUAmount const& in)
{
    XRPL_ASSERT(cache_, "xrpl::DirectStepI::fwdImp : cache is set");
    // NOLINTBEGIN(bugprone-unchecked-optional-access) assert above

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxFlow(sb, cache_->srcToDst);

    auto const [srcQOut, dstQIn] = qualities(sb, srcDebtDir, StrandDirection::Forward);

    Issue const srcToDstIss(currency_, redeems(srcDebtDir) ? dst_ : src_);

    JLOG(j_.trace()) << "DirectStepI::fwd"
                     << " srcRedeems: " << redeems(srcDebtDir) << " inReq: " << to_string(in)
                     << " maxSrcToDst: " << to_string(maxSrcToDst) << " srcQOut: " << srcQOut
                     << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "DirectStepI::fwd: dry";
        cache_.emplace(
            IOUAmount(beast::kZero), IOUAmount(beast::kZero), IOUAmount(beast::kZero), srcDebtDir);
        return {beast::kZero, beast::kZero};
    }

    IOUAmount const srcToDst = mulRatio(in, QUALITY_ONE, srcQOut, /*roundUp*/ false);

    if (srcToDst <= maxSrcToDst)
    {
        IOUAmount const out = mulRatio(srcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting(in, srcToDst, out, srcDebtDir);
        directSendNoFee(
            sb,
            src_,
            dst_,
            toSTAmount(cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ true,
            j_);
        JLOG(j_.trace()) << "DirectStepI::fwd: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst) << " out: " << to_string(out);
    }
    else
    {
        // limiting node
        IOUAmount const actualIn = mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        IOUAmount const out = mulRatio(maxSrcToDst, dstQIn, QUALITY_ONE, /*roundUp*/ false);
        setCacheLimiting(actualIn, maxSrcToDst, out, srcDebtDir);
        directSendNoFee(
            sb,
            src_,
            dst_,
            toSTAmount(cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ true,
            j_);
        JLOG(j_.trace()) << "DirectStepI::rev: Limiting"
                         << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(actualIn)
                         << " srcToDst: " << to_string(srcToDst) << " out: " << to_string(out);
    }
    return {cache_->in, cache_->out};
    // NOLINTEND(bugprone-unchecked-optional-access)
}

template <class TDerived>
std::pair<bool, EitherAmount>
DirectStepI<TDerived>::validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG(j_.trace()) << "Expected valid cache in validFwd";
        return {false, EitherAmount(IOUAmount(beast::kZero))};
    }

    auto const savCache = *cache_;

    XRPL_ASSERT(in.holds<IOUAmount>(), "xrpl::DirectStepI::validFwd : input is IOU");

    auto const [maxSrcToDst, srcDebtDir] =
        static_cast<TDerived const*>(this)->maxFlow(sb, cache_->srcToDst);
    (void)srcDebtDir;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp(sb, afView, dummy, in.get<IOUAmount>());  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount(IOUAmount(beast::kZero))};
    }

    // NOLINTBEGIN(bugprone-unchecked-optional-access) fwdImp sets cache_ on success
    if (maxSrcToDst < cache_->srcToDst)
    {
        JLOG(j_.warn()) << "DirectStepI: Strand re-execute check failed."
                        << " Exceeded max src->dst limit"
                        << " max src->dst: " << to_string(maxSrcToDst)
                        << " actual src->dst: " << to_string(cache_->srcToDst);
        return {false, EitherAmount(cache_->out)};
    }

    if (!(checkNear(savCache.in, cache_->in) && checkNear(savCache.out, cache_->out)))
    {
        JLOG(j_.warn()) << "DirectStepI: Strand re-execute check failed."
                        << " ExpectedIn: " << to_string(savCache.in)
                        << " CachedIn: " << to_string(cache_->in)
                        << " ExpectedOut: " << to_string(savCache.out)
                        << " CachedOut: " << to_string(cache_->out);
        return {false, EitherAmount(cache_->out)};
    }
    return {true, EitherAmount(cache_->out)};
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepI<TDerived>::qualitiesSrcRedeems(ReadView const& sb) const
{
    if (prevStep_ == nullptr)
        return {QUALITY_ONE, QUALITY_ONE};

    auto const prevStepQIn = prevStep_->lineQualityIn(sb);
    auto srcQOut = static_cast<TDerived const*>(this)->quality(sb, QualityDirection::Out);

    if (prevStepQIn > srcQOut)
        srcQOut = prevStepQIn;
    return {srcQOut, QUALITY_ONE};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepI<TDerived>::qualitiesSrcIssues(ReadView const& sb, DebtDirection prevStepDebtDirection)
    const
{
    // Charge a transfer rate when issuing and previous step redeems

    XRPL_ASSERT(
        static_cast<TDerived const*>(this)->verifyPrevStepDebtDirection(prevStepDebtDirection),
        "xrpl::DirectStepI::qualitiesSrcIssues : will prevStepDebtDirection "
        "issue");

    std::uint32_t const srcQOut =
        redeems(prevStepDebtDirection) ? transferRate(sb, src_).value : QUALITY_ONE;
    auto dstQIn = static_cast<TDerived const*>(this)->quality(sb, QualityDirection::In);

    if (isLast_ && dstQIn > QUALITY_ONE)
        dstQIn = QUALITY_ONE;
    return {srcQOut, dstQIn};
}

// Returns srcQOut, dstQIn
template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepI<TDerived>::qualities(
    ReadView const& sb,
    DebtDirection srcDebtDir,
    StrandDirection strandDir) const
{
    if (redeems(srcDebtDir))
    {
        return qualitiesSrcRedeems(sb);
    }

    auto const prevStepDebtDirection = [&] {
        if (prevStep_)
            return prevStep_->debtDirection(sb, strandDir);
        return DebtDirection::Issues;
    }();
    return qualitiesSrcIssues(sb, prevStepDebtDirection);
}

template <class TDerived>
std::uint32_t
DirectStepI<TDerived>::lineQualityIn(ReadView const& v) const
{
    // dst quality in
    return static_cast<TDerived const*>(this)->quality(v, QualityDirection::In);
}

template <class TDerived>
std::pair<std::optional<Quality>, DebtDirection>
DirectStepI<TDerived>::qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::Forward);

    auto const [srcQOut, dstQIn] =
        redeems(dir) ? qualitiesSrcRedeems(v) : qualitiesSrcIssues(v, prevStepDir);

    Issue const iss{currency_, src_};
    // Be careful not to switch the parameters to `getRate`. The
    // `getRate(offerOut, offerIn)` function is usually used for offers. It
    // returns offerIn/offerOut. For a direct step, the rate is srcQOut/dstQIn
    // (Input*dstQIn/srcQOut = Output; So rate = srcQOut/dstQIn). Although the
    // first parameter is called `offerOut`, it should take the `dstQIn`
    // variable.
    return {Quality(getRate(STAmount(iss, dstQIn), STAmount(iss, srcQOut))), dir};
}

template <class TDerived>
TER
DirectStepI<TDerived>::check(StrandContext const& ctx) const
{
    // The following checks apply for both payments and offer crossing.
    if (!src_ || !dst_)
    {
        JLOG(j_.debug()) << "DirectStepI: specified bad account.";
        return temBAD_PATH;
    }

    if (src_ == dst_)
    {
        JLOG(j_.debug()) << "DirectStepI: same src and dst.";
        return temBAD_PATH;
    }

    auto const sleSrc = ctx.view.read(keylet::account(src_));
    if (!sleSrc)
    {
        JLOG(j_.warn()) << "DirectStepI: can't receive IOUs from non-existent issuer: " << src_;
        return terNO_ACCOUNT;
    }

    // pure issue/redeem can't be frozen
    if (!(ctx.isLast && ctx.isFirst))
    {
        if (auto const ter = checkFreeze(ctx.view, src_, dst_, currency_); !isTesSuccess(ter))
            return ter;

        // An LPToken redeemed against its AMM (dst_ is the LPToken issuer on
        // this hop) cannot move if a pool asset is an MPT that forbids
        // transfers between these accounts. A no-op unless dst_ is an AMM whose
        // pool holds such an MPT (so it is implicitly gated by featureMPTokensV2).
        if (auto const ter = canTransferLPToken(ctx.view, src_, dst_, dst_); !isTesSuccess(ter))
            return ter;
    }

    // If previous step was a direct step then we need to check
    // no ripple flags.
    if (ctx.prevStep != nullptr)
    {
        if (auto prevSrc = ctx.prevStep->directStepSrcAcct())
        {
            auto const ter = checkNoRipple(ctx.view, *prevSrc, src_, dst_, currency_, j_);
            if (!isTesSuccess(ter))
                return ter;
        }
    }
    {
        Issue const srcIssue{currency_, src_};
        Issue const dstIssue{currency_, dst_};

        if (ctx.seenBookOuts.count(srcIssue) != 0u)
        {
            if (ctx.prevStep == nullptr)
            {
                // LCOV_EXCL_START
                UNREACHABLE(
                    "xrpl::DirectStepI::check : prev seen book without a "
                    "prev step");
                return temBAD_PATH_LOOP;
                // LCOV_EXCL_STOP
            }

            // This is OK if the previous step is a book step that outputs this
            // issue
            if (auto book = ctx.prevStep->bookStepBook())
            {
                if (book->out.get<Issue>() != srcIssue)
                    return temBAD_PATH_LOOP;
            }
        }

        if (!ctx.seenDirectAssets[0].insert(srcIssue).second ||
            !ctx.seenDirectAssets[1].insert(dstIssue).second)
        {
            JLOG(j_.debug()) << "DirectStepI: loop detected: Index: " << ctx.strandSize << ' '
                             << *this;
            return temBAD_PATH_LOOP;
        }
    }

    return static_cast<TDerived const*>(this)->check(ctx, sleSrc);
}

//------------------------------------------------------------------------------

namespace test {
// Needed for testing
bool
directStepEqual(
    Step const& step,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency)
{
    if (auto ds = dynamic_cast<DirectStepI<DirectIPaymentStep> const*>(&step))
    {
        return ds->src() == src && ds->dst() == dst && ds->currency() == currency;
    }
    return false;
}
}  // namespace test

//------------------------------------------------------------------------------

std::pair<TER, std::unique_ptr<Step>>
makeDirectStepI(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    Currency const& c)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing != OfferCrossing::No)
    {
        auto offerCrossingStep = std::make_unique<DirectIOfferCrossingStep>(ctx, src, dst, c);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep = std::make_unique<DirectIPaymentStep>(ctx, src, dst, c);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (!isTesSuccess(ter))
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

}  // namespace xrpl
