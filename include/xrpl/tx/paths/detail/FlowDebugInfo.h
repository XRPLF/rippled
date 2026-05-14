#pragma once

#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/detail/AmountSpec.h>

#include <boost/container/flat_map.hpp>

#include <chrono>
#include <optional>
#include <sstream>

namespace xrpl::path::detail {

/**
 * Per-payment telemetry accumulator for the flow engine.
 *
 * Constructed once per call to `flow()` and threaded down the call stack as
 * a raw pointer. The `flow()` entry point accepts `FlowDebugInfo* = nullptr`,
 * making the entire instrumentation path opt-in with zero overhead when the
 * pointer is null. `RippleCalc::rippleCalculate` currently always passes
 * `nullptr`, so no telemetry is gathered in production consensus paths — this
 * struct is intended for testing, benchmarking, and developer tooling only.
 *
 * Two `boost::container::flat_map` members (reserved to 16 entries at
 * construction) store named timing intervals (`timePoints`) and occurrence
 * counters (`counts`). The flat-map layout is cache-friendly at the small
 * sizes typical for a single payment execution.
 *
 * The single `PassInfo passInfo` member captures the sequence of incremental
 * liquidity fills performed by the outer loop in `StrandFlow.h`.
 *
 * @note Callers must ensure the `FlowDebugInfo` object outlives any `Stopper`
 *     returned by `timeBlock()`, as `Stopper` holds a raw pointer back to
 *     this struct.
 * @see FlowDebugInfo::timeBlock, FlowDebugInfo::PassInfo
 */
struct FlowDebugInfo
{
    /** High-resolution clock used for all timing measurements. */
    using clock = std::chrono::high_resolution_clock;

    /** A point in time as recorded by `clock`. */
    using time_point = clock::time_point;

    /** Named timing intervals: tag → (start, end). Reserved to 16 entries. */
    boost::container::flat_map<std::string, std::pair<time_point, time_point>> timePoints;

    /** Named occurrence counters: tag → count. Reserved to 16 entries. */
    boost::container::flat_map<std::string, std::size_t> counts;

    /**
     * Per-pass liquidity data for one complete `flow()` execution.
     *
     * The outer liquidity-selection loop in `StrandFlow.h` iterates, each
     * iteration selecting the best-quality strand and routing an increment of
     * the payment through it. `PassInfo` records one data point per iteration:
     * - `in` / `out`: total amount consumed and delivered in that pass.
     * - `numActive`: number of strands still active after that pass.
     * - `liquiditySrcIn` / `liquiditySrcOut`: per-strand contributions within
     *   the pass, indexed as `[pass][strand]`.
     *
     * `nativeIn` and `nativeOut` are set at construction and drive amount
     * serialization in `FlowDebugInfo::toString()` — XRP amounts are extracted
     * via `get<XRPAmount>()`, IOU amounts via `get<IOUAmount>()`.
     */
    struct PassInfo
    {
        PassInfo() = delete;

        /**
         * Constructs a `PassInfo` for a payment whose source and destination
         * currency types are known up front.
         *
         * @param nativeIn  True if the payment's input currency is XRP.
         * @param nativeOut True if the payment's output currency is XRP.
         */
        PassInfo(bool nativeIn, bool nativeOut) : nativeIn(nativeIn), nativeOut(nativeOut)
        {
        }

        /** True if the payment's source currency is XRP. */
        bool const nativeIn;

        /** True if the payment's destination currency is XRP. */
        bool const nativeOut;

        /** Total amount consumed from senders, one entry per pass. */
        std::vector<EitherAmount> in;

        /** Total amount delivered to receivers, one entry per pass. */
        std::vector<EitherAmount> out;

        /** Number of active strands remaining after each pass. */
        std::vector<size_t> numActive;

        /**
         * Per-strand input amounts, indexed as `[pass][strand]`.
         *
         * Each inner vector is opened by `newLiquidityPass()` and populated
         * by `pushLiquiditySrc()`.
         */
        std::vector<std::vector<EitherAmount>> liquiditySrcIn;

        /**
         * Per-strand output amounts, indexed as `[pass][strand]`.
         *
         * Each inner vector is opened by `newLiquidityPass()` and populated
         * by `pushLiquiditySrc()`.
         */
        std::vector<std::vector<EitherAmount>> liquiditySrcOut;

        /**
         * Reserves capacity in all parallel vectors to avoid repeated
         * reallocations during the liquidity loop.
         *
         * @param s Expected number of passes.
         */
        void
        reserve(size_t s)
        {
            in.reserve(s);
            out.reserve(s);
            liquiditySrcIn.reserve(s);
            liquiditySrcOut.reserve(s);
            numActive.reserve(s);
        }

        /**
         * Returns the number of passes recorded so far (equal to `in.size()`).
         */
        [[nodiscard]] size_t
        size() const
        {
            return in.size();
        }

        /**
         * Records the aggregate result of one liquidity pass.
         *
         * Appends one entry to `in`, `out`, and `numActive`. Must be called
         * once per outer-loop iteration after all per-strand contributions for
         * that iteration have been pushed via `pushLiquiditySrc()`.
         *
         * @param inAmt   Total amount consumed from senders in this pass.
         * @param outAmt  Total amount delivered to receivers in this pass.
         * @param active  Number of strands still active after this pass.
         */
        void
        pushBack(EitherAmount const& inAmt, EitherAmount const& outAmt, std::size_t active)
        {
            in.push_back(inAmt);
            out.push_back(outAmt);
            numActive.push_back(active);
        }

        /**
         * Records the contribution of a single strand within the current pass.
         *
         * Appends to the back of `liquiditySrcIn` and `liquiditySrcOut`.
         * `newLiquidityPass()` must be called before the first `pushLiquiditySrc()`
         * call for each pass; an assertion fires if the inner vectors are empty.
         *
         * @param eIn   Amount consumed from this strand's source in this pass.
         * @param eOut  Amount delivered by this strand in this pass.
         */
        void
        pushLiquiditySrc(EitherAmount const& eIn, EitherAmount const& eOut)
        {
            XRPL_ASSERT(
                !liquiditySrcIn.empty(),
                "xrpl::path::detail::FlowDebugInfo::pushLiquiditySrc : "
                "non-empty liquidity source");
            liquiditySrcIn.back().push_back(eIn);
            liquiditySrcOut.back().push_back(eOut);
        }

        /**
         * Opens a new inner vector in `liquiditySrcIn` and `liquiditySrcOut`
         * before the start of each liquidity pass.
         *
         * The capacity of the new inner vectors is hinted from the active-strand
         * count of the previous pass (or 16 for the first pass), minimizing
         * reallocations within the pass.
         */
        void
        newLiquidityPass()
        {
            auto const s = liquiditySrcIn.size();
            size_t const r = !numActive.empty() ? numActive.back() : 16;
            liquiditySrcIn.resize(s + 1);
            liquiditySrcIn.back().reserve(r);
            liquiditySrcOut.resize(s + 1);
            liquiditySrcOut.back().reserve(r);
        }
    };

    /** Accumulated per-pass data for this payment execution. */
    PassInfo passInfo;

    FlowDebugInfo() = delete;

    /**
     * Constructs a `FlowDebugInfo` for a payment whose source and destination
     * currency types are known up front.
     *
     * Pre-reserves the flat maps to 16 entries and `passInfo` to 64 passes to
     * avoid repeated reallocations during typical payment executions.
     *
     * @param nativeIn  True if the payment's input currency is XRP.
     * @param nativeOut True if the payment's output currency is XRP.
     */
    FlowDebugInfo(bool nativeIn, bool nativeOut) : passInfo(nativeIn, nativeOut)
    {
        timePoints.reserve(16);
        counts.reserve(16);
        passInfo.reserve(64);
    }

    /**
     * Returns the wall-clock duration of the named timing block.
     *
     * @param tag  The name used when the block was opened with `timeBlock()`.
     * @return     Elapsed time as a `std::chrono::duration<double>` (seconds).
     *     Returns zero and triggers `UNREACHABLE` if `tag` was never recorded.
     */
    [[nodiscard]] auto
    duration(std::string const& tag) const
    {
        auto i = timePoints.find(tag);
        if (i == timePoints.end())
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::path::detail::FlowDebugInfo::duration : timepoint not "
                "found");
            return std::chrono::duration<double>(0);
            // LCOV_EXCL_STOP
        }
        auto const& t = i->second;
        return std::chrono::duration_cast<std::chrono::duration<double>>(t.second - t.first);
    }

    /**
     * Returns the current occurrence count for a named counter.
     *
     * @param tag  Counter name previously incremented via `inc()` or set via
     *     `setCount()`.
     * @return     The stored count, or 0 if the tag has never been recorded.
     */
    [[nodiscard]] std::size_t
    count(std::string const& tag) const
    {
        auto i = counts.find(tag);
        if (i == counts.end())
            return 0;
        return i->second;
    }

    /**
     * Times the duration of a lexical scope via RAII.
     *
     * Returns a `Stopper` whose constructor records `clock::now()` as both the
     * start and the initial end of `tag`'s entry in `timePoints`. The
     * destructor overwrites the end with a fresh `clock::now()`, capturing the
     * elapsed time when the `Stopper` goes out of scope:
     *
     * ```cpp
     * auto _ = flowDebugInfo->timeBlock("main");
     * // ... timed work ...
     * // duration captured automatically on scope exit
     * ```
     *
     * The returned `Stopper` is move-constructible (required for RVO) but not
     * copy-constructible, and it holds a raw pointer to this `FlowDebugInfo`.
     * The parent must outlive the `Stopper`.
     *
     * @param name  Unique tag for this timing interval.
     * @return      A `Stopper` RAII guard; keep it in scope for the duration to
     *     be measured.
     */
    auto
    timeBlock(std::string name)
    {
        struct Stopper
        {
            std::string tag;
            FlowDebugInfo* info;
            Stopper(std::string name, FlowDebugInfo& pi) : tag(std::move(name)), info(&pi)
            {
                auto const start = FlowDebugInfo::clock::now();
                info->timePoints.emplace(tag, std::make_pair(start, start));
            }
            ~Stopper()
            {
                auto const end = FlowDebugInfo::clock::now();
                info->timePoints[tag].second = end;
            }
            Stopper(Stopper&&) = default;
        };
        return Stopper(std::move(name), *this);
    }

    /**
     * Increments the occurrence count for a named counter.
     *
     * @param tag  Counter name to increment.
     *
     * @note Contains a latent bug: when `tag` is absent from `counts`, the
     *     map inserts `counts[tag] = 1` and then attempts `++i->second` using
     *     the pre-insertion iterator `i`. For `flat_map`'s vector-backed
     *     storage, insertion can relocate elements, invalidating `i` and
     *     producing undefined behavior on the first use of any new tag. Because
     *     `FlowDebugInfo` is only used in diagnostic (non-production) code
     *     paths, this has not caused observable failures, but callers should
     *     prefer `setCount()` for new tags.
     */
    void
    inc(std::string const& tag)
    {
        auto i = counts.find(tag);
        if (i == counts.end())
        {
            counts[tag] = 1;
        }
        ++i->second;
    }

    /**
     * Sets the occurrence count for a named counter to an explicit value.
     *
     * Overwrites any existing count for `tag`.
     *
     * @param tag  Counter name.
     * @param c    Value to assign.
     */
    void
    setCount(std::string const& tag, std::size_t c)
    {
        counts[tag] = c;
    }

    /**
     * Returns the total number of liquidity passes recorded.
     *
     * Equal to `passInfo.size()` (the number of entries in `passInfo.in`).
     */
    [[nodiscard]] std::size_t
    passCount() const
    {
        return passInfo.size();
    }

    /**
     * Records the aggregate result of one liquidity pass.
     *
     * Delegates to `passInfo.pushBack()`. Called once per outer-loop iteration
     * in `StrandFlow.h` after all per-strand contributions have been pushed.
     *
     * @param in             Total amount consumed from senders in this pass.
     * @param out            Total amount delivered to receivers in this pass.
     * @param activeStrands  Number of strands still active after this pass.
     */
    void
    pushPass(EitherAmount const& in, EitherAmount const& out, std::size_t activeStrands)
    {
        passInfo.pushBack(in, out, activeStrands);
    }

    /**
     * Records the contribution of a single strand within the current pass.
     *
     * Delegates to `passInfo.pushLiquiditySrc()`. Called once per strand
     * selection inside the outer loop of `StrandFlow.h`.
     *
     * @param in   Amount consumed from this strand's source.
     * @param out  Amount delivered by this strand.
     */
    void
    pushLiquiditySrc(EitherAmount const& in, EitherAmount const& out)
    {
        passInfo.pushLiquiditySrc(in, out);
    }

    /**
     * Opens a new per-strand contribution slot for the upcoming pass.
     *
     * Must be called once at the start of each outer-loop iteration in
     * `StrandFlow.h`, before any `pushLiquiditySrc()` calls for that pass.
     * Delegates to `passInfo.newLiquidityPass()`.
     */
    void
    newLiquidityPass()
    {
        passInfo.newLiquidityPass();
    }

    /**
     * Serializes the accumulated telemetry to a human-readable string.
     *
     * Always emits the total wall-clock duration of the `"main"` timed block
     * and the number of passes. When `writePassInfo` is true, also emits the
     * full sequence of per-pass in/out amounts, active strand counts, and
     * per-strand liquidity arrays.
     *
     * Format (pass info omitted when `writePassInfo` is false):
     * ```
     * duration: <seconds>, pass_count: <N>[, in_pass: [...], out_pass: [...],
     *     num_active: [...][, l_src_in: [[...|...]], l_src_out: [[...|...]]]]
     * ```
     * Outer delimiter between passes is `;`; inner delimiter between strands
     * within a pass is `|`.
     *
     * Amount serialization branches on `passInfo.nativeIn`/`nativeOut`: XRP
     * amounts use `get<XRPAmount>()`, IOU amounts use `get<IOUAmount>()`.
     *
     * @param writePassInfo  If true, include the full per-pass breakdown.
     * @return               Formatted diagnostic string suitable for logging.
     */
    [[nodiscard]] std::string
    toString(bool writePassInfo) const
    {
        std::ostringstream ostr;

        auto const d = duration("main");

        ostr << "duration: " << d.count() << ", pass_count: " << passCount();

        if (writePassInfo)
        {
            auto writeList = [&ostr](auto const& vals, auto&& fun, char delim = ';') {
                ostr << '[';
                if (!vals.empty())
                {
                    ostr << fun(vals[0]);
                    for (size_t i = 1, e = vals.size(); i < e; ++i)
                        ostr << delim << fun(vals[i]);
                }
                ostr << ']';
            };
            auto writeXrpAmtList = [&writeList](
                                       std::vector<EitherAmount> const& amts, char delim = ';') {
                auto getVal = [](EitherAmount const& a) -> std::string {
                    return xrpl::to_string(a.get<XRPAmount>());
                };
                writeList(amts, getVal, delim);
            };
            auto writeIouAmtList = [&writeList](
                                       std::vector<EitherAmount> const& amts, char delim = ';') {
                auto getVal = [](EitherAmount const& a) -> std::string {
                    return xrpl::to_string(a.get<IOUAmount>());
                };
                writeList(amts, getVal, delim);
            };
            auto writeIntList = [&writeList](std::vector<size_t> const& vals, char delim = ';') {
                // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
                auto getVal = [](size_t const& v) -> size_t const& { return v; };
                writeList(vals, getVal);
            };
            auto writeNestedIouAmtList =
                [&ostr, &writeIouAmtList](std::vector<std::vector<EitherAmount>> const& amts) {
                    ostr << '[';
                    if (!amts.empty())
                    {
                        writeIouAmtList(amts[0], '|');
                        for (size_t i = 1, e = amts.size(); i < e; ++i)
                        {
                            ostr << ';';
                            writeIouAmtList(amts[i], '|');
                        }
                    }
                    ostr << ']';
                };
            auto writeNestedXrpAmtList =
                [&ostr, &writeXrpAmtList](std::vector<std::vector<EitherAmount>> const& amts) {
                    ostr << '[';
                    if (!amts.empty())
                    {
                        writeXrpAmtList(amts[0], '|');
                        for (size_t i = 1, e = amts.size(); i < e; ++i)
                        {
                            ostr << ';';
                            writeXrpAmtList(amts[i], '|');
                        }
                    }
                    ostr << ']';
                };

            ostr << ", in_pass: ";
            if (passInfo.nativeIn)
            {
                writeXrpAmtList(passInfo.in);
            }
            else
            {
                writeIouAmtList(passInfo.in);
            }
            ostr << ", out_pass: ";
            if (passInfo.nativeOut)
            {
                writeXrpAmtList(passInfo.out);
            }
            else
            {
                writeIouAmtList(passInfo.out);
            }
            ostr << ", num_active: ";
            writeIntList(passInfo.numActive);
            if (!passInfo.liquiditySrcIn.empty() && !passInfo.liquiditySrcIn.back().empty())
            {
                ostr << ", l_src_in: ";
                if (passInfo.nativeIn)
                {
                    writeNestedXrpAmtList(passInfo.liquiditySrcIn);
                }
                else
                {
                    writeNestedIouAmtList(passInfo.liquiditySrcIn);
                }
                ostr << ", l_src_out: ";
                if (passInfo.nativeOut)
                {
                    writeNestedXrpAmtList(passInfo.liquiditySrcOut);
                }
                else
                {
                    writeNestedIouAmtList(passInfo.liquiditySrcOut);
                }
            }
        }

        return ostr.str();
    }
};

/**
 * Formats a single trust-line balance change entry into an output stream.
 *
 * Writes the element as `[sender|receiver|currency|amount]`, where the key
 * is a `(AccountID, AccountID, Currency)` tuple and the value is the net
 * `STAmount` change observed in a `PaymentSandbox`.
 *
 * @param ostr  Destination stream.
 * @param elem  A map entry from `PaymentSandbox::balanceChanges()`.
 */
inline void
writeDiffElement(
    std::ostringstream& ostr,
    std::pair<std::tuple<AccountID, AccountID, Currency>, STAmount> const& elem)
{
    using namespace std;
    auto const k = elem.first;
    auto const v = elem.second;
    ostr << '[' << get<0>(k) << '|' << get<1>(k) << '|' << get<2>(k) << '|' << v << ']';
};

/**
 * Serializes a range of trust-line balance change entries to an output stream.
 *
 * Writes the full range as `[elem0;elem1;...]` using `writeDiffElement()` for
 * each entry. Used by `balanceDiffsToString()` to produce a compact, machine-
 * readable audit trail of every trust-line mutation caused by a payment.
 *
 * @tparam Iter  Forward iterator over `(key, STAmount)` pairs as produced by
 *     `PaymentSandbox::balanceChanges()`.
 * @param ostr   Destination stream.
 * @param begin  Start of the range.
 * @param end    One past the end of the range.
 */
template <class Iter>
void
writeDiffs(std::ostringstream& ostr, Iter begin, Iter end)
{
    ostr << '[';
    if (begin != end)
    {
        writeDiffElement(ostr, *begin);
        ++begin;
    }
    for (; begin != end; ++begin)
    {
        ostr << ';';
        writeDiffElement(ostr, *begin);
    }
    ostr << ']';
};

}  // namespace xrpl::path::detail
