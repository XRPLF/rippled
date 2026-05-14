/**
 * @file DirectStep.cpp
 * @brief IOU-to-IOU direct transfer step in the payment path engine.
 *
 * Implements `DirectStepI`, the strand `Step` that moves same-currency IOU
 * value directly between two accounts over their shared trust line — the
 * fundamental *rippling* operation of the XRP Ledger. Two concrete subtypes
 * are defined here: `DirectIPaymentStep` (ordinary payments, quality fields
 * and auth rules enforced) and `DirectIOfferCrossingStep` (offer crossing,
 * trust-line limits relaxed on the final step, quality fields ignored).
 */
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
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

/**
 * CRTP base for the IOU direct-transfer step.
 *
 * Holds the common state and algorithm for moving IOU value between `src_`
 * and `dst_` over a single trust line. The two template specialisations
 * — `DirectIPaymentStep` and `DirectIOfferCrossingStep` — provide
 * context-specific policy via the CRTP hooks `maxFlow()`, `quality()`,
 * `verifyPrevStepDebtDirection()`, `verifyDstQualityIn()`, and `check()`.
 *
 * @tparam TDerived The concrete subtype; must expose the CRTP hook methods.
 */
template <class TDerived>
class DirectStepI : public StepImp<IOUAmount, IOUAmount, DirectStepI<TDerived>>
{
protected:
    AccountID src_;      ///< Sending account for this step.
    AccountID dst_;      ///< Receiving account for this step.
    Currency currency_;  ///< IOU currency transferred on this step.

    /// Pointer to the immediately preceding `Step`, used to determine whether
    /// a transfer fee applies (fee is charged when the previous step redeems
    /// and this step issues). Null when this is the first step of the strand.
    Step const* const prevStep_ = nullptr;
    bool const isLast_;      ///< True when this is the final step of the strand.
    beast::Journal const j_; ///< Logger.

    /**
     * Per-pass result cache populated by `revImp` and updated by `fwdImp`.
     *
     * Stores the amounts and debt direction computed during the reverse pass so
     * that `fwdImp` and `validFwd` can reference them without re-reading the
     * ledger. `setCacheLimiting` reconciles forward-pass rounding against the
     * cached values to ensure the forward pass never authorises more liquidity
     * than the reverse pass established.
     */
    struct Cache
    {
        IOUAmount in;            ///< Amount entering this step (sender perspective).
        IOUAmount srcToDst;      ///< Amount moving on the trust line (may differ from `out` when `dstQIn` != QUALITY_ONE).
        IOUAmount out;           ///< Amount leaving this step (receiver perspective).
        DebtDirection srcDebtDir; ///< Whether `src_` redeems or issues on this step.

        Cache(
            IOUAmount const& in,
            IOUAmount const& srcToDst,
            IOUAmount const& out,
            DebtDirection srcDebtDir)
            : in(in), srcToDst(srcToDst), out(out), srcDebtDir(srcDebtDir)
        {
        }
    };

    std::optional<Cache> cache_; ///< Set after `revImp`; cleared at the start of each reverse pass.

    /**
     * Maximum IOU flow available for an ordinary payment.
     *
     * Calls `accountHolds` to read `src_`'s balance against `dst_`.
     * A positive balance means `src_` redeems (returns IOUs to the issuer)
     * and may send up to that balance. A zero or negative balance means
     * `src_` issues and may send up to the remaining trust-line headroom.
     *
     * @param sb Read view of current ledger state.
     * @return `{maxFlow, debtDirection}` — the maximum amount that can flow
     *     and whether `src_` is redeeming or issuing.
     */
    [[nodiscard]] std::pair<IOUAmount, DebtDirection>
    maxPaymentFlow(ReadView const& sb) const;

    /**
     * Quality multipliers when `src_` redeems (sends IOUs back to issuer).
     *
     * No transfer fee applies on a redeem. `srcQOut` is taken as the maximum
     * of the trust-line's quality-out field and the previous step's
     * `lineQualityIn`, to honour the more conservative of the two rates.
     * `dstQIn` is always `QUALITY_ONE`.
     *
     * @param sb Read view of current ledger state.
     * @return `{srcQOut, dstQIn}` as raw `uint32_t` quality values.
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcRedeems(ReadView const& sb) const;

    /**
     * Quality multipliers when `src_` issues (creates new IOU obligations).
     *
     * A transfer fee (`srcQOut = transferRate(src_)`) applies when the
     * previous step redeems, signalling a redeem→issue transition. `dstQIn`
     * is the trust-line quality-in for the destination, capped at
     * `QUALITY_ONE` on the final step to avoid over-charging the recipient.
     *
     * @param sb Read view of current ledger state.
     * @param prevStepDebtDirection Debt direction of the preceding step,
     *     used to decide whether the transfer fee triggers.
     * @return `{srcQOut, dstQIn}` as raw `uint32_t` quality values.
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualitiesSrcIssues(ReadView const& sb, DebtDirection prevStepDebtDirection) const;

    /**
     * Dispatch to `qualitiesSrcRedeems` or `qualitiesSrcIssues`.
     *
     * Routes based on `srcDebtDir`. When issuing, the previous step's debt
     * direction is obtained from `prevStep_->debtDirection()` or defaulted
     * to `Issues` when there is no previous step.
     *
     * @param sb Read view of current ledger state.
     * @param srcDebtDir Whether `src_` redeems or issues on this pass.
     * @param strandDir Forward or reverse pass (used to read cached direction).
     * @return `{srcQOut, dstQIn}` as raw `uint32_t` quality values.
     */
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t>
    qualities(ReadView const& sb, DebtDirection srcDebtDir, StrandDirection strandDir) const;

private:
    /** Constructs the step from a strand-building context. Only accessible via `TDerived`. */
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
    /** Source account of this step. */
    [[nodiscard]] AccountID const&
    src() const
    {
        return src_;
    }
    /** Destination account of this step. */
    [[nodiscard]] AccountID const&
    dst() const
    {
        return dst_;
    }
    /** IOU currency transferred on this step. */
    [[nodiscard]] Currency const&
    currency() const
    {
        return currency_;
    }

    /**
     * Return the cached input amount from the most recent reverse pass, if any.
     * @return Wrapped `IOUAmount` or `std::nullopt` if no pass has run yet.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedIn() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->in);
    }

    /**
     * Return the cached output amount from the most recent reverse pass, if any.
     * @return Wrapped `IOUAmount` or `std::nullopt` if no pass has run yet.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedOut() const override
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(cache_->out);
    }

    /**
     * Return the source account for NoRipple / loop-detection queries by the
     * subsequent step.
     */
    [[nodiscard]] std::optional<AccountID>
    directStepSrcAcct() const override
    {
        return src_;
    }

    /** Return both accounts so callers can identify the trust line. */
    [[nodiscard]] std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const override
    {
        return std::make_pair(src_, dst_);
    }

    /**
     * Whether `src_` redeems or issues on this step.
     *
     * For the forward pass, reads from `cache_` when available to avoid a
     * redundant ledger lookup. For the reverse pass (or when no cache exists),
     * calls `accountHolds` to determine the sign of `src_`'s balance.
     *
     * @param sb Ledger view.
     * @param dir Forward or Reverse — selects cache vs. ledger read.
     */
    [[nodiscard]] DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override;

    /**
     * Destination trust-line quality-in, for use by the *previous* step
     * when computing its `srcQOut`.
     *
     * @param v Ledger view.
     * @return Raw quality-in value, or `QUALITY_ONE` when absent or zero.
     */
    [[nodiscard]] std::uint32_t
    lineQualityIn(ReadView const& v) const override;

    /**
     * Upper bound on the quality this step can provide, used by the
     * `ActiveStrands` priority queue to rank candidate strands.
     *
     * Computes `rate = srcQOut / dstQIn` via `getRate(dstQIn, srcQOut)`.
     * Note the argument order to `getRate` is intentionally reversed from the
     * typical offer usage — for a direct step the rate is `srcQOut/dstQIn`.
     *
     * @param v Ledger view.
     * @param prevStepDir Debt direction of the previous step, forwarded to
     *     `qualitiesSrcIssues` when this step issues.
     * @return `{qualityUpperBound, debtDirection}`.
     */
    [[nodiscard]] std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection dir) const override;

    /**
     * Reverse pass: compute how much input is needed to produce `out`.
     *
     * Calls the CRTP `maxFlow()` to get the liquidity ceiling, then applies
     * `qualities()` to obtain `srcQOut` and `dstQIn`. If the requested flow
     * fits within the ceiling, the exact amounts are computed and cached. If
     * not, the step becomes the limiting node and `actualOut` may be less than
     * `out`. In both cases `directSendNoFee` tentatively moves value in `sb`.
     *
     * @param sb Mutable sandbox; balances are updated.
     * @param afView Baseline view for unfunded-offer detection (unused here).
     * @param ofrsToRm Offers to remove (unused here).
     * @param out Requested output amount.
     * @return `{actualIn, actualOut}`.
     */
    std::pair<IOUAmount, IOUAmount>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        IOUAmount const& out);

    /**
     * Forward pass: compute the output for a given `in`.
     *
     * Uses `cache_->srcToDst` (set by the reverse pass) as the `desired`
     * parameter to `maxFlow()`. After computing forward amounts, calls
     * `setCacheLimiting` to reconcile rounding differences before calling
     * `directSendNoFee` with the cache-limited values.
     *
     * @param sb Mutable sandbox; balances are updated.
     * @param afView Baseline view (unused here).
     * @param ofrsToRm Offers to remove (unused here).
     * @param in Actual input available.
     * @return `{cache_->in, cache_->out}` after limiting.
     */
    std::pair<IOUAmount, IOUAmount>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        IOUAmount const& in);

    /**
     * Re-execute the forward pass and verify consistency with the cached result.
     *
     * Called by the strand runner to confirm that ledger state has not changed
     * enough to invalidate the forward pass. Fails if `maxSrcToDst` is
     * exceeded or if the re-computed `in`/`out` deviate beyond `checkNear`
     * tolerance from the cached values.
     *
     * @param sb Mutable sandbox.
     * @param afView Baseline view.
     * @param in Input amount to validate against.
     * @return `{valid, cachedOut}`.
     */
    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) override;

    /**
     * Validate this step against the strand-building context.
     *
     * Checks applicable to both payment and offer crossing: non-null/non-equal
     * accounts, source account existence, freeze constraints (skipped for
     * single-hop pure issue/redeem), NoRipple enforcement when the previous
     * step was also a direct step, and loop detection via `seenDirectAssets`.
     * Delegates to the CRTP `TDerived::check(ctx, sleSrc)` for
     * context-specific rules.
     *
     * @param ctx Strand-building context carrying the ledger view and flags.
     * @return `tesSUCCESS` or an appropriate error code.
     */
    [[nodiscard]] TER
    check(StrandContext const& ctx) const;

    /**
     * Reconcile forward-pass amounts against the cached reverse-pass values.
     *
     * Prevents the forward pass from authorising more liquidity than the
     * reverse pass established. Applies the minimum of forward and cached
     * values for `srcToDst` and `out`. If the discrepancy in `in` exceeds
     * 1% mantissa ratio, the function logs a warning and accepts the forward
     * values rather than silently clamping — prioritising visibility of
     * unexpected rounding behaviour.
     *
     * @param fwdIn      Input amount computed by the forward pass.
     * @param fwdSrcToDst Trust-line amount computed by the forward pass.
     * @param fwdOut     Output amount computed by the forward pass.
     * @param srcDebtDir Debt direction from the forward pass.
     */
    void
    setCacheLimiting(
        IOUAmount const& fwdIn,
        IOUAmount const& fwdSrcToDst,
        IOUAmount const& fwdOut,
        DebtDirection srcDebtDir);

    /** Steps are equal when src, dst, and currency all match. */
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
    /** Build a human-readable log string prefixed with the concrete class name. */
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

/**
 * Direct IOU transfer step for ordinary payment transactions.
 *
 * Enforces trust-line existence, `lsfRequireAuth`, NoRipple (when following a
 * book step), and dry-path detection. Reads quality-in/out fields from the
 * trust line and honours transfer fees on redeem→issue transitions.
 */
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

    /** Payments impose no constraint on the previous step's debt direction. */
    static bool
    verifyPrevStepDebtDirection(DebtDirection)
    {
        return true;
    }

    /** Payments impose no constraint on `dstQIn`. */
    static bool
    verifyDstQualityIn(std::uint32_t dstQIn)
    {
        return true;
    }

    /**
     * Read the trust-line quality field for the given direction.
     *
     * Reads `sfLowQualityIn`/`sfHighQualityIn` or `sfLowQualityOut`/
     * `sfHighQualityOut` based on account ordering. Returns `QUALITY_ONE`
     * when the field is absent, zero, or when `src_ == dst_`.
     *
     * @param sb Ledger view.
     * @param qDir `In` for destination quality-in; `Out` for source quality-out.
     * @return Raw quality value as `uint32_t`.
     */
    [[nodiscard]] std::uint32_t
    quality(ReadView const& sb, QualityDirection qDir) const;

    /**
     * Maximum IOU flow available for a payment (delegates to `maxPaymentFlow`).
     *
     * @param sb Ledger view.
     * @param desired Requested output (ignored; signature matches CRTP hook).
     * @return `{maxFlow, debtDirection}`.
     */
    [[nodiscard]] std::pair<IOUAmount, DebtDirection>
    maxFlow(ReadView const& sb, IOUAmount const& desired) const;

    /**
     * Payment-specific strand consistency checks.
     *
     * Requires a pre-existing trust line (`terNO_LINE`), verifies
     * `lsfRequireAuth` is satisfied, enforces NoRipple when the previous step
     * was a book step, and rejects a dry path (destination balance at limit).
     *
     * @param ctx Strand-building context.
     * @param sleSrc Source account SLE (already validated by base `check`).
     * @return `tesSUCCESS` or an error code.
     */
    [[nodiscard]] TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc) const;

    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("DirectIPaymentStep");
    }
};

/**
 * Direct IOU transfer step for offer-crossing strands.
 *
 * Relaxes several constraints that apply to payments:
 * - No trust-line existence requirement.
 * - Trust-line quality fields are ignored (returns `QUALITY_ONE` always).
 * - On the final step (`isLast_`), the trust-line limit may be exceeded so
 *   that the full desired amount can be delivered (posting an offer signals
 *   willingness to receive beyond the current limit).
 * - No NoRipple check, no auth check, no dry-path check.
 */
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

    /**
     * Assert that the previous step always issues during offer crossing.
     *
     * When present, `prevStep_` is always a `BookStep`, and
     * `BookStep::debtDirection()` always returns `Issues` during offer
     * crossing. This hook fires an assert if that structural invariant
     * ever changes.
     *
     * @return `true` iff `prevStepDir` is `Issues`.
     */
    static bool
    verifyPrevStepDebtDirection(DebtDirection prevStepDir)
    {
        return issues(prevStepDir);
    }

    /**
     * Assert that `dstQIn` is always `QUALITY_ONE` during offer crossing.
     *
     * Quality-in for the destination is always `QUALITY_ONE` for offer
     * crossing because quality fields are ignored. This hook fires an
     * assert if that ever changes.
     *
     * @return `true` iff `dstQIn == QUALITY_ONE`.
     */
    static bool
    verifyDstQualityIn(std::uint32_t dstQIn)
    {
        return dstQIn == QUALITY_ONE;
    }

    /**
     * Return `QUALITY_ONE` unconditionally.
     *
     * Trust-line quality fields (`sfLowQualityIn`, etc.) are irrelevant for
     * offer crossing — a long-standing protocol tradition.
     */
    static std::uint32_t
    quality(ReadView const& sb, QualityDirection qDir);

    /**
     * Maximum flow for offer crossing.
     *
     * On the final step (`isLast_`), returns `{desired, Issues}` directly,
     * bypassing trust-line limits. On non-final steps, delegates to
     * `maxPaymentFlow`.
     *
     * @param sb Ledger view.
     * @param desired Requested output; used as the ceiling on the last step.
     * @return `{maxFlow, debtDirection}`.
     */
    [[nodiscard]] std::pair<IOUAmount, DebtDirection>
    maxFlow(ReadView const& sb, IOUAmount const& desired) const;

    /**
     * Offer-crossing-specific strand consistency checks.
     *
     * The common checks in `DirectStepI::check` are sufficient; no
     * trust-line-dependent checks are needed here.
     *
     * @return Always `tesSUCCESS`.
     */
    static TER
    check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc);

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

    auto const sle = sb.read(keylet::line(dst_, src_, currency_));

    if (!sle)
        return QUALITY_ONE;

    auto const& field = [&, this]() -> SF_UINT32 const& {
        if (qDir == QualityDirection::In)
        {
            if (this->dst_ < this->src_)
            {
                return sfLowQualityIn;
            }

            return sfHighQualityIn;
        }

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
    return QUALITY_ONE;
}

std::pair<IOUAmount, DebtDirection>
DirectIPaymentStep::maxFlow(ReadView const& sb, IOUAmount const&) const
{
    return maxPaymentFlow(sb);
}

std::pair<IOUAmount, DebtDirection>
DirectIOfferCrossingStep::maxFlow(ReadView const& sb, IOUAmount const& desired) const
{
    // Using `desired` (== "out") directly as the max is safe here because
    // `dstQIn` is always QUALITY_ONE for offer crossing, so `maxSrcToDst`
    // never needs to exceed `out`.  For payments that invariant doesn't hold.
    if (isLast_)
        return {desired, DebtDirection::Issues};

    return maxPaymentFlow(sb);
}

TER
DirectIPaymentStep::check(StrandContext const& ctx, std::shared_ptr<const SLE> const& sleSrc) const
{
    {
        auto const sleLine = ctx.view.read(keylet::line(src_, dst_, currency_));
        if (!sleLine)
        {
            JLOG(j_.trace()) << "DirectStepI: No credit line. " << *this;
            return terNO_LINE;
        }

        auto const authField = (src_ > dst_) ? lsfHighAuth : lsfLowAuth;

        if ((((*sleSrc)[sfFlags] & lsfRequireAuth) != 0u) &&
            (((*sleLine)[sfFlags] & authField) == 0u) && (*sleLine)[sfBalance] == beast::kZERO)
        {
            JLOG(j_.debug()) << "DirectStepI: can't receive IOUs from issuer without auth."
                             << " src: " << src_;
            return terNO_AUTH;
        }

        if (ctx.prevStep != nullptr)
        {
            if (ctx.prevStep->bookStepBook())
            {
                auto const noRippleSrcToDst =
                    ((*sleLine)[sfFlags] & ((src_ > dst_) ? lsfHighNoRipple : lsfLowNoRipple));
                if (noRippleSrcToDst != 0u)
                    return terNO_RIPPLE;
            }
        }
    }

    {
        auto const owed = creditBalance(ctx.view, dst_, src_, currency_);
        if (owed <= beast::kZERO)
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
DirectIOfferCrossingStep::check(StrandContext const&, std::shared_ptr<const SLE> const&)
{
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

template <class TDerived>
std::pair<IOUAmount, DebtDirection>
DirectStepI<TDerived>::maxPaymentFlow(ReadView const& sb) const
{
    auto const srcOwed = toAmount<IOUAmount>(
        accountHolds(sb, src_, currency_, dst_, FreezeHandling::IgnoreFreeze, j_));

    if (srcOwed.signum() > 0)
        return {srcOwed, DebtDirection::Redeems};

    return {creditLimit2(sb, dst_, src_, currency_) + srcOwed, DebtDirection::Issues};
}

template <class TDerived>
DebtDirection
DirectStepI<TDerived>::debtDirection(ReadView const& sb, StrandDirection dir) const
{
    if (dir == StrandDirection::Forward && cache_)
        return cache_->srcDebtDir;

    auto const srcOwed = accountHolds(sb, src_, currency_, dst_, FreezeHandling::IgnoreFreeze, j_);
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
            IOUAmount(beast::kZERO), IOUAmount(beast::kZERO), IOUAmount(beast::kZERO), srcDebtDir);
        return {beast::kZERO, beast::kZERO};
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
            IOUAmount(beast::kZERO), IOUAmount(beast::kZERO), IOUAmount(beast::kZERO), srcDebtDir);
        return {beast::kZERO, beast::kZERO};
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
        return {false, EitherAmount(IOUAmount(beast::kZERO))};
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
        return {false, EitherAmount(IOUAmount(beast::kZERO))};
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

template <class TDerived>
std::pair<std::uint32_t, std::uint32_t>
DirectStepI<TDerived>::qualitiesSrcIssues(ReadView const& sb, DebtDirection prevStepDebtDirection)
    const
{
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
        auto const ter = checkFreeze(ctx.view, src_, dst_, currency_);
        if (!isTesSuccess(ter))
            return ter;
    }

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
/**
 * Test-only introspection: check whether a `Step` is a payment direct step
 * with the given src/dst/currency triple.
 *
 * Downcasts to `DirectStepI<DirectIPaymentStep>` — intentionally limited to
 * the payment variant so unit tests can distinguish it from offer-crossing
 * steps without adding virtual methods to the production interface.
 *
 * @param step     Step to inspect.
 * @param src      Expected source account.
 * @param dst      Expected destination account.
 * @param currency Expected currency.
 * @return `true` if the cast succeeds and all three fields match.
 */
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

/**
 * Factory for IOU direct-transfer steps.
 *
 * Selects `DirectIOfferCrossingStep` or `DirectIPaymentStep` based on
 * `ctx.offerCrossing`, constructs the step, runs `check()`, and returns
 * the validated step polymorphically. Called by the strand builder
 * (`toStrand`) when it encounters an IOU→IOU hop.
 *
 * @param ctx Strand-building context (ledger view, flags, previous step, etc.).
 * @param src Source account.
 * @param dst Destination account.
 * @param c   IOU currency.
 * @return `{tesSUCCESS, step}` on success, or `{errorCode, nullptr}` on
 *     validation failure.
 */
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
