/** @file
 *  Payment-path step for Multi-Party Token (MPT) strand endpoints.
 *
 *  Defines `MPTEndpointStep<TDerived>` and its two concrete specialisations,
 *  `MPTEndpointPaymentStep` and `MPTEndpointOfferCrossingStep`. These handle
 *  the source or destination edge of a payment strand when the asset in motion
 *  is an MPT — the MPT counterpart to `XRPEndpointStep` and `DirectStepI`.
 *  Factory function `makeMptEndpointStep` and the test utility
 *  `test::mptEndpointStepEqual` are also defined here.
 */

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/detail/EitherAmount.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <boost/container/flat_set.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace xrpl {

/** CRTP base for the MPT strand-endpoint step.
 *
 *  Represents the first or last hop in a payment strand when the asset being
 *  transferred is a Multi-Party Token.  One of `src_` or `dst_` must always be
 *  the MPT issuer; two non-issuer accounts may never appear adjacent in the
 *  same MPT step.
 *
 *  Two concrete specialisations exist in this translation unit:
 *  `MPTEndpointPaymentStep` for normal payments and
 *  `MPTEndpointOfferCrossingStep` for DEX offer crossing.  The distinction
 *  is resolved once at construction time by `makeMptEndpointStep`; all
 *  hot-path methods dispatch to the derived type through CRTP without virtual
 *  overhead.
 *
 *  @tparam TDerived  Concrete subclass (`MPTEndpointPaymentStep` or
 *      `MPTEndpointOfferCrossingStep`).  Must provide `maxPaymentFlow`,
 *      `checkCreateMPT`, `verifyPrevStepDebtDirection`, and `check(ctx, sleSrc)`.
 */
template <class TDerived>
class MPTEndpointStep : public StepImp<MPTAmount, MPTAmount, MPTEndpointStep<TDerived>>
{
protected:
    AccountID const src_;
    AccountID const dst_;
    MPTIssue const mptIssue_;

    /** Pointer to the immediately preceding step; null when this is the first.
     *  Used to determine whether the previous step is redeeming, which triggers
     *  the MPT transfer rate when the issuer is the source.
     */
    Step const* const prevStep_ = nullptr;
    bool const isLast_;
    /** True when this step delivers MPT directly between two holders (neither
     *  endpoint is the issuer) and there is no book step immediately before it.
     *  Used in `check()` to apply holder-specific frozen and transfer rules.
     */
    bool const isDirectBetweenHolders_ = false;
    beast::Journal const j_;

    /** Per-pass result cache shared between `revImp` and `fwdImp`.
     *
     *  Written at the end of `revImp` and used by `fwdImp` as an upper bound
     *  via `setCacheLimiting()`, preventing forward-pass rounding from
     *  delivering more liquidity than the reverse pass determined was available.
     *  Reset to zero via `resetCache()` whenever a pass returns dry.
     */
    struct Cache
    {
        MPTAmount in;
        MPTAmount srcToDst;
        MPTAmount out;
        DebtDirection srcDebtDir;

        Cache(
            MPTAmount const& in,
            MPTAmount const& srcToDst,
            MPTAmount const& out,
            DebtDirection srcDebtDir)
            : in(in), srcToDst(srcToDst), out(out), srcDebtDir(srcDebtDir)
        {
        }
    };

    std::optional<Cache> cache_;

    /** Compute the maximum MPT amount that can flow from `src_` to `dst_`.
     *
     *  For a holder source, the limit is the holder's current balance.  For an
     *  issuer source with no prior step, the limit is the issuer's available
     *  liquidity.  For an issuer source that is the last step, the
     *  `MaximumAmount` ceiling is returned and the preceding step is trusted to
     *  constrain the flow; this allows `OutstandingAmount` to temporarily
     *  overflow within a pass without blocking progress.
     *
     *  @param sb  Read-only ledger view.
     *  @return  `{maxFlow, srcDebtDir}` where `srcDebtDir` is `Redeems` for a
     *      holder source and `Issues` for an issuer source.
     */
    [[nodiscard]] std::pair<MPTAmount, DebtDirection>
    maxPaymentFlow(ReadView const& sb) const;

    /** Compute `{srcQOut, dstQIn}` when the source is redeeming.
     *
     *  MPT has no per-trustline quality field, so `dstQIn` is always
     *  `QUALITY_ONE`. `srcQOut` takes the maximum of `QUALITY_ONE` and the
     *  previous step's `lineQualityIn`, which mirrors the IOU convention.
     *
     *  @param sb  Read-only ledger view.
     *  @return  `{srcQOut, QUALITY_ONE}`.
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcRedeems(ReadView const& sb) const;

    /** Compute `{srcQOut, dstQIn}` when the source is issuing.
     *
     *  A transfer rate is applied to `srcQOut` only when the previous step is
     *  redeeming; when it is issuing (always the case during offer crossing),
     *  `srcQOut` is `QUALITY_ONE`.  `dstQIn` is always `QUALITY_ONE` because
     *  MPT has no per-trustline quality field.
     *
     *  @param sb                    Read-only ledger view.
     *  @param prevStepDebtDirection Debt direction of the immediately preceding
     *      step; `Issues` when there is no previous step.
     *  @return  `{srcQOut, QUALITY_ONE}`.
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcIssues(ReadView const& sb, DebtDirection prevStepDebtDirection) const;

    /** Dispatch to `qualitiesSrcRedeems` or `qualitiesSrcIssues`.
     *
     *  @param sb          Read-only ledger view.
     *  @param srcDebtDir  Whether the source is redeeming or issuing.
     *  @param strandDir   Pass direction; used to query the previous step's
     *      cached debt direction during the forward pass.
     *  @return  `{srcQOut, dstQIn}`.
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualities(ReadView const& sb, DebtDirection srcDebtDir, StrandDirection strandDir) const;

    /** Set `cache_` to zero amounts, preserving the given debt direction. */
    void
    resetCache(DebtDirection dir);

private:
    /** Construct from strand-build context.
     *
     *  Pre-computes `isDirectBetweenHolders_` from the strand topology so that
     *  `check()` and the pass methods do not need to recompute it.  Asserts
     *  that exactly one of `src` and `dst` is the MPT issuer.
     *
     *  @param ctx  Strand-build context providing topology flags, the previous
     *      step pointer, and the journal.
     *  @param src  Source account for this step.
     *  @param dst  Destination account for this step.
     *  @param mpt  The MPTID identifying the token being transferred.
     */
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
              mptIssue_ == ctx.strandDeliver && ctx.strandSrc != mptIssue_.getIssuer() &&
              ctx.strandDst != mptIssue_.getIssuer() &&
              (ctx.isFirst || (ctx.prevStep != nullptr && !ctx.prevStep->bookStepBook())))
        , j_(ctx.j)
    {
        XRPL_ASSERT(
            src_ == mptIssue_.getIssuer() || dst_ == mptIssue_.getIssuer(),
            "MPTEndpointStep::MPTEndpointStep src or dst must be an issuer");
    }

public:
    /** @return The source account for this step. */
    [[nodiscard]] AccountID const&
    src() const
    {
        return src_;
    }
    /** @return The destination account for this step. */
    [[nodiscard]] AccountID const&
    dst() const
    {
        return dst_;
    }
    /** @return The MPTID of the token being transferred. */
    [[nodiscard]] MPTID const&
    mptID() const
    {
        return mptIssue_.getMptID();
    }

    /** @return The cached input amount from the most recent pass, or
     *      `std::nullopt` if no pass has been executed yet.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedIn() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->in);
    }

    /** @return The cached output amount from the most recent pass, or
     *      `std::nullopt` if no pass has been executed yet.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedOut() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->out);
    }

    /** @return `src_`, identifying this as a direct (non-book) step. */
    [[nodiscard]] std::optional<AccountID>
    directStepSrcAcct() const override
    {
        return src_;
    }

    /** @return `{src_, dst_}` as the two accounts involved in this step. */
    [[nodiscard]] std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const override
    {
        return std::make_pair(src_, dst_);
    }

    /** Return the debt direction of the source account.
     *
     *  During the forward pass, returns the direction cached from `revImp`
     *  when available.  Otherwise derives it statically: `Issues` when
     *  `src_` is the issuer, `Redeems` when `src_` is a holder.
     *
     *  @param sb   Read-only ledger view.
     *  @param dir  Pass direction.
     *  @return  `DebtDirection::Issues` or `DebtDirection::Redeems`.
     */
    [[nodiscard]] DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override;

    /** Return the destination quality-in for this step.
     *
     *  MPT has no per-trustline quality field, so this always returns
     *  `QUALITY_ONE`.
     *
     *  @param v  Read-only ledger view (unused).
     *  @return   `QUALITY_ONE`.
     */
    [[nodiscard]] std::uint32_t
    lineQualityIn(ReadView const& v) const override;

    /** Compute an upper bound on the effective quality for strand sorting.
     *
     *  Calls `qualitiesSrcRedeems` or `qualitiesSrcIssues` depending on the
     *  current debt direction, then builds a `Quality` from the ratio
     *  `srcQOut / dstQIn` (see in-code comment for parameter order).
     *
     *  @param v           Read-only ledger view.
     *  @param prevStepDir Debt direction of the preceding step.
     *  @return  `{Quality upper bound, debt direction}`.
     */
    [[nodiscard]] std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection dir) const override;

    /** Execute the reverse pass: determine required input for a desired output.
     *
     *  Calls `maxPaymentFlow` and `qualities`, then either caps the flow at
     *  `maxSrcToDst` (becoming the limiting node) or passes the requested
     *  `out` through unchanged.  The result is written to `cache_` and the
     *  transfer is journalled via `directSendNoFee` on the sandbox.
     *
     *  @param sb        Payment sandbox (mutable ledger view).
     *  @param afView    Apply view (unused — placeholder for CRTP interface).
     *  @param ofrsToRm  Offer-removal set (unused for MPT endpoint steps).
     *  @param out       Desired output amount.
     *  @return  `{actualIn, actualOut}`.
     */
    std::pair<MPTAmount, MPTAmount>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        MPTAmount const& out);

    /** Execute the forward pass: compute actual output for a given input.
     *
     *  Calls `maxPaymentFlow` and `qualities`, applies `setCacheLimiting` to
     *  prevent rounding from exceeding reverse-pass liquidity, and journals the
     *  transfer via `directSendNoFee`.
     *
     *  @param sb        Payment sandbox (mutable ledger view).
     *  @param afView    Apply view (unused — placeholder for CRTP interface).
     *  @param ofrsToRm  Offer-removal set (unused for MPT endpoint steps).
     *  @param in        Available input amount.
     *  @return  `{actualIn, actualOut}` taken from `cache_` after limiting.
     */
    std::pair<MPTAmount, MPTAmount>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        MPTAmount const& in);

    /** Validate that the forward pass is reproducible with the cached input.
     *
     *  Saves the current cache, re-executes `fwdImp` with `in`, then checks
     *  that the new cache values are within `checkNear` of the saved values.
     *  A `FlowException` during the re-execution is treated as a failure.
     *  This is the payment engine's guard against strands that behave
     *  non-deterministically.
     *
     *  @param sb     Payment sandbox.
     *  @param afView Apply view.
     *  @param in     Input amount used during the forward pass.
     *  @return  `{valid, cachedOut}`.
     */
    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) override;

    /** Perform structural validation common to payments and offer crossing.
     *
     *  Checks zero/equal accounts, source-account existence, frozen status,
     *  path-loop detection via `seenBookOuts` and `seenDirectAssets`, and the
     *  constraint that MPT may only appear as the first or last step of a
     *  strand.  On success, delegates to the derived `check(ctx, sleSrc)` for
     *  context-specific rules.
     *
     *  @param ctx  Strand-build context.
     *  @return  `tesSUCCESS` or an appropriate error code.
     */
    [[nodiscard]] TER
    check(StrandContext const& ctx) const;

    /** Cap forward-pass results against the reverse-pass cache.
     *
     *  If the forward pass computes the same or less input than the cache, the
     *  cache is updated directly.  A difference of exactly 1 unit is silently
     *  corrected.  A larger difference within 1% is clamped to the cached
     *  value.  A difference exceeding 1% is accepted but logged at `warn` level
     *  so it can be investigated without blocking the transaction.
     *
     *  @param fwdIn        Forward-pass input.
     *  @param fwdSrcToDst  Forward-pass intermediate amount.
     *  @param fwdOut       Forward-pass output.
     *  @param srcDebtDir   Debt direction of the source in this pass.
     *
     *  @note `cache_` must be populated (set by `revImp`) before this is
     *      called; the NOLINTBEGIN comment in the implementation documents that
     *      invariant.
     */
    void
    setCacheLimiting(
        MPTAmount const& fwdIn,
        MPTAmount const& fwdSrcToDst,
        MPTAmount const& fwdOut,
        DebtDirection srcDebtDir);

    /** @return True if both steps have the same `src_`, `dst_`, and `mptIssue_`. */
    friend bool
    operator==(MPTEndpointStep const& lhs, MPTEndpointStep const& rhs)
    {
        return lhs.src_ == rhs.src_ && lhs.dst_ == rhs.dst_ && lhs.mptIssue_ == rhs.mptIssue_;
    }

    /** @return True if the two steps are not equal. */
    friend bool
    operator!=(MPTEndpointStep const& lhs, MPTEndpointStep const& rhs)
    {
        return !(lhs == rhs);
    }

protected:
    /** Format a human-readable description of this step for logging.
     *
     *  @param name  Class name prefix string supplied by the derived type.
     *  @return  Multi-line string of the form `"<name>:\nSrc: <src>\nDst: <dst>"`.
     */
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
        if (auto ds = dynamic_cast<MPTEndpointStep const*>(&rhs))
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

/** Concrete MPT endpoint step for normal (non-offer-crossing) payments.
 *
 *  Applies the full MPT authorization, frozen, `canTransfer`, and `canTrade`
 *  rule set during `check()`.  `checkCreateMPT` is a no-op because the
 *  destination's `MPToken` object must already exist for a payment; holders
 *  cannot receive MPT without first holding it.
 */
class MPTEndpointPaymentStep : public MPTEndpointStep<MPTEndpointPaymentStep>
{
public:
    using MPTEndpointStep<MPTEndpointPaymentStep>::MPTEndpointStep;
    using MPTEndpointStep<MPTEndpointPaymentStep>::check;

    MPTEndpointPaymentStep(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        MPTID const& mpt)
        : MPTEndpointStep<MPTEndpointPaymentStep>(ctx, src, dst, mpt)
    {
    }

    static bool
    verifyPrevStepDebtDirection(DebtDirection)
    {
        // A payment doesn't care regardless of prevStepRedeems.
        return true;
    }

    /** Apply payment-specific validation after the shared structural checks.
     *
     *  Enforces authorization (`requireAuth`), frozen status, `canTransfer`
     *  for direct holder-to-holder payments, and `canTrade` for cross-token
     *  payments routed through the DEX.  Also short-circuits with
     *  `tecPATH_DRY` when this is the first step and the source holds no
     *  balance.
     *
     *  @param ctx     Strand-build context.
     *  @param sleSrc  Source account SLE (pre-read by the base `check`).
     *  @return  `tesSUCCESS` or an appropriate error code.
     */
    [[nodiscard]] TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc) const;

    /** @return Log string prefixed with `"MPTEndpointPaymentStep"`. */
    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("MPTEndpointPaymentStep");
    }

    /** No-op: MPToken creation is not applicable for payments.
     *
     *  @return Always `tesSUCCESS`.
     */
    static TER
    checkCreateMPT(ApplyView&, DebtDirection)
    {
        return tesSUCCESS;
    }
};

/** Concrete MPT endpoint step for DEX offer crossing.
 *
 *  Defers all MPT authorization and frozen checks until `checkCreateMPT`,
 *  so `check()` unconditionally returns `tesSUCCESS`.  This is safe because
 *  the book step that precedes this step always issues, and the checks are
 *  performed atomically at crossing time rather than at strand-build time.
 *
 *  @note During offer crossing, `BookStep::debtDirection()` always returns
 *      `Issues`; `verifyPrevStepDebtDirection` asserts this invariant so
 *      callers are warned if that assumption ever changes.
 */
class MPTEndpointOfferCrossingStep : public MPTEndpointStep<MPTEndpointOfferCrossingStep>
{
public:
    using MPTEndpointStep<MPTEndpointOfferCrossingStep>::MPTEndpointStep;
    using MPTEndpointStep<MPTEndpointOfferCrossingStep>::check;

    MPTEndpointOfferCrossingStep(
        StrandContext const& ctx,
        AccountID const& src,
        AccountID const& dst,
        MPTID const& mpt)
        : MPTEndpointStep<MPTEndpointOfferCrossingStep>(ctx, src, dst, mpt)
    {
    }

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

    /** No-op for offer crossing: authorization checks are deferred to
     *  `checkCreateMPT` and executed atomically at crossing time.
     *
     *  @return Always `tesSUCCESS`.
     */
    static TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc);

    /** @return Log string prefixed with `"MPTEndpointOfferCrossingStep"`. */
    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("MPTEndpointOfferCrossingStep");
    }

    /** Lazily create the offer owner's `MPToken` object during crossing.
     *
     *  Called from both `revImp` and `fwdImp` (when this is the limiting
     *  step).  If `isLast_` is true (i.e. this step represents TakerPays),
     *  calls `xrpl::checkCreateMPT` to create the object for `dst_` if it
     *  does not yet exist.  The reserve check is intentionally waived because
     *  the offer never goes on the books when it crosses — `CreateOffer::applyGuts()`
     *  handles reserve separately.
     *
     *  @param view        Mutable apply view for ledger object creation.
     *  @param srcDebtDir  Debt direction passed to `resetCache` on failure.
     *  @return  `tesSUCCESS`, or the error from `xrpl::checkCreateMPT`.
     */
    TER
    checkCreateMPT(ApplyView& view, DebtDirection srcDebtDir);
};

//------------------------------------------------------------------------------

TER
MPTEndpointPaymentStep::check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc)
    const
{
    // Since this is a payment, MPToken must be present.  Perform all
    // MPToken related checks.

    // requireAuth checks if MPTIssuance exist. Note that issuer to issuer
    // payment is invalid
    auto const& issuer = mptIssue_.getIssuer();
    if (src_ != issuer)
    {
        if (auto const ter = requireAuth(ctx.view, mptIssue_, src_); !isTesSuccess(ter))
            return ter;
    }

    if (dst_ != issuer)
    {
        if (auto const ter = requireAuth(ctx.view, mptIssue_, dst_); !isTesSuccess(ter))
            return ter;
    }

    // Direct MPT payment, no DEX
    if (mptIssue_ == ctx.strandDeliver &&
        (ctx.isFirst || (ctx.prevStep != nullptr && !ctx.prevStep->bookStepBook())))
    {
        // Between holders
        if (isDirectBetweenHolders_)
        {
            auto const& holder = ctx.isFirst ? src_ : dst_;
            // Payment between the holders
            if (isFrozen(ctx.view, holder, mptIssue_))
                return tecLOCKED;

            if (auto const ter = canTransfer(ctx.view, mptIssue_, holder, ctx.strandDst);
                !isTesSuccess(ter))
                return ter;
        }
        // Don't need to check if a payment is between issuer and holder
        // in either direction
    }
    // Cross-token MPT payment via DEX
    else
    {
        if (auto const ter = canTrade(ctx.view, mptIssue_); !isTesSuccess(ter))
            return ter;
    }

    // Can't check for creditBalance/Limit unless it's the first step.
    // Otherwise, even if OutstandingAmount is equal to MaximumAmount
    // a payment can still be successful. For instance, when a balance
    // is shifted from one holder to another.

    if (prevStep_ == nullptr)
    {
        auto const owed = accountFunds(
            ctx.view, src_, mptIssue_, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_);
        // Already at MaximumAmount
        if (owed <= beast::kZERO)
            return tecPATH_DRY;
    }

    return tesSUCCESS;
}

TER
MPTEndpointOfferCrossingStep::check(StrandContext const& ctx, std::shared_ptr<const SLE> const&)
{
    return tesSUCCESS;
}

TER
MPTEndpointOfferCrossingStep::checkCreateMPT(ApplyView& view, xrpl::DebtDirection srcDebtDir)
{
    // TakerPays is the last step if offer crossing
    if (isLast_)
    {
        // Create MPToken for the offer's owner. No need to check
        // for the reserve since the offer doesn't go on the books
        // if crossed. Insufficient reserve is allowed if the offer
        // crossed. See CreateOffer::applyGuts() for reserve check.
        if (auto const err = xrpl::checkCreateMPT(view, mptIssue_, dst_, j_); !isTesSuccess(err))
        {
            JLOG(j_.trace()) << "MPTEndpointStep::checkCreateMPT: failed create MPT";
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
    auto const maxFlow = accountFunds(
        sb, src_, mptIssue_, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_);

    // From a holder to an issuer
    if (src_ != mptIssue_.getIssuer())
        return {toAmount<MPTAmount>(maxFlow), DebtDirection::Redeems};

    // From an issuer to a holder
    if (auto const sle = sb.read(keylet::mptIssuance(mptIssue_)))
    {
        // If issuer is the source account, and it is direct payment then
        // MPTEndpointStep is the only step. Provide available maxFlow.
        if (prevStep_ == nullptr)
            return {toAmount<MPTAmount>(maxFlow), DebtDirection::Issues};

        // MPTEndpointStep is the last step. It's always issuing in
        // this case. Can't infer at this point what the maxFlow is, because
        // the previous step may issue or redeem. Allow OutstandingAmount
        // to temporarily overflow. Let the previous step figure out how
        // to limit the flow.
        std::int64_t const maxAmount = maxMPTAmount(*sle);
        return {MPTAmount{maxAmount}, DebtDirection::Issues};
    }

    return {MPTAmount{0}, DebtDirection::Issues};
}

template <class TDerived>
DebtDirection
MPTEndpointStep<TDerived>::debtDirection(ReadView const& sb, StrandDirection dir) const
{
    if (dir == StrandDirection::Forward && cache_)
        return cache_->srcDebtDir;

    return (src_ == mptIssue_.getIssuer()) ? DebtDirection::Issues : DebtDirection::Redeems;
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

    auto const [maxSrcToDst, srcDebtDir] = static_cast<TDerived const*>(this)->maxPaymentFlow(sb);

    auto const [srcQOut, dstQIn] = qualities(sb, srcDebtDir, StrandDirection::Reverse);
    (void)dstQIn;

    MPTIssue const srcToDstIss(mptIssue_);

    JLOG(j_.trace()) << "MPTEndpointStep::rev"
                     << " srcRedeems: " << redeems(srcDebtDir) << " outReq: " << to_string(out)
                     << " maxSrcToDst: " << to_string(maxSrcToDst) << " srcQOut: " << srcQOut
                     << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "MPTEndpointStep::rev: dry";
        resetCache(srcDebtDir);
        return {beast::kZERO, beast::kZERO};
    }

    if (auto const err = static_cast<TDerived*>(this)->checkCreateMPT(sb, srcDebtDir);
        !isTesSuccess(err))
        return {beast::kZERO, beast::kZERO};

    // Don't have to factor in dstQIn since it is always QUALITY_ONE
    MPTAmount const srcToDst = out;

    if (srcToDst <= maxSrcToDst)
    {
        MPTAmount const in = mulRatio(srcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        cache_.emplace(in, srcToDst, srcToDst, srcDebtDir);
        auto const ter = directSendNoFee(
            sb,
            src_,
            dst_,
            toSTAmount(srcToDst, srcToDstIss),
            /*checkIssuer*/ false,
            j_);
        if (!isTesSuccess(ter))
        {
            JLOG(j_.trace()) << "MPTEndpointStep::rev: error " << ter;
            resetCache(srcDebtDir);
            return {beast::kZERO, beast::kZERO};
        }
        JLOG(j_.trace()) << "MPTEndpointStep::rev: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst) << " out: " << to_string(out);
        return {in, out};
    }

    // limiting node
    MPTAmount const in = mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
    // Don't have to factor in dsqQIn since it's always QUALITY_ONE
    MPTAmount const actualOut = maxSrcToDst;
    cache_.emplace(in, maxSrcToDst, actualOut, srcDebtDir);

    auto const ter = directSendNoFee(
        sb,
        src_,
        dst_,
        toSTAmount(maxSrcToDst, srcToDstIss),
        /*checkIssuer*/ false,
        j_);
    if (!isTesSuccess(ter))
    {
        JLOG(j_.trace()) << "MPTEndpointStep::rev: error " << ter;
        resetCache(srcDebtDir);
        return {beast::kZERO, beast::kZERO};
    }
    JLOG(j_.trace()) << "MPTEndpointStep::rev: Limiting"
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
MPTEndpointStep<TDerived>::setCacheLimiting(
    MPTAmount const& fwdIn,
    MPTAmount const& fwdSrcToDst,
    MPTAmount const& fwdOut,
    DebtDirection srcDebtDir)
{
    // NOLINTBEGIN(bugprone-unchecked-optional-access) cache_ always set before setCacheLimiting is
    // called
    if (cache_->in < fwdIn)
    {
        MPTAmount const smallDiff(1);
        auto const diff = fwdIn - cache_->in;
        if (diff > smallDiff)
        {
            if (!cache_->in.value() ||
                (Number(fwdIn.value()) / Number(cache_->in.value())) > Number(101, -2))
            {
                // Detect large diffs on forward pass so they may be
                // investigated
                JLOG(j_.warn()) << "MPTEndpointStep::fwd: setCacheLimiting"
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
std::pair<MPTAmount, MPTAmount>
MPTEndpointStep<TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& /*afView*/,
    boost::container::flat_set<uint256>& /*ofrsToRm*/,
    MPTAmount const& in)
{
    XRPL_ASSERT(cache_, "MPTEndpointStep<TDerived>::fwdImp : valid cache");
    // NOLINTBEGIN(bugprone-unchecked-optional-access) assert above

    auto const [maxSrcToDst, srcDebtDir] = static_cast<TDerived const*>(this)->maxPaymentFlow(sb);

    auto const [srcQOut, dstQIn] = qualities(sb, srcDebtDir, StrandDirection::Forward);
    (void)dstQIn;

    MPTIssue const srcToDstIss(mptIssue_);

    JLOG(j_.trace()) << "MPTEndpointStep::fwd"
                     << " srcRedeems: " << redeems(srcDebtDir) << " inReq: " << to_string(in)
                     << " maxSrcToDst: " << to_string(maxSrcToDst) << " srcQOut: " << srcQOut
                     << " dstQIn: " << dstQIn;

    if (maxSrcToDst.signum() <= 0)
    {
        JLOG(j_.trace()) << "MPTEndpointStep::fwd: dry";
        resetCache(srcDebtDir);
        return {beast::kZERO, beast::kZERO};
    }

    if (auto const err = static_cast<TDerived*>(this)->checkCreateMPT(sb, srcDebtDir);
        !isTesSuccess(err))
        return {beast::kZERO, beast::kZERO};

    MPTAmount const srcToDst = mulRatio(in, QUALITY_ONE, srcQOut, /*roundUp*/ false);

    if (srcToDst <= maxSrcToDst)
    {
        // Don't have to factor in dstQIn since it's always QUALITY_ONE
        MPTAmount const out = srcToDst;
        setCacheLimiting(in, srcToDst, out, srcDebtDir);
        auto const ter = directSendNoFee(
            sb,
            src_,
            dst_,
            toSTAmount(cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ false,
            j_);
        if (!isTesSuccess(ter))
        {
            JLOG(j_.trace()) << "MPTEndpointStep::fwd: error " << ter;
            resetCache(srcDebtDir);
            return {beast::kZERO, beast::kZERO};
        }
        JLOG(j_.trace()) << "MPTEndpointStep::fwd: Non-limiting"
                         << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(in)
                         << " srcToDst: " << to_string(srcToDst) << " out: " << to_string(out);
    }
    else
    {
        // limiting node
        MPTAmount const actualIn = mulRatio(maxSrcToDst, srcQOut, QUALITY_ONE, /*roundUp*/ true);
        // Don't have to factor in dstQIn since it's always QUALITY_ONE
        MPTAmount const out = maxSrcToDst;
        setCacheLimiting(actualIn, maxSrcToDst, out, srcDebtDir);
        auto const ter = directSendNoFee(
            sb,
            src_,
            dst_,
            toSTAmount(cache_->srcToDst, srcToDstIss),
            /*checkIssuer*/ false,
            j_);
        if (!isTesSuccess(ter))
        {
            JLOG(j_.trace()) << "MPTEndpointStep::fwd: error " << ter;
            resetCache(srcDebtDir);
            return {beast::kZERO, beast::kZERO};
        }
        JLOG(j_.trace()) << "MPTEndpointStep::fwd: Limiting"
                         << " srcRedeems: " << redeems(srcDebtDir) << " in: " << to_string(actualIn)
                         << " srcToDst: " << to_string(srcToDst) << " out: " << to_string(out);
    }
    return {cache_->in, cache_->out};
    // NOLINTEND(bugprone-unchecked-optional-access)
}

template <class TDerived>
std::pair<bool, EitherAmount>
MPTEndpointStep<TDerived>::validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG(j_.trace()) << "Expected valid cache in validFwd";
        return {false, EitherAmount(MPTAmount(beast::kZERO))};
    }

    auto const savCache = *cache_;

    XRPL_ASSERT(in.holds<MPTAmount>(), "MPTEndpoint<TDerived>::validFwd : is MPT");

    auto const [maxSrcToDst, srcDebtDir] = static_cast<TDerived const*>(this)->maxPaymentFlow(sb);
    (void)srcDebtDir;

    try
    {
        boost::container::flat_set<uint256> dummy;
        fwdImp(sb, afView, dummy, in.get<MPTAmount>());  // changes cache
    }
    catch (FlowException const&)
    {
        return {false, EitherAmount(MPTAmount(beast::kZERO))};
    }

    // NOLINTBEGIN(bugprone-unchecked-optional-access) fwdImp sets cache_ on success
    if (maxSrcToDst < cache_->srcToDst)
    {
        JLOG(j_.warn()) << "MPTEndpointStep: Strand re-execute check failed."
                        << " Exceeded max src->dst limit"
                        << " max src->dst: " << to_string(maxSrcToDst)
                        << " actual src->dst: " << to_string(cache_->srcToDst);
        return {false, EitherAmount(cache_->out)};
    }

    if (!(checkNear(savCache.in, cache_->in) && checkNear(savCache.out, cache_->out)))
    {
        JLOG(j_.warn()) << "MPTEndpointStep: Strand re-execute check failed."
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
MPTEndpointStep<TDerived>::qualitiesSrcRedeems(ReadView const& sb) const
{
    if (prevStep_ == nullptr)
        return {QUALITY_ONE, QUALITY_ONE};

    auto const prevStepQIn = prevStep_->lineQualityIn(sb);
    // Unlike trustline MPT doesn't have line quality field
    auto srcQOut = QUALITY_ONE;

    srcQOut = std::max<std::uint32_t>(prevStepQIn, srcQOut);
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
        static_cast<TDerived const*>(this)->verifyPrevStepDebtDirection(prevStepDebtDirection),
        "MPTEndpointStep<TDerived>::qualitiesSrcIssues : verify prev step debt "
        "direction");

    std::uint32_t const srcQOut =
        redeems(prevStepDebtDirection) ? transferRate(sb, mptIssue_.getMptID()).value : QUALITY_ONE;

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

    auto const prevStepDebtDirection = [&] {
        if (prevStep_ != nullptr)
            return prevStep_->debtDirection(sb, strandDir);
        return DebtDirection::Issues;
    }();
    return qualitiesSrcIssues(sb, prevStepDebtDirection);
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
MPTEndpointStep<TDerived>::qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const
{
    auto const dir = this->debtDirection(v, StrandDirection::Forward);

    auto const [srcQOut, dstQIn] =
        redeems(dir) ? qualitiesSrcRedeems(v) : qualitiesSrcIssues(v, prevStepDir);
    (void)dstQIn;

    MPTIssue const iss{mptIssue_};
    // Be careful not to switch the parameters to `getRate`. The
    // `getRate(offerOut, offerIn)` function is usually used for offers. It
    // returns offerIn/offerOut. For a direct step, the rate is srcQOut/dstQIn
    // (Input*dstQIn/srcQOut = Output; So rate = srcQOut/dstQIn). Although the
    // first parameter is called `offerOut`, it should take the `dstQIn`
    // variable.
    return {Quality(getRate(STAmount(iss, QUALITY_ONE), STAmount(iss, srcQOut))), dir};
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
        JLOG(j_.warn()) << "MPTEndpointStep: can't receive MPT from non-existent issuer: " << src_;
        return terNO_ACCOUNT;
    }

    // pure issue/redeem can't be frozen (issuer/holder)
    if (!(ctx.isLast && ctx.isFirst))
    {
        auto const& account = ctx.isFirst ? src_ : dst_;
        if (isFrozen(ctx.view, account, mptIssue_))
            return terLOCKED;
    }

    if (ctx.seenBookOuts.count(mptIssue_) > 0)
    {
        if (ctx.prevStep == nullptr)
        {
            UNREACHABLE(
                "xrpl::MPTEndpointStep::check : prev seen book without a "
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
        JLOG(j_.debug()) << "MPTEndpointStep: loop detected: Index: " << ctx.strandSize << ' '
                         << *this;
        return temBAD_PATH_LOOP;
    }

    // MPT can only be an endpoint
    if (!ctx.isLast && !ctx.isFirst)
    {
        JLOG(j_.warn()) << "MPTEndpointStep: MPT can only be an endpoint";
        return temBAD_PATH;
    }

    auto const& issuer = mptIssue_.getIssuer();
    if ((src_ != issuer && dst_ != issuer) || (src_ == issuer && dst_ == issuer))
    {
        JLOG(j_.warn()) << "MPTEndpointStep: invalid src/dst";
        return temBAD_PATH;
    }

    return static_cast<TDerived const*>(this)->check(ctx, sleSrc);
}

template <class TDerived>
void
MPTEndpointStep<TDerived>::resetCache(xrpl::DebtDirection dir)
{
    cache_.emplace(MPTAmount(beast::kZERO), MPTAmount(beast::kZERO), MPTAmount(beast::kZERO), dir);
}

//------------------------------------------------------------------------------

/** Factory that constructs the appropriate MPT endpoint step.
 *
 *  Dispatches on `ctx.offerCrossing` to instantiate either
 *  `MPTEndpointOfferCrossingStep` or `MPTEndpointPaymentStep`, immediately
 *  runs `check()` on the new instance, and returns a null pointer if any
 *  check fails.
 *
 *  @param ctx  Strand-build context (provides `offerCrossing`, topology flags,
 *      the ledger view, and the journal).
 *  @param src  Source account.
 *  @param dst  Destination account.
 *  @param mpt  MPTID of the token being transferred.
 *  @return  `{tesSUCCESS, step}` on success, or `{ter, nullptr}` on failure.
 */
std::pair<TER, std::unique_ptr<Step>>
makeMptEndpointStep(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    MPTID const& mpt)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing != OfferCrossing::No)
    {
        auto offerCrossingStep = std::make_unique<MPTEndpointOfferCrossingStep>(ctx, src, dst, mpt);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep = std::make_unique<MPTEndpointPaymentStep>(ctx, src, dst, mpt);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (!isTesSuccess(ter))
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

namespace test {

/** Test utility: structurally compare a `Step` to an expected MPT endpoint.
 *
 *  Downcasts `step` to `MPTEndpointStep<MPTEndpointPaymentStep>` and checks
 *  that `src`, `dst`, and `mptid` match.  Returns `false` for offer-crossing
 *  steps and non-MPT steps, which is sufficient for strand-composition tests
 *  that always construct payment steps.
 *
 *  @param step    Step to inspect.
 *  @param src     Expected source account.
 *  @param dst     Expected destination account.
 *  @param mptid   Expected MPTID.
 *  @return  True if `step` is a payment MPT endpoint step with the given
 *      accounts and token.
 */
bool
mptEndpointStepEqual(
    Step const& step,
    AccountID const& src,
    AccountID const& dst,
    MPTID const& mptid)
{
    if (auto ds = dynamic_cast<MPTEndpointStep<MPTEndpointPaymentStep> const*>(&step))
    {
        return ds->src() == src && ds->dst() == dst && ds->mptID() == mptid;
    }
    return false;
}
}  // namespace test

}  // namespace xrpl
