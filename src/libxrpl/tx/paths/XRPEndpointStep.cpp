/** @file
 *  XRP source/destination endpoint step for the XRPL payment path engine.
 *
 *  Every strand begins or ends with an `XRPEndpointStep` when XRP is the
 *  currency being sent or received.  This file implements that bookend step,
 *  connecting a real account's XRP balance to the abstract flow graph used by
 *  `Flow.cpp` and `StrandFlow.h`.  No currency conversion occurs here — the
 *  step simply debits or credits XRP from/to `acc_` while handing the amount
 *  to the virtual `xrpAccount()` sentinel.
 *
 *  Two concrete subclasses (`XRPEndpointPaymentStep` and
 *  `XRPEndpointOfferCrossingStep`) share all logic via CRTP, differing only
 *  in how they compute the spendable XRP balance (`xrpLiquid()`).
 */
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/detail/AmountSpec.h>
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

/** CRTP base for the XRP-endpoint step (first or last step in a strand).
 *
 *  Handles all reverse-pass, forward-pass, validation, and quality logic for
 *  a step that transfers XRP between a concrete account (`acc_`) and the
 *  virtual `xrpAccount()` sentinel.  The sole behavioral variation — how much
 *  XRP the account can spend — is supplied by the derived class via
 *  `xrpLiquid()`, dispatched at compile time through `static_cast<TDerived>`.
 *
 *  @tparam TDerived  Concrete subclass (`XRPEndpointPaymentStep` or
 *      `XRPEndpointOfferCrossingStep`) that implements `xrpLiquid()` and
 *      `logString()`.
 */
template <class TDerived>
class XRPEndpointStep : public StepImp<XRPAmount, XRPAmount, XRPEndpointStep<TDerived>>
{
private:
    AccountID acc_;
    bool const isLast_;
    beast::Journal const j_;

    /** Single cache entry shared between `cachedIn` and `cachedOut`.
     *
     *  Because XRP transfers are 1:1, input equals output for every execution.
     *  Since this step is always a strand endpoint, only one direction's cache
     *  is ever consumed by an adjacent step, so a single optional suffices.
     */
    std::optional<XRPAmount> cache_;

    /** Wrap `cache_` in an `EitherAmount`, or return nullopt if not yet populated. */
    [[nodiscard]] std::optional<EitherAmount>
    cached() const
    {
        if (!cache_)
            return std::nullopt;
        return EitherAmount(*cache_);
    }

    XRPEndpointStep(StrandContext const& ctx, AccountID const& acc)
        : acc_(acc), isLast_(ctx.isLast), j_(ctx.j)
    {
    }

public:
    /** Return the XRP-holding account associated with this endpoint step. */
    [[nodiscard]] AccountID const&
    acc() const
    {
        return acc_;
    }

    /** Return the (sender, receiver) account pair for this step.
     *
     *  One of the two accounts is always `xrpAccount()`, the virtual XRP
     *  sentinel.  For a first step (XRP sender) the pair is `(acc_, xrpAccount())`;
     *  for a last step (XRP receiver) it is `(xrpAccount(), acc_)`.
     */
    [[nodiscard]] std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const override
    {
        if (isLast_)
            return std::make_pair(xrpAccount(), acc_);
        return std::make_pair(acc_, xrpAccount());
    }

    /** Return the amount cached by the most recent reverse pass, or nullopt. */
    [[nodiscard]] std::optional<EitherAmount>
    cachedIn() const override
    {
        return cached();
    }

    /** Return the amount cached by the most recent reverse pass, or nullopt.
     *
     *  @note `cachedIn` and `cachedOut` return the same value because XRP
     *      transfers are 1:1 and this step is always a strand endpoint.
     */
    [[nodiscard]] std::optional<EitherAmount>
    cachedOut() const override
    {
        return cached();
    }

    /** Always returns `DebtDirection::Issues`.
     *
     *  XRP has no issuer, so this step never redeems toward a counterparty.
     *  The debt-direction concept is only meaningful for IOU trust-line steps.
     */
    [[nodiscard]] DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const override
    {
        return DebtDirection::Issues;
    }

    /** Return the quality upper bound for this step.
     *
     *  XRP transfers are always 1:1, so the bound is `Quality{STAmount::kU_RATE_ONE}`
     *  (rate = 1.0) regardless of ledger state or direction.
     *
     *  @param v            Ledger read view (unused for XRP).
     *  @param prevStepDir  Debt direction of the preceding step (unused for XRP).
     *  @return Pair of (fixed 1:1 quality, `DebtDirection::Issues`).
     */
    [[nodiscard]] std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const override;

    /** Reverse pass: compute input required to produce @p out.
     *
     *  For a last step (XRP receiver), accepts @p out unconditionally — a
     *  receiving account can always accept XRP.  For a first step (XRP sender),
     *  caps the result at `min(xrpLiquid(sb), out)`.  The result is stored in
     *  `cache_` for use by the forward pass.
     *
     *  @param sb        Payment sandbox carrying running ledger state.
     *  @param afView    Pre-strand ledger state (unused for XRP endpoint steps).
     *  @param ofrsToRm  Unfunded offer set (unused; XRP steps consume no offers).
     *  @param out       Desired output amount.
     *  @return Pair of (actual input, actual output); both are zero if
     *      `accountSend` fails.
     */
    std::pair<XRPAmount, XRPAmount>
    revImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        XRPAmount const& out);

    /** Forward pass: commit the transfer given available input @p in.
     *
     *  Mirrors `revImp`: last steps accept @p in unconditionally; first steps
     *  cap at `min(xrpLiquid(sb), in)`.  Requires `cache_` to be populated,
     *  i.e., `revImp` must have run first.
     *
     *  @param sb        Payment sandbox carrying running ledger state.
     *  @param afView    Pre-strand ledger state (unused for XRP endpoint steps).
     *  @param ofrsToRm  Unfunded offer set (unused).
     *  @param in        Available input amount.
     *  @return Pair of (actual input consumed, actual output produced); both
     *      are zero if `accountSend` fails.
     */
    std::pair<XRPAmount, XRPAmount>
    fwdImp(
        PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        XRPAmount const& in);

    /** Validate that @p in is consistent with the cached reverse-pass result.
     *
     *  For a first step (sender), confirms the current spendable balance still
     *  covers `in`; logs a warning if the balance has shifted since the reverse
     *  pass.  If the incoming amount diverges from `cache_`, logs a warning but
     *  still returns `true` — the discrepancy is noted and deferred to `fwdImp`.
     *
     *  @param sb     Payment sandbox.
     *  @param afView Pre-strand ledger state (unused).
     *  @param in     The input amount to validate.
     *  @return Pair of (valid flag, @p in).  Returns `{false, zero}` only if
     *      `cache_` is not populated.
     */
    std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) override;

    /** Validate this step at strand construction time.
     *
     *  Enforces five invariants in order:
     *  1. `acc_` is non-zero.
     *  2. `acc_` maps to a live `AccountRoot` in the ledger.
     *  3. The step is strictly first or last in the strand (XRP cannot be an
     *     intermediate currency).
     *  4. No global or directional freeze blocks the transfer.
     *  5. `xrpIssue()` has not already appeared on the same side of the strand
     *     (loop detection via `ctx.seenDirectAssets`).
     *
     *  @param ctx  Strand construction context.
     *  @return `tesSUCCESS` on success, `temBAD_PATH` for a malformed path or
     *      zero account, `terNO_ACCOUNT` if the account does not exist,
     *      `tecFROZEN` if a freeze check fails, or `temBAD_PATH_LOOP` if a
     *      cycle through XRP is detected.
     */
    [[nodiscard]] TER
    check(StrandContext const& ctx) const;

protected:
    /** Compute the spendable XRP balance for `acc_`, applying a reserve offset.
     *
     *  Delegates to `xrpl::xrpLiquid`, which subtracts the base reserve and
     *  the per-object owner reserve from the account's total balance.
     *
     *  @param sb                Ledger view to read the account balance from.
     *  @param reserveReduction  Reserve units to subtract before the normal
     *      reserve calculation.  Pass 0 for payments; pass -1 for offer
     *      crossing when the buyer does not yet hold the delivered asset
     *      (see `XRPEndpointOfferCrossingStep`).
     *  @return Spendable XRP, floored at zero.
     */
    XRPAmount
    xrpLiquidImpl(ReadView& sb, std::int32_t reserveReduction) const
    {
        return xrpl::xrpLiquid(sb, acc_, reserveReduction, j_);
    }

    /** Build the diagnostic log string for this step.
     *
     *  @param name  The concrete class name to embed in the output.
     *  @return A string of the form `"<name>:\nAcc: <acc_>"`.
     */
    std::string
    logStringImpl(char const* name) const
    {
        std::ostringstream ostr;
        ostr << name << ": "
             << "\nAcc: " << acc_;
        return ostr.str();
    }

private:
    template <class P>
    friend bool
    operator==(XRPEndpointStep<P> const& lhs, XRPEndpointStep<P> const& rhs);

    friend bool
    operator!=(XRPEndpointStep const& lhs, XRPEndpointStep const& rhs)
    {
        return !(lhs == rhs);
    }

    /** Return true if @p rhs is an `XRPEndpointStep` with the same account and direction. */
    [[nodiscard]] bool
    equal(Step const& rhs) const override
    {
        if (auto ds = dynamic_cast<XRPEndpointStep const*>(&rhs))
        {
            return *this == *ds;
        }
        return false;
    }

    friend TDerived;
};

//------------------------------------------------------------------------------

/** XRP endpoint step for ordinary payments.
 *
 *  Computes the spendable balance with no reserve reduction — the full base
 *  and owner reserves are always deducted before spending.
 *
 *  @see XRPEndpointOfferCrossingStep for the offer-crossing variant that may
 *      allow spending one additional reserve unit.
 */
class XRPEndpointPaymentStep : public XRPEndpointStep<XRPEndpointPaymentStep>
{
public:
    XRPEndpointPaymentStep(StrandContext const& ctx, AccountID const& acc)
        : XRPEndpointStep<XRPEndpointPaymentStep>(ctx, acc)
    {
    }

    /** Return the spendable XRP balance with the standard full reserve applied. */
    XRPAmount
    xrpLiquid(ReadView& sb) const
    {
        return xrpLiquidImpl(sb, 0);
        ;
    }

    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("XRPEndpointPaymentStep");
    }
};

/** XRP endpoint step for offer-crossing operations.
 *
 *  During offer crossing the buyer may not yet hold a trust line (or MPT
 *  holding) for the delivered asset — that object is created *after* XRP is
 *  debited.  To prevent this sequencing from blocking otherwise valid crosses,
 *  the step is allowed to spend one additional reserve unit of XRP.
 *  `reserveReduction_` encodes that offset (-1 when the trust line / MPT does
 *  not yet exist, 0 otherwise) and is applied via `xrpLiquidImpl`.
 */
class XRPEndpointOfferCrossingStep : public XRPEndpointStep<XRPEndpointOfferCrossingStep>
{
private:
    /** Determine how many reserve units to deduct before computing liquid XRP.
     *
     *  Returns -1 (reduce required reserve by one unit) when this is the first
     *  step in the strand AND the buyer does not yet hold the strand's delivery
     *  asset (trust line for IOU, MPToken for MPT).  Returns 0 otherwise.
     *
     *  @param ctx  Construction context carrying the strand's delivery asset.
     *  @param acc  The XRP-spending account to check.
     *  @return -1 if a reserve reduction applies; 0 otherwise.
     */
    static std::int32_t
    computeReserveReduction(StrandContext const& ctx, AccountID const& acc)
    {
        if (ctx.isFirst)
        {
            return ctx.strandDeliver.visit(
                [&](Issue const& issue) {
                    if (!ctx.view.exists(keylet::line(acc, issue)))
                        return -1;
                    return 0;
                },
                [&](MPTIssue const& issue) {
                    if (!ctx.view.exists(keylet::mptoken(issue.getMptID(), acc)))
                        return -1;
                    return 0;
                });
        }
        return 0;
    }

public:
    XRPEndpointOfferCrossingStep(StrandContext const& ctx, AccountID const& acc)
        : XRPEndpointStep<XRPEndpointOfferCrossingStep>(ctx, acc)
        , reserveReduction_(computeReserveReduction(ctx, acc))
    {
    }

    /** Return the spendable XRP balance, applying `reserveReduction_` if set. */
    XRPAmount
    xrpLiquid(ReadView& sb) const
    {
        return xrpLiquidImpl(sb, reserveReduction_);
    }

    [[nodiscard]] std::string
    logString() const override
    {
        return logStringImpl("XRPEndpointOfferCrossingStep");
    }

private:
    /** Reserve offset applied by `xrpLiquid`; -1 when the delivery asset is new, 0 otherwise. */
    std::int32_t const reserveReduction_;
};

//------------------------------------------------------------------------------

/** Return true if @p lhs and @p rhs represent the same XRP endpoint step.
 *
 *  Two steps are equal iff they reference the same account and occupy the same
 *  position in the strand (both first, or both last).
 */
template <class TDerived>
inline bool
operator==(XRPEndpointStep<TDerived> const& lhs, XRPEndpointStep<TDerived> const& rhs)
{
    return lhs.acc_ == rhs.acc_ && lhs.isLast_ == rhs.isLast_;
}

template <class TDerived>
std::pair<std::optional<Quality>, DebtDirection>
XRPEndpointStep<TDerived>::qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const
{
    return {Quality{STAmount::kU_RATE_ONE}, this->debtDirection(v, StrandDirection::Forward)};
}

template <class TDerived>
std::pair<XRPAmount, XRPAmount>
XRPEndpointStep<TDerived>::revImp(
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    XRPAmount const& out)
{
    auto const balance = static_cast<TDerived const*>(this)->xrpLiquid(sb);

    auto const result = isLast_ ? out : std::min(balance, out);

    auto& sender = isLast_ ? xrpAccount() : acc_;
    auto& receiver = isLast_ ? acc_ : xrpAccount();
    auto ter = accountSend(sb, sender, receiver, toSTAmount(result), j_);
    if (!isTesSuccess(ter))
        return {XRPAmount{beast::kZERO}, XRPAmount{beast::kZERO}};

    cache_.emplace(result);
    return {result, result};
}

template <class TDerived>
std::pair<XRPAmount, XRPAmount>
XRPEndpointStep<TDerived>::fwdImp(
    PaymentSandbox& sb,
    ApplyView& afView,
    boost::container::flat_set<uint256>& ofrsToRm,
    XRPAmount const& in)
{
    XRPL_ASSERT(cache_, "xrpl::XRPEndpointStep::fwdImp : cache is set");
    auto const balance = static_cast<TDerived const*>(this)->xrpLiquid(sb);

    auto const result = isLast_ ? in : std::min(balance, in);

    auto& sender = isLast_ ? xrpAccount() : acc_;
    auto& receiver = isLast_ ? acc_ : xrpAccount();
    auto ter = accountSend(sb, sender, receiver, toSTAmount(result), j_);
    if (!isTesSuccess(ter))
        return {XRPAmount{beast::kZERO}, XRPAmount{beast::kZERO}};

    cache_.emplace(result);
    return {result, result};
}

template <class TDerived>
std::pair<bool, EitherAmount>
XRPEndpointStep<TDerived>::validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in)
{
    if (!cache_)
    {
        JLOG(j_.error()) << "Expected valid cache in validFwd";
        return {false, EitherAmount(XRPAmount(beast::kZERO))};
    }

    XRPL_ASSERT(in.holds<XRPAmount>(), "xrpl::XRPEndpointStep::validFwd : input is XRP");

    auto const& xrpIn = in.get<XRPAmount>();
    auto const balance = static_cast<TDerived const*>(this)->xrpLiquid(sb);

    if (!isLast_ && balance < xrpIn)
    {
        JLOG(j_.warn()) << "XRPEndpointStep: Strand re-execute check failed."
                        << " Insufficient balance: " << to_string(balance)
                        << " Requested: " << to_string(xrpIn);
        return {false, EitherAmount(balance)};
    }

    if (xrpIn != *cache_)
    {
        JLOG(j_.warn()) << "XRPEndpointStep: Strand re-execute check failed."
                        << " ExpectedIn: " << to_string(*cache_)
                        << " CachedIn: " << to_string(xrpIn);
    }
    return {true, in};
}

template <class TDerived>
TER
XRPEndpointStep<TDerived>::check(StrandContext const& ctx) const
{
    if (!acc_)
    {
        JLOG(j_.debug()) << "XRPEndpointStep: specified bad account.";
        return temBAD_PATH;
    }

    auto sleAcc = ctx.view.read(keylet::account(acc_));
    if (!sleAcc)
    {
        JLOG(j_.warn()) << "XRPEndpointStep: can't send or receive XRP from "
                           "non-existent account: "
                        << acc_;
        return terNO_ACCOUNT;
    }

    if (!ctx.isFirst && !ctx.isLast)
    {
        return temBAD_PATH;
    }

    auto& src = isLast_ ? xrpAccount() : acc_;
    auto& dst = isLast_ ? acc_ : xrpAccount();
    auto ter = checkFreeze(ctx.view, src, dst, xrpCurrency());
    if (!isTesSuccess(ter))
        return ter;

    auto const issuesIndex = isLast_ ? 0 : 1;
    if (!ctx.seenDirectAssets[issuesIndex].insert(xrpIssue()).second)
    {
        JLOG(j_.debug()) << "XRPEndpointStep: loop detected: Index: " << ctx.strandSize << ' '
                         << *this;
        return temBAD_PATH_LOOP;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

namespace test {
/** Return true if @p step is an `XRPEndpointPaymentStep` for @p acc.
 *
 *  Downcasts to `XRPEndpointStep<XRPEndpointPaymentStep>` via `dynamic_cast`
 *  so test code can verify step identity without access to private types.
 *
 *  @param step  The step to inspect.
 *  @param acc   Expected account ID.
 *  @return True if @p step is an XRP payment endpoint for @p acc.
 */
bool
xrpEndpointStepEqual(Step const& step, AccountID const& acc)
{
    if (auto xs = dynamic_cast<XRPEndpointStep<XRPEndpointPaymentStep> const*>(&step))
    {
        return xs->acc() == acc;
    }
    return false;
}
}  // namespace test

//------------------------------------------------------------------------------

/** Construct an XRP endpoint step for account @p acc, then validate it.
 *
 *  Allocates `XRPEndpointOfferCrossingStep` when `ctx.offerCrossing != No`,
 *  otherwise `XRPEndpointPaymentStep`.  Calls `check()` immediately after
 *  construction and discards the step on failure.
 *
 *  @param ctx  Strand construction context.
 *  @param acc  The XRP-holding account for this endpoint.
 *  @return Pair of (TER, step).  On success TER is `tesSUCCESS` and the step
 *      is non-null; on failure TER carries the error code and the step is null.
 */
std::pair<TER, std::unique_ptr<Step>>
makeXrpEndpointStep(StrandContext const& ctx, AccountID const& acc)
{
    TER ter = tefINTERNAL;
    std::unique_ptr<Step> r;
    if (ctx.offerCrossing != OfferCrossing::No)
    {
        auto offerCrossingStep = std::make_unique<XRPEndpointOfferCrossingStep>(ctx, acc);
        ter = offerCrossingStep->check(ctx);
        r = std::move(offerCrossingStep);
    }
    else  // payment
    {
        auto paymentStep = std::make_unique<XRPEndpointPaymentStep>(ctx, acc);
        ter = paymentStep->check(ctx);
        r = std::move(paymentStep);
    }
    if (!isTesSuccess(ter))
        return {ter, nullptr};

    return {tesSUCCESS, std::move(r)};
}

}  // namespace xrpl
