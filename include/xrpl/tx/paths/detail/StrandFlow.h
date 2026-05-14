/**
 * @file StrandFlow.h
 * @brief Core payment flow execution engine: two-pass strand algorithm and
 *        multi-strand outer search loop.
 *
 * This header is the heart of the XRPL payment engine. It provides:
 *   - `flow<TInAmt, TOutAmt>(PaymentSandbox, Strand, ...)` — executes one
 *     strand via a reverse-then-forward two-pass algorithm.
 *   - `flow<TInAmt, TOutAmt>(PaymentSandbox, vector<Strand>, ...)` — the
 *     outer multi-strand search loop that consumes strands in quality order
 *     until the requested output is satisfied or all strands are dry.
 *
 * Every XRP payment — direct transfer, cross-currency, or offer cross — is
 * ultimately executed through these functions. Callers are in `Flow.cpp`,
 * which dispatches to the correct template instantiation via `std::visit`.
 */
#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/Flow.h>
#include <xrpl/tx/paths/detail/AmountSpec.h>
#include <xrpl/tx/paths/detail/FlatSets.h>
#include <xrpl/tx/paths/detail/FlowDebugInfo.h>
#include <xrpl/tx/paths/detail/Steps.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <boost/container/flat_set.hpp>

#include <algorithm>
#include <iterator>
#include <numeric>

namespace xrpl {

/**
 * Result of executing a single Strand through the two-pass flow algorithm.
 *
 * Bundles the actual amounts consumed/produced, the proposed ledger state
 * (inside a `PaymentSandbox` that can be merged or discarded), the set of
 * offers that must be removed regardless of payment outcome, and flags
 * indicating whether the strand is now exhausted.
 *
 * @tparam TInAmt  Input amount type (`XRPAmount`, `IOUAmount`, or `MPTAmount`).
 * @tparam TOutAmt Output amount type.
 */
template <class TInAmt, class TOutAmt>
struct StrandResult
{
    bool success = false;                          ///< True if the strand produced non-zero output.
    TInAmt in = beast::kZERO;                      ///< Actual input consumed by the strand.
    TOutAmt out = beast::kZERO;                    ///< Actual output produced by the strand.
    std::optional<PaymentSandbox> sandbox;         ///< Proposed ledger mutations; empty on failure.
    boost::container::flat_set<uint256> ofrsToRm;  ///< Offers to remove from the ledger (bad/expired), even on failure.
    /// Number of offers consumed or partially consumed (includes expired and
    /// unfunded offers). Counts toward `maxOffersToConsider` in the outer loop.
    std::uint32_t ofrsUsed = 0;
    /// True when the strand has no remaining liquidity or has consumed too many
    /// offers. An inactive strand is not returned to the `next_` candidate set
    /// for future rounds.
    bool inactive = false;

    /** Constructs a default (failed/dry) result with no sandbox. */
    StrandResult() = default;

    /**
     * Constructs a successful strand result.
     *
     * @param strand              The strand that was executed (used to count offers).
     * @param in                  Actual input consumed.
     * @param out                 Actual output produced.
     * @param sandbox             Sandbox containing the resulting ledger mutations.
     * @param ofrsToRemoveMember  Offers to remove regardless of payment success.
     * @param inactive            True if the strand is now exhausted.
     */
    StrandResult(
        Strand const& strand,
        TInAmt const& in,
        TOutAmt const& out,
        PaymentSandbox&& sandbox,
        boost::container::flat_set<uint256> ofrsToRemoveMember,
        bool inactive)
        : success(true)
        , in(in)
        , out(out)
        , sandbox(std::move(sandbox))
        , ofrsToRm(std::move(ofrsToRemoveMember))
        , ofrsUsed(offersUsed(strand))
        , inactive(inactive)
    {
    }

    /**
     * Constructs a failed (dry) strand result that still carries offers to
     * remove.  Used when the strand produced no usable liquidity but did
     * encounter unfunded or expired offers that must be cleaned up.
     *
     * @param strand              The strand that was executed.
     * @param ofrsToRemoveMember  Offers to remove even though the strand failed.
     */
    StrandResult(Strand const& strand, boost::container::flat_set<uint256> ofrsToRemoveMember)
        : ofrsToRm(std::move(ofrsToRemoveMember)), ofrsUsed(offersUsed(strand))
    {
    }
};

/**
 * Execute a single payment strand and request `out` units of output.
 *
 * Implements the classical reverse-then-forward two-pass algorithm:
 *
 * **Reverse pass (right-to-left):** Each step is called via `rev()` in
 * reverse order, threading the requested output backwards to determine how
 * much input each step needs.  When a step cannot fully satisfy its request
 * (the *limiting step*), the sandbox is discarded and that step is
 * re-executed with the capped amount.  If the limiting step is index 0 and
 * would exceed `maxIn`, the step is re-executed forward with exactly `maxIn`.
 *
 * **Forward pass (left-to-right from limiting step):** After the reverse
 * pass, every step to the right of the limiting step is called via `fwd()`
 * to propagate the actual amounts and complete the sandbox state.
 *
 * A debug-only re-validation (`#ifndef NDEBUG`) re-executes the whole strand
 * forward via `validFwd()` to catch inconsistencies in step implementations.
 *
 * Two early exits apply: an empty strand returns immediately, and a direct
 * XRP-to-XRP strand (no exchange needed) returns a dry result without any
 * execution.  Any `FlowException` is caught and converted to a dry result so
 * a bad offer in one strand does not abort the entire payment.
 *
 * @tparam TInAmt  Input amount type (`XRPAmount`, `IOUAmount`, `MPTAmount`).
 * @tparam TOutAmt Output amount type.
 * @param baseView  Read-only view of trust lines and balances before this
 *                  strand executes (the "all funds" baseline).
 * @param strand    Ordered list of `Step` objects describing the route.
 * @param maxIn     If present, caps the total input this strand may consume.
 * @param out       Amount of output requested from the strand.
 * @param j         Journal for diagnostic log messages.
 * @return A `StrandResult` containing the actual amounts, the proposed
 *         ledger mutations in a `PaymentSandbox`, offers to remove, and the
 *         `inactive` flag indicating whether the strand is now exhausted.
 */
template <class TInAmt, class TOutAmt>
StrandResult<TInAmt, TOutAmt>
flow(
    PaymentSandbox const& baseView,
    Strand const& strand,
    std::optional<TInAmt> const& maxIn,
    TOutAmt const& out,
    beast::Journal j)
{
    using Result = StrandResult<TInAmt, TOutAmt>;
    if (strand.empty())
    {
        JLOG(j.warn()) << "Empty strand passed to Liquidity";
        return {};
    }

    boost::container::flat_set<uint256> ofrsToRm;

    if (isDirectXrpToXrp<TInAmt, TOutAmt>(strand))
    {
        return Result{strand, std::move(ofrsToRm)};
    }

    try
    {
        std::size_t const s = strand.size();

        std::size_t limitingStep = strand.size();
        std::optional<PaymentSandbox> sb(&baseView);
        // afView is the "all funds" snapshot: account balances as they were
        // before any step in this strand executed. Steps use it to determine
        // whether an offer is currently funded (vs. the evolving `sb` view).
        std::optional<PaymentSandbox> afView(&baseView);
        EitherAmount limitStepOut;
        {
            EitherAmount stepOut(out);
            for (auto i = s; i--;)
            {
                auto r = strand[i]->rev(*sb, *afView, ofrsToRm, stepOut);
                if (strand[i]->isZero(r.second))
                {
                    JLOG(j.trace()) << "Strand found dry in rev";
                    return Result{strand, std::move(ofrsToRm)};
                }

                if (i == 0 && maxIn && *maxIn < get<TInAmt>(r.first))
                {
                    // Step 0 would exceed maxIn: re-execute forward with
                    // exactly maxIn rather than in reverse, because step 0
                    // has no predecessor to supply a revised input.
                    sb.emplace(&baseView);
                    limitingStep = i;

                    r = strand[i]->fwd(*sb, *afView, ofrsToRm, EitherAmount(*maxIn));
                    limitStepOut = r.second;

                    if (strand[i]->isZero(r.second))
                    {
                        JLOG(j.trace()) << "First step found dry";
                        return Result{strand, std::move(ofrsToRm)};
                    }
                    if (get<TInAmt>(r.first) != *maxIn)
                    {
                        // Invariant violation: discarding the old sandbox can
                        // only increase available liquidity, so re-executing
                        // with maxIn should always consume exactly maxIn.
                        // LCOV_EXCL_START
                        JLOG(j.fatal())
                            << "Re-executed limiting step failed. r.first: "
                            << to_string(get<TInAmt>(r.first)) << " maxIn: " << to_string(*maxIn);
                        UNREACHABLE(
                            "xrpl::flow : first step re-executing the "
                            "limiting step failed");
                        return Result{strand, std::move(ofrsToRm)};
                        // LCOV_EXCL_STOP
                    }
                }
                else if (!strand[i]->equalOut(r.second, stepOut))
                {
                    // This step is the limiting step: it produced less than
                    // requested. Discard the partial sandbox (fresh view gives
                    // the step full liquidity again) and re-execute in reverse
                    // with the capped amount to record the definitive state.
                    sb.emplace(&baseView);
                    afView.emplace(&baseView);
                    limitingStep = i;

                    stepOut = r.second;
                    r = strand[i]->rev(*sb, *afView, ofrsToRm, stepOut);
                    limitStepOut = r.second;

                    if (strand[i]->isZero(r.second))
                    {
                        // A tiny input amount can cause this step to output
                        // zero. I.e. 10^-80 IOU into an IOU -> XRP offer.
                        JLOG(j.trace()) << "Limiting step found dry";
                        return Result{strand, std::move(ofrsToRm)};
                    }
                    if (!strand[i]->equalOut(r.second, stepOut))
                    {
                        // Invariant violation: a fresh sandbox can only
                        // increase liquidity, so the re-executed limiting step
                        // must be able to deliver at least as much as before.
                        // LCOV_EXCL_START
#ifndef NDEBUG
                        JLOG(j.fatal())
                            << "Re-executed limiting step failed. r.second: " << r.second
                            << " stepOut: " << stepOut;
#else
                        JLOG(j.fatal()) << "Re-executed limiting step failed";
#endif
                        UNREACHABLE(
                            "xrpl::flow : limiting step re-executing the "
                            "limiting step failed");
                        return Result{strand, std::move(ofrsToRm)};
                        // LCOV_EXCL_STOP
                    }
                }

                stepOut = r.first;  // propagate: predecessor must deliver what this step will consume
            }
        }

        {
            EitherAmount stepIn(limitStepOut);
            for (auto i = limitingStep + 1; i < s; ++i)
            {
                auto const r = strand[i]->fwd(*sb, *afView, ofrsToRm, stepIn);
                if (strand[i]->isZero(r.second))
                {
                    // A tiny input amount can cause this step to output zero.
                    // I.e. 10^-80 IOU into an IOU -> XRP offer.
                    JLOG(j.trace()) << "Non-limiting step found dry";
                    return Result{strand, std::move(ofrsToRm)};
                }
                if (!strand[i]->equalIn(r.first, stepIn))
                {
                    // Invariant violation: the reverse pass already found the
                    // global limiting step, so the forward pass must never
                    // encounter a new limit.
                    // LCOV_EXCL_START
#ifndef NDEBUG
                    JLOG(j.fatal()) << "Re-executed forward pass failed. r.first: " << r.first
                                    << " stepIn: " << stepIn;
#else
                    JLOG(j.fatal()) << "Re-executed forward pass failed";
#endif
                    UNREACHABLE(
                        "xrpl::flow : non-limiting step re-executing the "
                        "forward pass failed");
                    return Result{strand, std::move(ofrsToRm)};
                    // LCOV_EXCL_STOP
                }
                stepIn = r.second;
            }
        }

        // NOLINTBEGIN(bugprone-unchecked-optional-access) cachedIn/Out set after strand is stepped
        // above
        auto const strandIn = *strand.front()->cachedIn();
        auto const strandOut = *strand.back()->cachedOut();
        // NOLINTEND(bugprone-unchecked-optional-access)

#ifndef NDEBUG
        {
            // Check that the strand will execute as intended
            // Re-executing the strand will change the cached values
            PaymentSandbox checkSB(&baseView);
            PaymentSandbox checkAfView(&baseView);
            EitherAmount stepIn(
                *strand[0]->cachedIn());  // NOLINT(bugprone-unchecked-optional-access)
            for (auto i = 0; i < s; ++i)
            {
                bool valid = false;
                std::tie(valid, stepIn) = strand[i]->validFwd(checkSB, checkAfView, stepIn);
                if (!valid)
                {
                    JLOG(j.warn()) << "Strand re-execute check failed. Step: " << i;
                    break;
                }
            }
        }
#endif

        bool const inactive =
            std::any_of(strand.begin(), strand.end(), [](std::unique_ptr<Step> const& step) {
                return step->inactive();
            });

        return Result(
            strand,
            get<TInAmt>(strandIn),
            get<TOutAmt>(strandOut),
            std::move(*sb),
            std::move(ofrsToRm),
            inactive);
    }
    catch (FlowException const&)
    {
        return Result{strand, std::move(ofrsToRm)};
    }
}

/// @cond INTERNAL
/**
 * Aggregate result of the multi-strand payment flow loop.
 *
 * Accumulates the total input consumed and output produced across all
 * successful strand rounds, the merged `PaymentSandbox` representing all
 * proposed ledger changes, offers that must be cleaned up regardless of
 * payment success, and a `TER` result code.
 *
 * @tparam TInAmt  Input amount type.
 * @tparam TOutAmt Output amount type.
 */
template <class TInAmt, class TOutAmt>
struct FlowResult
{
    TInAmt in = beast::kZERO;                             ///< Total input consumed across all rounds.
    TOutAmt out = beast::kZERO;                           ///< Total output delivered across all rounds.
    std::optional<PaymentSandbox> sandbox;                ///< Merged sandbox; present only on success.
    boost::container::flat_set<uint256> removableOffers;  ///< Offers to remove whether or not payment succeeds.
    TER ter = temUNKNOWN;                                 ///< `tesSUCCESS` or a TEC/TEF error code.

    /** Constructs a default (unknown-error) result. */
    FlowResult() = default;

    /**
     * Constructs a successful result with all amounts and ledger state.
     *
     * @param in       Total input consumed.
     * @param out      Total output delivered.
     * @param sandbox  Merged sandbox holding all ledger mutations.
     * @param ofrsToRm Offers to remove from the ledger.
     */
    FlowResult(
        TInAmt const& in,
        TOutAmt const& out,
        PaymentSandbox&& sandbox,
        boost::container::flat_set<uint256> ofrsToRm)
        : in(in)
        , out(out)
        , sandbox(std::move(sandbox))
        , removableOffers(std::move(ofrsToRm))
        , ter(tesSUCCESS)
    {
    }

    /**
     * Constructs a failure result carrying only the offers to remove.
     * Used when the flow loop exits with no usable liquidity at all.
     *
     * @param ter      Error code describing the failure.
     * @param ofrsToRm Offers to remove even though the payment failed.
     */
    FlowResult(TER ter, boost::container::flat_set<uint256> ofrsToRm)
        : removableOffers(std::move(ofrsToRm)), ter(ter)
    {
    }

    /**
     * Constructs a partial-failure result that includes the amounts achieved
     * before the flow loop stopped (e.g., `tecPATH_PARTIAL`).
     *
     * @param ter      Error code (typically `tecPATH_PARTIAL`).
     * @param in       Input consumed up to the point of failure.
     * @param out      Output delivered up to the point of failure.
     * @param ofrsToRm Offers to remove regardless of payment outcome.
     */
    FlowResult(
        TER ter,
        TInAmt const& in,
        TOutAmt const& out,
        boost::container::flat_set<uint256> ofrsToRm)
        : in(in), out(out), removableOffers(std::move(ofrsToRm)), ter(ter)
    {
    }
};
/// @endcond

/// @cond INTERNAL
/**
 * Compute an upper bound on the exchange rate (quality) achievable by a strand.
 *
 * Calls `qualityUpperBound()` on each step in sequence, composing the
 * per-step bounds via `composedQuality()` while propagating the `DebtDirection`
 * (distinguishing redemption from issuance, which affects transfer fees).
 * Returns `std::nullopt` if any step is provably dry.
 *
 * This estimate may be optimistic — unfunded offers at the tip of a book can
 * make the actual quality lower — but it is a sound heuristic for ranking
 * candidate strands in `ActiveStrands::activateNext()`.
 *
 * @param v       Read-only ledger view.
 * @param strand  The strand to evaluate.
 * @return An upper-bound `Quality`, or `std::nullopt` if the strand is dry.
 */
inline std::optional<Quality>
qualityUpperBound(ReadView const& v, Strand const& strand)
{
    Quality q{STAmount::kU_RATE_ONE};
    std::optional<Quality> stepQ;
    DebtDirection dir = DebtDirection::Issues;
    for (auto const& step : strand)
    {
        if (std::tie(stepQ, dir) = step->qualityUpperBound(v, dir); stepQ)
        {
            q = composedQuality(q, *stepQ);
        }
        else
        {
            return std::nullopt;
        }
    }
    return q;
};
/// @endcond

/// @cond INTERNAL
/**
 * Reduce the requested output to the amount that exactly satisfies
 * `limitQuality` when there is exactly one active strand.
 *
 * This optimization applies only to AMM-backed strands where quality is not
 * constant: for an AMM, the average exchange rate is a quadratic function of
 * output, so requesting less output yields a better average quality. The
 * function collects per-step `QualityFunction` objects and combines them into
 * a strand-level quality function, then solves for the output that achieves
 * `limitQuality` via `QualityFunction::outFromAvgQ()`.
 *
 * A relative-distance guard (`withinRelativeDistance(..., 1e-9)`) absorbs
 * floating-point rounding and avoids spurious adjustments. If the quality
 * function is constant (no AMM steps), or if any step does not provide a
 * quality function, the function returns `remainingOut` unchanged.
 *
 * @tparam TOutAmt  Output amount type.
 * @param v             Ledger view used by step quality-function queries.
 * @param strand        The single active strand.
 * @param remainingOut  Current remaining output request; returned unchanged
 *                      if no adjustment is possible.
 * @param limitQuality  Minimum acceptable average quality.
 * @return The adjusted output cap, which is ≤ `remainingOut`.
 */
template <typename TOutAmt>
inline TOutAmt
limitOut(
    ReadView const& v,
    Strand const& strand,
    TOutAmt const& remainingOut,
    Quality const& limitQuality)
{
    std::optional<QualityFunction> stepQualityFunc;
    std::optional<QualityFunction> qf;
    DebtDirection dir = DebtDirection::Issues;
    for (auto const& step : strand)
    {
        if (std::tie(stepQualityFunc, dir) = step->getQualityFunc(v, dir); stepQualityFunc)
        {
            if (!qf)
            {
                qf = stepQualityFunc;
            }
            else
            {
                qf->combine(*stepQualityFunc);
            }
        }
        else
        {
            return remainingOut;
        }
    }

    // If every step is a fixed-rate hop the quality function is constant;
    // no adjustment is possible or necessary.
    if (!qf || qf->isConst())
        return remainingOut;

    auto const out = [&]() {
        auto const out = qf->outFromAvgQ(limitQuality);
        if (!out)
            return remainingOut;
        if constexpr (std::is_same_v<TOutAmt, XRPAmount>)
        {
            return XRPAmount{*out};
        }
        else if constexpr (std::is_same_v<TOutAmt, IOUAmount>)
        {
            return IOUAmount{*out};
        }
        else if constexpr (std::is_same_v<TOutAmt, MPTAmount>)
        {
            return MPTAmount{*out};
        }
        else
        {
            return STAmount{remainingOut.asset(), out->mantissa(), out->exponent()};
        }
    }();
    // A computed `out` that is negligibly close to `remainingOut` (within 1e-9
    // relative) is treated as equal to avoid spurious reductions caused by
    // floating-point rounding in `outFromAvgQ`.
    if (withinRelativeDistance(out, remainingOut, Number(1, -9)))
        return remainingOut;
    return std::min(out, remainingOut);
};
/// @endcond

/// @cond INTERNAL
/**
 * Lazy candidate set for the multi-strand liquidity search loop.
 *
 * Tracks which strands are still eligible to provide liquidity across
 * outer iterations.  Maintains two sets:
 *   - `cur_`  — strands being evaluated in the *current* round.
 *   - `next_` — strands to evaluate in the *next* round (strands that still
 *               had remaining liquidity after the current round, plus any
 *               strand not yet reached in `cur_`).
 *
 * The probe-and-push pattern guarantees that only one strand is consumed per
 * outer iteration: the loop picks the first strand from `cur_` that yields
 * usable liquidity (`best`), pushes all remaining unchecked `cur_` strands to
 * `next_`, and returns `best` to `next_` if the strand is not exhausted.
 * This ensures high-quality strands that only partially satisfy the remaining
 * output remain in contention for subsequent rounds.
 *
 * @note `activateNext()` uses `std::ranges::stable_sort` intentionally: the
 *       stability is required for deterministic tie-breaking across different
 *       C++ standard library implementations, which is critical for consensus.
 */
class ActiveStrands
{
private:
    std::vector<Strand const*> cur_;   ///< Strands under evaluation this round.
    std::vector<Strand const*> next_;  ///< Candidates for the next round.

public:
    /**
     * Initialise from all candidate strands; all are placed in `next_` so
     * that the first call to `activateNext()` populates `cur_`.
     *
     * @param strands  The full set of payment path strands.
     */
    ActiveStrands(std::vector<Strand> const& strands)
    {
        cur_.reserve(strands.size());
        next_.reserve(strands.size());
        for (auto& strand : strands)
            next_.push_back(&strand);
    }

    /**
     * Advance to the next round: sort `next_` by quality upper-bound
     * (best first) and swap it into `cur_`.
     *
     * Strands whose `qualityUpperBound` falls below `limitQuality` are pruned
     * and will never appear in `cur_` again. The sort is a `stable_sort` to
     * guarantee deterministic ordering when two strands have equal quality
     * upper bounds — required for consensus across nodes.
     *
     * @param v             Read-only ledger view for quality estimation.
     * @param limitQuality  If present, strands below this threshold are pruned.
     */
    void
    activateNext(ReadView const& v, std::optional<Quality> const& limitQuality)
    {
        cur_.clear();
        if (!next_.empty())
        {
            std::vector<std::pair<Quality, Strand const*>> strandQualities;
            strandQualities.reserve(next_.size());
            if (next_.size() > 1)  // no need to sort one strand
            {
                for (Strand const* strand : next_)
                {
                    if (strand == nullptr)
                    {
                        // should not happen
                        continue;
                    }
                    if (auto const qual = qualityUpperBound(v, *strand))
                    {
                        if (limitQuality && *qual < *limitQuality)
                        {
                            // Prune this strand permanently. Transfer fees can
                            // cause quality to increase as an account moves from
                            // redeeming to issuing, but this is rare enough that
                            // we accept the occasional premature prune.
                            continue;
                        }
                        strandQualities.emplace_back(*qual, strand);
                    }
                }
                // stable_sort is mandatory: deterministic tie-breaking is a
                // consensus requirement across different stdlib implementations.
                std::ranges::stable_sort(
                    strandQualities,

                    [](auto const& lhs, auto const& rhs) {
                        // higher qualities first
                        return std::get<Quality>(lhs) > std::get<Quality>(rhs);
                    });
                next_.clear();
                next_.reserve(strandQualities.size());
                for (auto const& sq : strandQualities)
                {
                    next_.push_back(std::get<Strand const*>(sq));
                }
            }
        }
        std::swap(cur_, next_);
    }

    /**
     * Return the strand at position `i` in the current round's candidate set.
     *
     * @param i  Zero-based index into `cur_`.
     * @return   Pointer to the strand, or `nullptr` if `i` is out of range
     *           (which should never happen in correct usage).
     */
    [[nodiscard]] Strand const*
    get(size_t i) const
    {
        if (i >= cur_.size())
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::ActiveStrands::get : input out of range");
            return nullptr;
            // LCOV_EXCL_STOP
        }
        return cur_[i];
    }

    /**
     * Return a strand to the `next_` candidate set for consideration in the
     * following round.  Called for the winning `best` strand when it still
     * has remaining liquidity (i.e., `!f.inactive`).
     *
     * @param s  Strand to preserve for the next round.
     */
    void
    push(Strand const* s)
    {
        next_.push_back(s);
    }

    /**
     * Move all strands from `cur_[i..end)` to `next_`.
     *
     * Called after the probe-and-push loop selects a winning strand: all
     * strands after the winner in `cur_` (which were not yet evaluated this
     * round) are deferred to `next_` so they remain in contention.
     *
     * @param i  Index of the first strand to defer (inclusive).
     */
    void
    pushRemainingCurToNext(size_t i)
    {
        if (i >= cur_.size())
            return;
        next_.insert(next_.end(), std::next(cur_.begin(), i), cur_.end());
    }

    /** Number of strands in the current round's candidate set. */
    [[nodiscard]] auto
    size() const
    {
        return cur_.size();
    }
};
/// @endcond

/**
 * Execute a multi-strand payment and request `outReq` units of output.
 *
 * This is the top-level payment loop.  It iterates over candidate strands in
 * quality order (best first), consuming one strand per outer iteration until
 * `outReq` is satisfied, all strands are exhausted, or a safety limit is hit.
 *
 * **Safety limits:**
 *   - `maxTries = 1000` — maximum outer iterations; returns
 *     `telFAILED_PROCESSING` if exceeded.
 *   - `maxOffersToConsider = 1500` — maximum cumulative offers consumed across
 *     all strands; triggers early exit with the best result so far.
 *
 * **Precision:** Rather than accumulating a running total (lossy for IOU
 * arithmetic), each round's amounts are collected into `flat_multiset`
 * containers (`savedIns`, `savedOuts`) and summed smallest-to-largest via
 * `std::accumulate` to minimise floating-point drift.
 *
 * **Offer cleanup:** Bad offers (`ofrsToRm`) are deleted from the sandbox
 * immediately after each strand attempt — even failed strands — so they
 * cannot interfere with subsequent strands. A superset (`ofrsToRmOnFail`)
 * is returned to callers for cleanup if the payment ultimately fails.
 *
 * **FillOrKill semantics (offer crossing):** The final section branches on the
 * `fixFillOrKill` amendment and the `offerCrossing` mode:
 *   1. Without `tfSell` (or pre-amendment): if `actualOut < outReq`, kill.
 *   2. With `tfSell` (or pre-amendment): if `remainingIn != 0`, kill (all of
 *      `TakerGets` must be spent).
 *
 * **AMM integration:** `ammContext.setMultiPath(n > 1)` informs the AMM
 * whether it is competing with other active strands (affects virtual offer
 * sizing). `ammContext.clear()` resets the per-strand used flag before each
 * strand attempt. `ammContext.update()` increments the AMM iteration counter
 * after each successful round.
 *
 * @tparam TInAmt   Input amount type (`XRPAmount`, `IOUAmount`, `MPTAmount`).
 * @tparam TOutAmt  Output amount type.
 * @param baseView      Read-only view of trust lines and balances.
 * @param strands       All candidate payment path strands.
 * @param outReq        Requested output amount.
 * @param partialPayment  If true, a partial delivery is acceptable.
 * @param offerCrossing   Indicates whether this is offer crossing and which
 *                        mode (`No`, `Sell`, or full `FillOrKill`).
 * @param limitQuality    If present, only strands meeting this quality
 *                        threshold are used.
 * @param sendMaxST       If present, caps the total input consumed.
 * @param j               Journal for diagnostic log messages.
 * @param ammContext      Tracks AMM iteration state across rounds.
 * @param flowDebugInfo   If non-null, receives per-round debug telemetry.
 * @return A `FlowResult` with the aggregate amounts, merged sandbox, offers to
 *         clean up, and a `TER` result code (`tesSUCCESS`,
 *         `tecPATH_PARTIAL`, `tecPATH_DRY`, or `telFAILED_PROCESSING`).
 */
template <StepAmount TInAmt, StepAmount TOutAmt>
FlowResult<TInAmt, TOutAmt>
flow(
    PaymentSandbox const& baseView,
    std::vector<Strand> const& strands,
    TOutAmt const& outReq,
    bool partialPayment,
    OfferCrossing offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMaxST,
    beast::Journal j,
    AMMContext& ammContext,
    path::detail::FlowDebugInfo* flowDebugInfo = nullptr)
{
    /** Holds the winning strand's result for the current outer iteration. */
    struct BestStrand
    {
        TInAmt in;
        TOutAmt out;
        PaymentSandbox sb;
        Strand const& strand;
        Quality quality;

        BestStrand(
            TInAmt const& in,
            TOutAmt const& out,
            PaymentSandbox&& sb,
            Strand const& strand,
            Quality const& quality)
            : in(in), out(out), sb(std::move(sb)), strand(strand), quality(quality)
        {
        }
    };

    std::size_t const maxTries = 1000;
    std::size_t curTry = 0;
    std::uint32_t const maxOffersToConsider = 1500;
    std::uint32_t offersConsidered = 0;

    // GCC incorrectly warns about uninitialized values when initialising
    // std::optional<TInAmt> via a copy constructor. Using make_optional
    // avoids this false positive without semantic change.
    TInAmt const sendMaxInit = sendMaxST ? toAmount<TInAmt>(*sendMaxST) : TInAmt{beast::kZERO};
    std::optional<TInAmt> const sendMax =
        (sendMaxST && sendMaxInit >= beast::kZERO) ? std::make_optional(sendMaxInit) : std::nullopt;
    std::optional<TInAmt> remainingIn = !!sendMax ? std::make_optional(sendMaxInit) : std::nullopt;

    TOutAmt remainingOut(outReq);

    PaymentSandbox sb(&baseView);

    ActiveStrands activeStrands(strands);

    // Amounts are accumulated in sorted flat_multiset containers and summed
    // smallest-to-largest to minimise floating-point precision loss.
    boost::container::flat_multiset<TInAmt> savedIns;
    savedIns.reserve(maxTries);
    boost::container::flat_multiset<TOutAmt> savedOuts;
    savedOuts.reserve(maxTries);

    auto sum = [](auto const& col) {
        using TResult = std::decay_t<decltype(*col.begin())>;
        if (col.empty())
            return TResult{beast::kZERO};
        return std::accumulate(col.begin() + 1, col.end(), *col.begin());
    };

    // Accumulates all bad offers seen during the flow loop. Returned to the
    // caller for removal even if the payment ultimately fails.
    boost::container::flat_set<uint256> ofrsToRmOnFail;

    while (remainingOut > beast::kZERO && (!remainingIn || *remainingIn > beast::kZERO))
    {
        ++curTry;
        if (curTry >= maxTries)
        {
            return {telFAILED_PROCESSING, std::move(ofrsToRmOnFail)};
        }

        activeStrands.activateNext(sb, limitQuality);

        ammContext.setMultiPath(activeStrands.size() > 1);

        // Apply AMM quality-function optimisation only when there is a single
        // active strand and a limitQuality threshold is set.
        auto const limitRemainingOut = [&]() {
            if (activeStrands.size() == 1 && limitQuality)
            {
                if (auto const strand = activeStrands.get(0))
                    return limitOut(sb, *strand, remainingOut, *limitQuality);
            }
            return remainingOut;
        }();
        auto const adjustedRemOut = limitRemainingOut != remainingOut;

        boost::container::flat_set<uint256> ofrsToRm;
        std::optional<BestStrand> best;
        if (flowDebugInfo)
            flowDebugInfo->newLiquidityPass();
        for (size_t strandIndex = 0, sie = activeStrands.size(); strandIndex != sie; ++strandIndex)
        {
            Strand const* strand = activeStrands.get(strandIndex);
            if (!strand)
            {
                // should not happen
                continue;
            }
            // Reset AMM used-flag before each strand: a failed prior strand
            // may have left the flag set, which would incorrectly mark the
            // next (unrelated) strand as having consumed AMM liquidity.
            ammContext.clear();
            if (offerCrossing != OfferCrossing::No && limitQuality)
            {
                auto const strandQ = qualityUpperBound(sb, *strand);
                if (!strandQ || *strandQ < *limitQuality)
                    continue;
            }
            auto f = flow<TInAmt, TOutAmt>(sb, *strand, remainingIn, limitRemainingOut, j);

            setUnion(ofrsToRm, f.ofrsToRm);  // collect bad offers even on strand failure

            offersConsidered += f.ofrsUsed;

            if (!f.success || f.out == beast::kZERO)
                continue;

            if (flowDebugInfo)
                flowDebugInfo->pushLiquiditySrc(EitherAmount(f.in), EitherAmount(f.out));

            XRPL_ASSERT(
                f.out <= remainingOut && f.sandbox && (!remainingIn || f.in <= *remainingIn),
                "xrpl::flow : remaining constraints");

            Quality const q(f.out, f.in);

            JLOG(j.trace()) << "New flow iter (iter, in, out): " << curTry - 1 << " "
                            << to_string(f.in) << " " << to_string(f.out);

            // `limitOut()` targets exact limitQuality but actual quality may
            // differ by ~1e-7 due to rounding; accept values within that band.
            if (limitQuality && q < *limitQuality &&
                (!adjustedRemOut || !withinRelativeDistance(q, *limitQuality, Number(1, -7))))
            {
                JLOG(j.trace()) << "Path rejected by limitQuality"
                                << " limit: " << *limitQuality << " path q: " << q;
                continue;
            }

            XRPL_ASSERT(!best, "xrpl::flow : best is unset");
            if (!f.inactive)
                activeStrands.push(strand);
            best.emplace(f.in, f.out, std::move(*f.sandbox), *strand, q);
            activeStrands.pushRemainingCurToNext(strandIndex + 1);
            break;
        }

        bool const shouldBreak = !best || offersConsidered >= maxOffersToConsider;

        if (best)
        {
            savedIns.insert(best->in);
            savedOuts.insert(best->out);
            remainingOut = outReq - sum(savedOuts);
            if (sendMax)
                remainingIn = *sendMax - sum(savedIns);

            if (flowDebugInfo)
            {
                flowDebugInfo->pushPass(
                    EitherAmount(best->in), EitherAmount(best->out), activeStrands.size());
            }

            JLOG(j.trace()) << "Best path: in: " << to_string(best->in)
                            << " out: " << to_string(best->out)
                            << " remainingOut: " << to_string(remainingOut);

            best->sb.apply(sb);
            ammContext.update();
        }
        else
        {
            JLOG(j.trace()) << "All strands dry.";
        }

        best.reset();  // view in best must be destroyed before modifying base view
        if (!ofrsToRm.empty())
        {
            setUnion(ofrsToRmOnFail, ofrsToRm);
            for (auto const& o : ofrsToRm)
            {
                if (auto ok = sb.peek(keylet::offer(o)))
                    offerDelete(sb, ok, j);
            }
        }

        if (shouldBreak)
            break;
    }

    auto const actualOut = sum(savedOuts);
    auto const actualIn = sum(savedIns);

    JLOG(j.trace()) << "Total flow: in: " << to_string(actualIn)
                    << " out: " << to_string(actualOut);

    /* flowCross doesn't handle offer crossing with tfFillOrKill flag correctly.
     * 1. If tfFillOrKill is set then the owner must receive the full
     *   TakerPays. We reverse pays and gets because during crossing
     *   we are taking, therefore the owner must deliver the full TakerPays and
     *   the entire TakerGets doesn't have to be spent.
     *   Pre-fixFillOrKill amendment code fails if the entire TakerGets
     *   is not spent. fixFillOrKill addresses this issue.
     * 2. If tfSell is also set then the owner must spend the entire TakerGets
     *   even if it means obtaining more than TakerPays. Since the pays and gets
     *   are reversed, the owner must send the entire TakerGets.
     */
    bool const fillOrKillEnabled = baseView.rules().enabled(fixFillOrKill);

    if (actualOut != outReq)
    {
        if (actualOut > outReq)
        {
            // Rounding in the payment engine is causing this assert to sometimes fire with "dust"
            // amounts. This is causing issues when running debug builds of xrpld.
            // While this issue still needs to be resolved, the assert is causing more harm than
            // good at this point.
            // UNREACHABLE("xrpl::flow : rounding error");

            return {tefEXCEPTION, std::move(ofrsToRmOnFail)};
        }
        if (!partialPayment)
        {
            // If we're offerCrossing a !partialPayment, then we're handling tfFillOrKill.
            // Pre-fixFillOrKill amendment:
            //   That case is handled below; not here.
            // fixFillOrKill amendment:
            //   That case is handled here if tfSell is also not set; i.e, case 1.
            if (offerCrossing == OfferCrossing::No ||
                (fillOrKillEnabled && offerCrossing != OfferCrossing::Sell))
            {
                return {tecPATH_PARTIAL, actualIn, actualOut, std::move(ofrsToRmOnFail)};
            }
        }
        else if (actualOut == beast::kZERO)
        {
            return {tecPATH_DRY, std::move(ofrsToRmOnFail)};
        }
    }
    if (offerCrossing != OfferCrossing::No &&
        (!partialPayment && (!fillOrKillEnabled || offerCrossing == OfferCrossing::Sell)))
    {
        // If we're offer crossing and partialPayment is *not* true, then
        // we're handling a FillOrKill offer.  In this case remainingIn must
        // be zero (all funds must be consumed) or else we kill the offer.
        // Pre-fixFillOrKill amendment:
        //   Handles both cases 1. and 2.
        // fixFillOrKill amendment:
        //   Handles 2. 1. is handled above and falls through for tfSell.
        XRPL_ASSERT(remainingIn, "xrpl::flow : nonzero remainingIn");
        if (remainingIn && *remainingIn != beast::kZERO)
            return {tecPATH_PARTIAL, actualIn, actualOut, std::move(ofrsToRmOnFail)};
    }

    return {actualIn, actualOut, std::move(sb), std::move(ofrsToRmOnFail)};
}

}  // namespace xrpl
