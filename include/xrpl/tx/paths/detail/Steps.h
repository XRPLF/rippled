/** @file
 *  Architectural backbone of the XRPL payment flow engine.
 *
 *  Defines the `Step` polymorphic interface, the `Strand` type alias,
 *  factory functions for every concrete step variant, and the context
 *  and exception types that tie the system together.  Every transaction
 *  that moves value through more than one trust line or offer book —
 *  payments, offer crossings, AMM swaps — goes through these abstractions.
 *
 *  @see StrandFlow.h for the multi-strand execution engine that drives these steps.
 *  @see PaySteps.cpp for the implementations of `toStrand` and `normalizePath`.
 */

#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/QualityFunction.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/paths/detail/EitherAmount.h>

#include <boost/container/flat_set.hpp>

#include <optional>

namespace xrpl {
class PaymentSandbox;
class ReadView;
class ApplyView;
class AMMContext;

/** Whether an account is creating new IOU obligations or cancelling existing ones.
 *
 *  This is not merely metadata — it drives transfer fee logic.  When an account
 *  redeems (receives back its own issued IOU), no transfer fee is assessed on
 *  that side of the hop.  When an account issues new credit, a fee may apply.
 *  `qualityUpperBound()` queries this per step so quality estimates reflect real
 *  fee impacts.
 */
enum class DebtDirection {
    Issues,   /**< Account is creating new credit obligations; transfer fee may apply. */
    Redeems,  /**< Account is cancelling its own IOU; no transfer fee on this side. */
};

/** Direction along which quality is being evaluated for a trust-line hop. */
enum class QualityDirection {
    In,   /**< Quality-in: receiving side discount applied by the trust line. */
    Out,  /**< Quality-out: sending side premium applied by the trust line. */
};

/** Which execution pass is running over the strand. */
enum class StrandDirection {
    Forward,  /**< Forward pass: given input, compute produced output. */
    Reverse,  /**< Reverse pass: given desired output, compute required input. */
};

/** Whether the step is part of a payment or an offer-crossing operation. */
enum class OfferCrossing {
    No = 0,   /**< Ordinary payment; sender pays transfer fees. */
    Yes = 1,  /**< Offer crossing; owner may pay transfer fees. */
    Sell = 2, /**< Offer crossing with tfSell; sell exactly the input amount. */
};

/** Return true if @p dir is `DebtDirection::Redeems`. */
inline bool
redeems(DebtDirection dir)
{
    return dir == DebtDirection::Redeems;
}

/** Return true if @p dir is `DebtDirection::Issues`. */
inline bool
issues(DebtDirection dir)
{
    return dir == DebtDirection::Issues;
}

/** One hop in a payment path.
 *
 *  Concrete step classes:
 *  - `DirectStepI`      — IOU-to-IOU ripple between two accounts.
 *  - `BookStepII`       — IOU/IOU offer book hop.
 *  - `BookStepIX`       — IOU/XRP offer book hop.
 *  - `BookStepXI`       — XRP/IOU offer book hop.
 *  - `XRPEndpointStep`  — source or destination account for XRP.
 *  - `MPTEndpointStep`  — source or destination account for an MPT.
 *
 *  Steps are evaluated in two passes.  The *reverse* pass (`rev`) asks:
 *  "what input do I need to produce a given output?"  The *forward* pass
 *  (`fwd`) then confirms: "given this input, what output do I actually get?"
 *  This two-pass protocol lets `StrandFlow` identify the *limiting step*
 *  (where liquidity first runs out) and anchor the forward pass from there.
 *
 *  All amounts are transformed using liquidity of the same quality (quality =
 *  out/in).  A `BookStep` may consume multiple offers in a single call, but
 *  all of them come from the same quality tier.  If liquidity is insufficient,
 *  both `fwd` and `rev` return the achievable (in, out) pair — they never
 *  throw on a dry path.
 *
 *  Concrete classes implement the CRTP mixin `StepImp<TIn, TOut, TDerived>`,
 *  which type-erases the strongly-typed `revImp`/`fwdImp` methods into the
 *  `EitherAmount`-based virtual interface declared here.
 */
class Step
{
public:
    virtual ~Step() = default;

    /** Compute the input required to produce a desired output (reverse pass).
     *
     *  Given a desired output amount, returns the (actual_in, actual_out) pair
     *  achievable given current liquidity.  The returned pair may be less than
     *  requested when liquidity is exhausted; it is never more.  Results are
     *  cached and accessible via `cachedIn()` / `cachedOut()` so the forward
     *  pass can seed itself without re-executing the reverse pass.
     *
     *  @param sb      Sandbox carrying the running ledger state for the strand.
     *  @param afView  Ledger state before the strand started; used to classify
     *      offers as pre-existing unfunded vs. drained by this payment.
     *  @param ofrsToRm  Offer IDs found unfunded or in error are appended here
     *      for later deletion, regardless of whether the payment succeeds.
     *  @param out  The desired output amount.
     *  @return Pair of (actual input consumed, actual output produced).
     */
    virtual std::pair<EitherAmount, EitherAmount>
    rev(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& out) = 0;

    /** Compute the output produced by a given input (forward pass).
     *
     *  Given an available input amount, returns the (actual_in, actual_out)
     *  pair the step can produce.  The returned pair may be less than
     *  requested when liquidity is exhausted.
     *
     *  @param sb      Sandbox carrying the running ledger state for the strand.
     *  @param afView  Ledger state before the strand started; used to classify
     *      offers as pre-existing unfunded vs. drained by this payment.
     *  @param ofrsToRm  Offer IDs found unfunded or in error are appended here
     *      for later deletion, regardless of whether the payment succeeds.
     *  @param in  The available input amount.
     *  @return Pair of (actual input consumed, actual output produced).
     */
    virtual std::pair<EitherAmount, EitherAmount>
    fwd(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& in) = 0;

    /** Input amount cached by the most recent `rev()` call.
     *
     *  The forward pass in `StrandFlow` seeds itself from this value for the
     *  limiting step, avoiding a redundant reverse-pass invocation.
     *  Returns `std::nullopt` if `rev()` has not yet been called on this step.
     */
    [[nodiscard]] virtual std::optional<EitherAmount>
    cachedIn() const = 0;

    /** Output amount cached by the most recent `rev()` call.
     *
     *  Returns `std::nullopt` if `rev()` has not yet been called on this step.
     *  @see cachedIn()
     */
    [[nodiscard]] virtual std::optional<EitherAmount>
    cachedOut() const = 0;

    /** Return the source account if this step is a `DirectStepI`, else nullopt.
     *
     *  Used by `checkNoRipple` to determine whether consecutive direct steps
     *  pass through an account whose trust lines are both marked `noRipple`.
     */
    [[nodiscard]] virtual std::optional<AccountID>
    directStepSrcAcct() const
    {
        return std::nullopt;
    }

    /** Return the (src, dst) account pair for a direct step, else nullopt.
     *
     *  For `XRPEndpointStep`, one of the returned accounts is the XRP root
     *  account.  Intended for debugging and diagnostic purposes only.
     */
    [[nodiscard]] virtual std::optional<std::pair<AccountID, AccountID>>
    directStepAccts() const
    {
        return std::nullopt;
    }

    /** Determine whether this step issues or redeems on the given pass.
     *
     *  For a `DirectStepI`: returns `Redeems` when the source account is
     *  returning its own IOU to the issuer, `Issues` otherwise.  For a
     *  `BookStep`: returns `Issues` when the offer owner pays the transfer fee
     *  (i.e., owner-pays mode), `Redeems` otherwise.  The result propagates
     *  through the chain to drive transfer-fee logic in `qualityUpperBound`.
     *
     *  @param sb   View used to read trust-line balances.
     *  @param dir  `Reverse` when called from `rev()`, `Forward` from `fwd()`.
     *  @return The debt direction for this step on this pass.
     */
    [[nodiscard]] virtual DebtDirection
    debtDirection(ReadView const& sb, StrandDirection dir) const = 0;

    /** Return the `QualityIn` ratio for the destination trust line, or
     *  `QUALITY_ONE` (1.0) for non-`DirectStepI` steps.
     *
     *  A value less than `QUALITY_ONE` represents a discount the destination
     *  account has negotiated with its counterparty.
     */
    [[nodiscard]] virtual std::uint32_t
    lineQualityIn(ReadView const&) const
    {
        return QUALITY_ONE;
    }

    /** Compute a theoretical upper bound on the quality (out/in ratio) for this step.
     *
     *  Used by `StrandFlow` to rank competing strands from best to worst before
     *  committing to any execution.  The estimate is an upper bound because book
     *  offers visible at strand-construction time may be unfunded by the time
     *  the step actually runs.
     *
     *  @param v           View to query the current ledger state from.
     *  @param prevStepDir Debt direction of the immediately preceding step;
     *      influences whether this step's transfer fee is included in the bound.
     *  @return A pair whose first element is the quality upper bound, or
     *      `std::nullopt` if the step is dry (no funded offers / zero balance).
     *      The second element is this step's own `DebtDirection`, propagated to
     *      the next call in the chain.
     */
    [[nodiscard]] virtual std::pair<std::optional<Quality>, DebtDirection>
    qualityUpperBound(ReadView const& v, DebtDirection prevStepDir) const = 0;

    /** Return the quality function for this step.
     *
     *  For most steps (non-AMM), this wraps `qualityUpperBound` into a
     *  constant `QualityFunction{quality, CLOBLikeTag{}}`.  AMM `BookStep`
     *  overrides this to return the full non-linear quality function, which
     *  encodes the constant-product pricing curve.  `StrandFlow` uses the
     *  non-linear form to back-calculate the precise output required to exactly
     *  hit a `limitQuality` constraint, rather than executing to dryness.
     *
     *  @param v           View to query the current ledger state from.
     *  @param prevStepDir Debt direction of the immediately preceding step.
     *  @return Pair of (optional QualityFunction, this step's DebtDirection).
     *      `std::nullopt` in the first element means the step is dry.
     */
    [[nodiscard]] virtual std::pair<std::optional<QualityFunction>, DebtDirection>
    getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const;

    /** Return the number of offers consumed or partially consumed on the last run.
     *
     *  Includes expired and unfunded offers encountered during the pass.  This
     *  is a per-invocation count, not a cumulative total: an offer partially
     *  consumed in multiple passes is counted once per pass.  Non-book steps
     *  return 0.
     */
    [[nodiscard]] virtual std::uint32_t
    offersUsed() const
    {
        return 0;
    }

    /** Return the order book if this step is a `BookStep`, else nullopt. */
    [[nodiscard]] virtual std::optional<Book>
    bookStepBook() const
    {
        return std::nullopt;
    }

    /** Return true if @p out is zero for this step's output amount type. */
    [[nodiscard]] virtual bool
    isZero(EitherAmount const& out) const = 0;

    /** Return true if this step has consumed too many offers and should be
     *  treated as exhausted even though liquidity may remain on the book.
     *
     *  `StrandFlow` treats a strand containing an inactive step as dry for
     *  the current iteration.  The limit is `MaxOffersToConsume` (1000) per
     *  book step.
     */
    [[nodiscard]] virtual bool
    inactive() const
    {
        return false;
    }

    /** Return true if the output amounts of @p lhs and @p rhs are equal. */
    [[nodiscard]] virtual bool
    equalOut(EitherAmount const& lhs, EitherAmount const& rhs) const = 0;

    /** Return true if the input amounts of @p lhs and @p rhs are equal. */
    [[nodiscard]] virtual bool
    equalIn(EitherAmount const& lhs, EitherAmount const& rhs) const = 0;

    /** Re-execute the step in the forward direction and validate the result.
     *
     *  After the reverse pass identifies the limiting step, `StrandFlow` calls
     *  `validFwd` on each step to confirm that the forward pass produces an
     *  output consistent (within tolerance via `checkNear`) with what the
     *  reverse pass computed.  Returns `{false, zero}` on any `FlowException`.
     *
     *  @param sb      Sandbox carrying the running ledger state for the strand.
     *  @param afView  Ledger state before the strand started.
     *  @param in      The input amount to validate against.
     *  @return Pair of (is-valid flag, actual output amount).
     */
    virtual std::pair<bool, EitherAmount>
    validFwd(PaymentSandbox& sb, ApplyView& afView, EitherAmount const& in) = 0;

    /** Return true if lhs == rhs.

        @param lhs Step to compare.
        @param rhs Step to compare.
        @return true if lhs == rhs.
    */
    friend bool
    operator==(Step const& lhs, Step const& rhs)
    {
        return lhs.equal(rhs);
    }

    /** Return true if lhs != rhs.

        @param lhs Step to compare.
        @param rhs Step to compare.
        @return true if lhs != rhs.
    */
    friend bool
    operator!=(Step const& lhs, Step const& rhs)
    {
        return !(lhs == rhs);
    }

    /** Streaming operator for a Step. */
    friend std::ostream&
    operator<<(std::ostream& stream, Step const& step)
    {
        stream << step.logString();
        return stream;
    }

private:
    [[nodiscard]] virtual std::string
    logString() const = 0;

    [[nodiscard]] virtual bool
    equal(Step const& rhs) const = 0;
};

inline std::pair<std::optional<QualityFunction>, DebtDirection>
Step::getQualityFunc(ReadView const& v, DebtDirection prevStepDir) const
{
    auto const res = qualityUpperBound(v, prevStepDir);
    if (res.first)
        return {QualityFunction{*res.first, QualityFunction::CLOBLikeTag{}}, res.second};
    return {std::nullopt, res.second};
}

/** An ordered sequence of steps representing one candidate payment path.
 *
 *  The first step is always a source-account endpoint; the last is always
 *  a destination-account endpoint.  Inner steps are either direct (IOU
 *  ripple) or book (offer crossing) hops.  `toStrand` constructs a Strand
 *  from a normalised `STPath`.
 */
using Strand = std::vector<std::unique_ptr<Step>>;

/** Return the total number of offers consumed across all steps in @p strand
 *  on their most recent execution.
 *
 *  Non-book steps contribute 0.  This is a per-invocation snapshot, not a
 *  cumulative total across the full payment.
 */
inline std::uint32_t
offersUsed(Strand const& strand)
{
    std::uint32_t r = 0;
    for (auto const& step : strand)
    {
        if (step)
            r += step->offersUsed();
    }
    return r;
}

/** Return true if @p lhs and @p rhs represent identical payment paths.
 *
 *  Two strands are equal iff they have the same length and every
 *  corresponding step compares equal via `Step::operator==`.  Used by
 *  `toStrands` to deduplicate strands constructed from the default path
 *  and the user-supplied path set.
 */
inline bool
operator==(Strand const& lhs, Strand const& rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0, e = lhs.size(); i != e; ++i)
    {
        if (*lhs[i] != *rhs[i])
            return false;
    }
    return true;
}

/** Expand a user-supplied path to its canonical form by inserting implied nodes.
 *
 *  XRPL allows callers to omit obvious intermediate accounts (e.g. the issuer
 *  of a currency) from `STPath` entries.  This function inserts them so that
 *  `toStrand` sees an unambiguous sequence: source account → (optional SendMax
 *  issuer) → user-supplied hops → (optional Deliver issuer) → destination
 *  account.
 *
 *  @param src          Account sending assets.
 *  @param dst          Account receiving assets.
 *  @param deliver      Asset the destination account will receive.  If
 *      `deliver.account() == dst`, any issuer is accepted.
 *  @param sendMaxAsset Optional asset the source account is spending.  When
 *      absent, the deliver asset is used as the send asset.
 *  @param path         User-supplied liquidity sources: offer books and
 *      intermediate rippling accounts, in traversal order.
 *  @return Pair of (TER error code, normalised `STPath`).  On success the TER
 *      is `tesSUCCESS`; on failure the path is unspecified.
 */
std::pair<TER, STPath>
normalizePath(
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Asset> const& sendMaxAsset,
    STPath const& path);

/** Build a single Strand from one normalised path.
 *
 *  Calls `normalizePath` internally, then iterates the resulting nodes and
 *  invokes the appropriate `make_*` factory for each hop, threading
 *  `StrandContext` (with shared `seenDirectAssets` / `seenBookOuts` sets)
 *  through each call.  Returns on the first factory error.
 *
 *  @param sb                  View for trust lines, balances, and auth/freeze
 *      attributes used by step factories during construction.
 *  @param src                 Account sending assets.
 *  @param dst                 Account receiving assets.
 *  @param deliver             Asset the destination will receive.  If the
 *      issuer equals @p dst, any issuer is accepted.
 *  @param limitQuality        Optional quality floor.  During offer crossing,
 *      a `BookStep` stops consuming offers once the book-tip quality falls
 *      below this value.
 *  @param sendMaxAsset        Optional asset the source is spending.
 *  @param path                User-supplied liquidity sources in traversal
 *      order (offer books and rippling accounts).
 *  @param ownerPaysTransferFee  When true, the offer owner pays transfer fees;
 *      when false, the sender pays.
 *  @param offerCrossing       `No` for payments; `Yes` or `Sell` for offer
 *      crossing.
 *  @param ammContext          Shared AMM iteration counter.
 *  @param domainID            Optional permissioned-domain ID for order books.
 *  @param j                   Journal for logging.
 *  @return Pair of (TER error code, constructed Strand).  On failure the
 *      Strand is empty.
 */
std::pair<TER, Strand>
toStrand(
    ReadView const& sb,
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Asset> const& sendMaxAsset,
    STPath const& path,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    std::optional<uint256> const& domainID,
    beast::Journal j);

/** Build one Strand per path in @p paths, plus an optional default path.
 *
 *  Calls `toStrand` for each path.  Duplicate strands (same steps in the same
 *  order) are silently removed.  Returns an error if no valid strand can be
 *  constructed.
 *
 *  @param sb                  View for trust lines, balances, and auth/freeze
 *      attributes.
 *  @param src                 Account sending assets.
 *  @param dst                 Account receiving assets.
 *  @param deliver             Asset the destination will receive.
 *  @param limitQuality        Optional quality floor for offer-crossing paths.
 *  @param sendMax             Optional asset the source is spending.
 *  @param paths               Set of user-supplied paths to convert.
 *  @param addDefaultPath      When true, also construct the implicit
 *      direct-path strand (src → dst with no intermediate hops).
 *  @param ownerPaysTransferFee  When true, offer owners pay transfer fees.
 *  @param offerCrossing       `No` for payments; `Yes` or `Sell` for offer
 *      crossing.
 *  @param ammContext          Shared AMM iteration counter.
 *  @param domainID            Optional permissioned-domain ID for order books.
 *  @param j                   Journal for logging.
 *  @return Pair of (TER error code, vector of distinct Strands).  Returns an
 *      error if the vector would be empty.
 */
std::pair<TER, std::vector<Strand>>
toStrands(
    ReadView const& sb,
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Asset> const& sendMax,
    STPathSet const& paths,
    bool addDefaultPath,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    std::optional<uint256> const& domainID,
    beast::Journal j);

/** CRTP mixin that bridges typed step implementations into the type-erased `Step` interface.
 *
 *  Concrete step classes (e.g., `DirectStepI`, `BookStepII`) are strongly typed
 *  in their input and output amount types.  `StepImp` unwraps the `EitherAmount`
 *  variant at each call boundary (using `get<T>()`) and forwards to the derived
 *  class's `revImp` / `fwdImp` methods which receive the concrete `TIn` / `TOut`
 *  amounts.  A type mismatch produces a clear `std::logic_error` rather than
 *  undefined behaviour.
 *
 *  @tparam TIn      Input amount type (`XRPAmount`, `IOUAmount`, or `MPTAmount`).
 *  @tparam TOut     Output amount type.
 *  @tparam TDerived The concrete step class (CRTP parameter).
 */
template <StepAmount TIn, StepAmount TOut, class TDerived>
struct StepImp : public Step
{
private:
    explicit StepImp() = default;

public:
    std::pair<EitherAmount, EitherAmount>
    rev(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& out) override
    {
        auto const r = static_cast<TDerived*>(this)->revImp(sb, afView, ofrsToRm, get<TOut>(out));
        return {EitherAmount(r.first), EitherAmount(r.second)};
    }

    std::pair<EitherAmount, EitherAmount>
    fwd(PaymentSandbox& sb,
        ApplyView& afView,
        boost::container::flat_set<uint256>& ofrsToRm,
        EitherAmount const& in) override
    {
        auto const r = static_cast<TDerived*>(this)->fwdImp(sb, afView, ofrsToRm, get<TIn>(in));
        return {EitherAmount(r.first), EitherAmount(r.second)};
    }

    [[nodiscard]] bool
    isZero(EitherAmount const& out) const override
    {
        return get<TOut>(out) == beast::kZERO;
    }

    [[nodiscard]] bool
    equalOut(EitherAmount const& lhs, EitherAmount const& rhs) const override
    {
        return get<TOut>(lhs) == get<TOut>(rhs);
    }

    [[nodiscard]] bool
    equalIn(EitherAmount const& lhs, EitherAmount const& rhs) const override
    {
        return get<TIn>(lhs) == get<TIn>(rhs);
    }
    friend TDerived;
};

/** Exception signalling an unrecoverable failure inside a step.
 *
 *  Thrown when a step encounters a ledger state that cannot be handled by
 *  returning a zero amount — for example, an internal consistency check fails
 *  or `toStrand` constructs an empty strand despite a `tesSUCCESS` return.
 *  `StrandFlow` catches this exception at the strand boundary and treats the
 *  strand as dry rather than propagating the error further.
 *
 *  The embedded `ter` code is used by the calling flow to classify the failure
 *  (e.g., `tecINTERNAL`) and, in debug builds, to produce a diagnostic message.
 */
class FlowException : public std::runtime_error
{
public:
    TER ter;  /**< TER code identifying the failure category. */

    /** Construct with an explicit message.
     *  @param t    TER code.
     *  @param msg  Human-readable description of the failure.
     */
    FlowException(TER t, std::string const& msg) : std::runtime_error(msg), ter(t)
    {
    }

    /** Construct using the standard human-readable string for @p t. */
    explicit FlowException(TER t) : std::runtime_error(transHuman(t)), ter(t)
    {
    }
};

/** Return true if @p expected and @p actual are equal within floating-point tolerance.
 *
 *  IOU arithmetic uses a mantissa/exponent representation where accumulated
 *  rounding across a multi-step path can produce values that are near but not
 *  exactly equal.  This overload allows a relative tolerance of ~0.1% and an
 *  exponent difference of at most 1.  Values with exponent below -20 are
 *  treated as equal unconditionally (precision floor).
 *
 *  @note Called by concrete step implementations inside `validFwd` to confirm
 *      the forward-pass result matches the cached reverse-pass result.
 */
bool
checkNear(IOUAmount const& expected, IOUAmount const& actual);

/** Return true if @p expected equals @p actual (exact integer comparison).
 *
 *  MPT amounts are 64-bit integers; no floating-point tolerance is needed.
 */
inline bool
checkNear(MPTAmount const& expected, MPTAmount const& actual)
{
    return expected == actual;
}

/** Return true if @p expected equals @p actual (exact integer comparison).
 *
 *  XRP amounts are 64-bit integers; no floating-point tolerance is needed.
 */
inline bool
checkNear(XRPAmount const& expected, XRPAmount const& actual)
{
    return expected == actual;
}

/** Immutable context threaded through every step factory during strand construction.
 *
 *  Each `make_*` factory receives a `StrandContext` and uses it to validate the
 *  new step in isolation and as part of the growing strand.  The two loop-detection
 *  sets (`seenDirectAssets` and `seenBookOuts`) are mutated by each factory to
 *  register the new step's assets, so the next factory in the chain can detect
 *  duplicates.  Construction fails with `temBAD_PATH_LOOP` if a cycle is found.
 *
 *  `prevStep` allows each factory to query the preceding step's `debtDirection`
 *  when enforcing the `noRipple` constraint: a path that enters and exits an
 *  intermediate account through two trust lines both marked `noRipple` is rejected.
 */
struct StrandContext
{
    ReadView const& view;                       ///< Ledger state used to read balances and trust-line attributes.
    AccountID const strandSrc;                  ///< Source account for the whole strand.
    AccountID const strandDst;                  ///< Destination account for the whole strand.
    Asset const strandDeliver;                  ///< Asset the destination will receive.
    std::optional<Quality> const limitQuality;  ///< Quality floor; book steps stop when the book tip falls below this.
    bool const isFirst;                         ///< True if the step being constructed is the first in the strand.
    bool const isLast = false;                  ///< True if the step being constructed is the last in the strand.
    bool const ownerPaysTransferFee;            ///< True if the offer owner pays transfer fees rather than the sender.
    OfferCrossing const offerCrossing;          ///< `Yes` or `Sell` for offer-crossing operations; `No` for payments.
    bool const isDefaultPath;                   ///< True if this strand was constructed from the implicit default path.
    size_t const strandSize;                    ///< Number of steps already in the strand (before the new step is added).

    /** The step immediately preceding the one being constructed.
     *
     *  `nullptr` when `isFirst` is true.  Used by direct-step factories to
     *  query `debtDirection` and enforce the `noRipple` constraint across
     *  consecutive trust-line hops through the same account.
     */
    Step const* const prevStep = nullptr;

    /** Per-position asset tracking for direct-step cycle detection.
     *
     *  `seenDirectAssets[0]` records assets appearing as the *source* of a
     *  direct step; `seenDirectAssets[1]` records assets appearing as the
     *  *destination*.  An account node may appear at most twice in a strand
     *  (once as src, once as dst), but never in the same position twice.
     *  Insertions return `false` on collision, which causes the factory to
     *  return `temBAD_PATH_LOOP`.
     */
    std::array<boost::container::flat_set<Asset>, 2>& seenDirectAssets;

    /** Asset tracking for book-step cycle detection.
     *
     *  Records every asset that a book step has already output in this strand.
     *  Two book steps outputting the same asset would allow the first book's
     *  offers to unfund the second book's offers, so this is prohibited.
     *  Direct steps also reject a source asset that appears here (unless it is
     *  the output of the immediately preceding book step).
     */
    boost::container::flat_set<Asset>& seenBookOuts;

    AMMContext& ammContext;           ///< Shared AMM iteration counter; limits total AMM offer evaluations per payment.
    std::optional<uint256> domainID;  ///< If set, restricts book steps to offers in this permissioned domain.
    beast::Journal const j;           ///< Journal for diagnostic logging during strand construction.

    /** Construct a context for the next step to be added to @p strand.
     *
     *  @param view               Ledger read view.
     *  @param strand             Steps constructed so far; used to derive `isFirst`,
     *      `strandSize`, and `prevStep`.
     *  @param strandSrc          Strand source account.
     *  @param strandDst          Strand destination account.
     *  @param strandDeliver      Asset to be delivered to the destination.
     *  @param limitQuality       Optional quality floor for offer-crossing paths.
     *  @param isLast             True if this is the final step of the strand.
     *  @param ownerPaysTransferFee  True when the offer owner bears transfer fees.
     *  @param offerCrossing      Mode of the enclosing operation.
     *  @param isDefaultPath      True when constructing from the default path.
     *  @param seenDirectAssets   Mutable loop-detection set for direct steps; shared
     *      across all steps in this strand construction call.
     *  @param seenBookOuts       Mutable loop-detection set for book steps; shared
     *      across all steps in this strand construction call.
     *  @param ammContext         Shared AMM iteration counter.
     *  @param domainID           Optional permissioned domain ID.
     *  @param j                  Journal for logging.
     */
    StrandContext(
        ReadView const& view,
        std::vector<std::unique_ptr<Step>> const& strand,
        AccountID const& strandSrc,
        AccountID const& strandDst,
        Asset const& strandDeliver,
        std::optional<Quality> const& limitQuality,
        bool isLast,
        bool ownerPaysTransferFee,
        OfferCrossing offerCrossing,
        bool isDefaultPath,
        std::array<boost::container::flat_set<Asset>, 2>& seenDirectAssets,
        boost::container::flat_set<Asset>& seenBookOuts,
        AMMContext& ammContext,
        std::optional<uint256> const& domainID,
        beast::Journal j);
};

/** White-box inspection helpers for unit tests.
 *
 *  These functions allow tests to inspect step identity without exposing
 *  internal state through the production `Step` API.  They are declared in
 *  `PaySteps.cpp` and linked only into test binaries.
 */
namespace test {

/** Return true if @p step is a `DirectStepI` between @p src and @p dst
 *  for @p currency. */
bool
directStepEqual(
    Step const& step,
    AccountID const& src,
    AccountID const& dst,
    Currency const& currency);

/** Return true if @p step is an `MPTEndpointStep` between @p src and @p dst
 *  for the given MPT ID @p mptid. */
bool
mptEndpointStepEqual(
    Step const& step,
    AccountID const& src,
    AccountID const& dst,
    MPTID const& mptid);

/** Return true if @p step is an `XRPEndpointStep` for account @p acc. */
bool
xrpEndpointStepEqual(Step const& step, AccountID const& acc);

/** Return true if @p step is a `BookStep` for the given @p book. */
bool
bookStepEqual(Step const& step, xrpl::Book const& book);

}  // namespace test

/** Construct an IOU-to-IOU direct (ripple) step between @p src and @p dst.
 *
 *  Validates the trust line, freeze status, and `noRipple` constraint.
 *  Registers the source and destination assets in `ctx.seenDirectAssets`.
 *
 *  @param ctx  Construction context carrying loop-detection state.
 *  @param src  Account issuing (or redeeming) the IOU.
 *  @param dst  Counterparty on the trust line.
 *  @param c    Currency of the trust line.
 *  @return Pair of (TER, step).  TER is `tesSUCCESS` on success.
 */
std::pair<TER, std::unique_ptr<Step>>
makeDirectStepI(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    Currency const& c);

/** Construct an MPT endpoint step (source or destination account for an MPT).
 *
 *  Validates MPT authorization, freeze/lock status, and loop-detection.
 *
 *  @param ctx   Construction context.
 *  @param src   Source account.
 *  @param dst   Destination account.
 *  @param a     MPT issuance ID.
 *  @return Pair of (TER, step).
 */
std::pair<TER, std::unique_ptr<Step>>
makeMptEndpointStep(
    StrandContext const& ctx,
    AccountID const& src,
    AccountID const& dst,
    MPTID const& a);

/** Construct an IOU/IOU offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param in   Input IOU issue.
 *  @param out  Output IOU issue.
 *  @return Pair of (TER, step).
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepIi(StrandContext const& ctx, Issue const& in, Issue const& out);

/** Construct an IOU/XRP offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param in   Input IOU issue.
 *  @return Pair of (TER, step).  Output is always XRP.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepIx(StrandContext const& ctx, Issue const& in);

/** Construct an XRP/IOU offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param out  Output IOU issue.
 *  @return Pair of (TER, step).  Input is always XRP.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepXi(StrandContext const& ctx, Issue const& out);

/** Construct an XRP endpoint step (source or destination account for XRP).
 *
 *  @param ctx  Construction context.
 *  @param acc  The XRP-holding account.
 *  @return Pair of (TER, step).
 */
std::pair<TER, std::unique_ptr<Step>>
makeXrpEndpointStep(StrandContext const& ctx, AccountID const& acc);

/** Construct an MPT/MPT offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param in   Input MPT issue.
 *  @param out  Output MPT issue.
 *  @return Pair of (TER, step).
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepMm(StrandContext const& ctx, MPTIssue const& in, MPTIssue const& out);

/** Construct an MPT/XRP offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param in   Input MPT issue.
 *  @return Pair of (TER, step).  Output is always XRP.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepMx(StrandContext const& ctx, MPTIssue const& in);

/** Construct an XRP/MPT offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param out  Output MPT issue.
 *  @return Pair of (TER, step).  Input is always XRP.
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepXm(StrandContext const& ctx, MPTIssue const& out);

/** Construct an MPT/IOU cross-asset offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param in   Input MPT issue.
 *  @param out  Output IOU issue.
 *  @return Pair of (TER, step).
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepMi(StrandContext const& ctx, MPTIssue const& in, Issue const& out);

/** Construct an IOU/MPT cross-asset offer-book step.
 *
 *  @param ctx  Construction context.
 *  @param in   Input IOU issue.
 *  @param out  Output MPT issue.
 *  @return Pair of (TER, step).
 */
std::pair<TER, std::unique_ptr<Step>>
makeBookStepIm(StrandContext const& ctx, Issue const& in, MPTIssue const& out);

/** Compile-time predicate: true iff @p strand is a direct XRP-to-XRP transfer.
 *
 *  A direct XRP-to-XRP path consists of exactly two endpoint steps (source and
 *  destination) with no intermediate hops.  The flow engine uses this to skip
 *  execution entirely for such strands, since they cannot change value.  The
 *  check short-circuits at compile time via `if constexpr` when either template
 *  parameter is not `XRPAmount`, avoiding any runtime evaluation.
 *
 *  @tparam InAmt   Amount type flowing into the strand.
 *  @tparam OutAmt  Amount type flowing out of the strand.
 *  @param strand   The strand to test.
 *  @return True only when both template parameters are `XRPAmount` and the strand
 *      contains exactly 2 steps.
 */
template <StepAmount InAmt, StepAmount OutAmt>
bool
isDirectXrpToXrp(Strand const& strand)
{
    if constexpr (std::is_same_v<InAmt, XRPAmount> && std::is_same_v<OutAmt, XRPAmount>)
    {
        return strand.size() == 2;
    }
    else
    {
        return false;
    }
}

}  // namespace xrpl
