// number_diff: measures how far Number's results are from correctly rounded
// results. See docs/NumberDecimalBenchmark.md, "Divergence harness".
//
// For every operation, the operands are converted exactly into a 96-digit
// mpdecimal value and the operation is evaluated there (correctly rounded at
// 96 digits). That near-exact result is then rounded onto the subject type's
// own grid of representable values in the active rounding mode, and compared
// with what the subject actually returned.
//
// Rounding a 96-digit result again creates a false tie only if digits 20-96
// of a non-terminating result happen to round to exactly 5000...0, which
// will not occur at these sample sizes.
//
// Usage: xrpl.bench.number_diff [--samples N] [--csv]

#include <xrpl/basics/Number.h>

#include <benchmarks/libxrpl/number/BoostDecimal.h>
#include <benchmarks/libxrpl/number/MpDecimal.h>
#include <benchmarks/libxrpl/number/Number128.h>
#include <benchmarks/libxrpl/number/NumberLike.h>
#include <benchmarks/libxrpl/number/Operands.h>

#ifdef XRPL_BENCH_INTEL_DFP
#include <benchmarks/libxrpl/number/IntelBid.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::number_bench {
namespace {

using Exact = MpDecimal<96>;

constexpr std::uint64_t kSeedLhs = 0xd1ff'0001;
constexpr std::uint64_t kSeedRhs = 0xd1ff'0002;

/**
 * The set of values a type can represent, as far as rounding is concerned.
 * `int64Cap` models Number's large scales, whose mantissas above 2^63-1 must
 * end in 0 (see Number.h, "External Interface").
 */
struct Grid
{
    unsigned digits;
    bool int64Cap = false;
};

/**
 * Integers: the grid of conversions to int64.
 */
struct IntegerGrid
{
};

// ---- Exact arithmetic helpers ----

Exact
fromSubject(DecomposableNumber auto const& x)
{
    return Exact::fromDecomposed(decompose(x));
}

/**
 * Rounds a non-negative value to `digits` significant digits, toward (down) or away from (up)
 * zero.
 */
Exact
roundMagnitude(Exact x, unsigned digits, bool up)
{
    mpd_context_t ctx;
    mpd_maxcontext(&ctx);
    ctx.prec = digits;
    ctx.round = up ? MPD_ROUND_UP : MPD_ROUND_DOWN;
    ctx.traps = 0;
    std::uint32_t status = 0;
    mpd_qfinalize(x.get(), &ctx, &status);
    return x;
}

/**
 * Rounds a non-negative value to an integer, toward (down) or away from (up) zero.
 */
Exact
roundMagnitudeToInteger(Exact const& x, bool up)
{
    mpd_context_t ctx;
    mpd_maxcontext(&ctx);
    ctx.round = up ? MPD_ROUND_UP : MPD_ROUND_DOWN;
    ctx.traps = 0;
    std::uint32_t status = 0;
    Exact result;
    mpd_qround_to_int(result.get(), x.get(), &ctx, &status);
    return result;
}

bool
isZero(Exact const& x)
{
    return mpd_iszero(x.get()) != 0;
}

bool
isNegative(Exact const& x)
{
    return mpd_isnegative(x.get()) != 0 && !isZero(x);
}

/**
 * True if q (a non-negative integer) is even.
 */
bool
isEvenInteger(Exact const& q)
{
    auto const s = to_string(q);
    auto const pos = s.find_first_of("Ee");
    // Positive exponent in scientific notation means trailing zeros.
    if (pos != std::string::npos && s[pos + 1] != '-')
        return true;
    auto const digits = s.substr(0, pos);
    return ((digits.back() - '0') % 2) == 0;
}

// ---- Candidate representable values around an exact result ----

/**
 * The representable magnitudes immediately at or below and at or above |x|.
 */
struct Bracket
{
    Exact lo;
    Exact hi;
    /**
     * Grid spacing at lo: lo / spacing is lo's integer coefficient.
     */
    Exact loSpacing;
    /**
     * lo is 2^63-1 and hi is its successor 2^63+3: the two candidates straddle Number's cap.
     */
    bool straddlesCap = false;
};

Bracket
bracket(Exact const& magnitude, Grid grid)
{
    auto const adjusted = static_cast<int>(mpd_adjexp(magnitude.get()));
    auto lo = roundMagnitude(magnitude, grid.digits, false);
    auto hi = roundMagnitude(magnitude, grid.digits, true);
    Exact loSpacing{1, adjusted - static_cast<int>(grid.digits) + 1};
    bool straddlesCap = false;
    if (grid.int64Cap)
    {
        // Above cap, only every tenth 19-digit value is representable.
        Exact const cap{static_cast<std::int64_t>(Number::kMaxRep), adjusted - 18};
        if (hi > cap)
            hi = roundMagnitude(magnitude, 18, true);
        if (lo > cap)
        {
            auto const lo18 = roundMagnitude(magnitude, 18, false);
            if (lo18 < cap)
            {
                lo = cap;
                straddlesCap = true;
            }
            else
            {
                lo = lo18;
                loSpacing = Exact{1, adjusted - 17};
            }
        }
    }
    return {std::move(lo), std::move(hi), std::move(loSpacing), straddlesCap};
}

/**
 * The correctly rounded magnitude, or nullopt if two candidates are equally valid (a cusp tie).
 */
std::optional<Exact>
choose(Exact const& magnitude, Bracket const& b, bool negative, Number::RoundingMode mode)
{
    using enum Number::RoundingMode;
    if (b.lo == b.hi)
        return b.lo;
    switch (mode)
    {
        case TowardsZero:
            return b.lo;
        case Downward:
            return negative ? b.hi : b.lo;
        case Upward:
            return negative ? b.lo : b.hi;
        case ToNearest:
            break;
    }
    auto const below = magnitude - b.lo;
    auto const above = b.hi - magnitude;
    if (below < above)
        return b.lo;
    if (above < below)
        return b.hi;
    // Tie at the cap: 2^63-1 and 2^63+3 are both odd, so "even" is undefined.
    if (b.straddlesCap)
        return std::nullopt;
    return isEvenInteger(b.lo / b.loSpacing) ? b.lo : b.hi;
}

// ---- Tallies ----

struct Tally
{
    std::size_t samples = 0;
    /**
     * Exactly the correctly rounded result.
     */
    std::size_t correct = 0;
    /**
     * The other neighbor of the exact result: rounded in the wrong direction.
     */
    std::size_t wrongNeighbor = 0;
    /**
     * Neither neighbor: off by more than one step.
     */
    std::size_t worse = 0;
    /**
     * Returned zero for a non-zero result.
     */
    std::size_t flushed = 0;
    /**
     * Threw instead of returning.
     */
    std::size_t threw = 0;
    /**
     * Largest |result - exact| / local grid spacing.
     */
    double maxError = 0;
};

void
classify(Tally& t, Exact const& result, Exact const& exact, Grid grid, Number::RoundingMode mode)
{
    if (isZero(exact))
    {
        ++(isZero(result) ? t.correct : t.worse);
        return;
    }
    if (isZero(result))
    {
        ++t.flushed;
        return;
    }
    bool const negative = isNegative(exact);
    auto const magnitude = abs(exact);
    auto const b = bracket(magnitude, grid);
    auto const resultMagnitude = abs(result);
    auto const chosen = choose(magnitude, b, negative, mode);

    bool const sameSign = isNegative(result) == negative;
    bool const isLo = sameSign && resultMagnitude == b.lo;
    bool const isHi = sameSign && resultMagnitude == b.hi;
    if (sameSign && (chosen ? resultMagnitude == *chosen : (isLo || isHi)))
        ++t.correct;
    else if (isLo || isHi)
        ++t.wrongNeighbor;
    else
        ++t.worse;

    if (b.lo != b.hi)
    {
        auto const error = abs(result - exact) / (b.hi - b.lo);
        t.maxError = std::max(t.maxError, std::stod(to_string(error)));
    }
}

void
classifyInteger(Tally& t, std::int64_t result, Exact const& exact, Number::RoundingMode mode)
{
    bool const negative = isNegative(exact);
    auto const magnitude = abs(exact);
    auto const lo = roundMagnitudeToInteger(magnitude, false);
    auto const hi = roundMagnitudeToInteger(magnitude, true);
    Bracket const b{lo, hi, Exact{1}};
    auto const chosen = *choose(magnitude, b, negative, mode);
    Exact const r{result};
    auto const resultMagnitude = abs(r);
    bool const sameSign = result == 0 || (result < 0) == negative;
    if (sameSign && resultMagnitude == chosen)
        ++t.correct;
    else if (sameSign && (resultMagnitude == lo || resultMagnitude == hi))
        ++t.wrongNeighbor;
    else
        ++t.worse;
    t.maxError = std::max(t.maxError, std::stod(to_string(abs(r - exact))));
}

// ---- Running cells ----

struct Row
{
    std::string subject;
    Number::RoundingMode mode;
    std::string op;
    std::string data;
    Tally tally;
};

template <DecomposableNumber T, class BinaryOp>
Tally
runBinary(
    std::vector<RawOperand> const& lhs,
    std::vector<RawOperand> const& rhs,
    BinaryOp op,
    Grid grid,
    Number::RoundingMode mode)
{
    Tally t;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        T const a{lhs[i].mantissa, lhs[i].exponent};
        T const b{rhs[i].mantissa, rhs[i].exponent};
        auto const exact = op(fromSubject(a), fromSubject(b));
        ++t.samples;
        try
        {
            classify(t, fromSubject(T{op(a, b)}), exact, grid, mode);
        }
        catch (std::exception const&)
        {
            ++t.threw;
        }
    }
    return t;
}

template <DecomposableNumber T, class UnaryOp>
Tally
runUnary(std::vector<RawOperand> const& operands, UnaryOp op, Grid grid, Number::RoundingMode mode)
{
    Tally t;
    for (auto const& raw : operands)
    {
        T const a{raw.mantissa, raw.exponent};
        auto const exact = op(fromSubject(a));
        ++t.samples;
        try
        {
            classify(t, fromSubject(T{op(a)}), exact, grid, mode);
        }
        catch (std::exception const&)
        {
            ++t.threw;
        }
    }
    return t;
}

template <DecomposableNumber T>
Tally
runToInt64(std::vector<RawOperand> const& operands, Number::RoundingMode mode)
{
    Tally t;
    for (auto const& raw : operands)
    {
        T const a{raw.mantissa, raw.exponent};
        auto const exact = fromSubject(a);
        ++t.samples;
        try
        {
            classifyInteger(t, static_cast<std::int64_t>(a), exact, mode);
        }
        catch (std::exception const&)
        {
            ++t.threw;
        }
    }
    return t;
}

/**
 * Runs every operation and dataset for one subject type and rounding mode.
 */
template <DecomposableNumber T>
void
runSubject(
    std::string_view subject,
    Grid grid,
    Number::RoundingMode mode,
    std::size_t samples,
    std::vector<Row>& rows)
{
    auto const saved = T::setround(mode);
    auto const record = [&](std::string op, Dataset d, Tally t) {
        rows.push_back({std::string{subject}, mode, std::move(op), std::string{toString(d)}, t});
    };
    auto const recordPairs = [&](std::string op, std::string data, Tally t) {
        rows.push_back({std::string{subject}, mode, std::move(op), std::move(data), t});
    };

    auto const plus = [](auto const& x, auto const& y) { return x + y; };
    auto const minus = [](auto const& x, auto const& y) { return x - y; };
    auto const times = [](auto const& x, auto const& y) { return x * y; };
    auto const divide = [](auto const& x, auto const& y) { return x / y; };

    for (auto const d : {Dataset::Full, Dataset::Iou, Dataset::Mpt})
    {
        auto const lhs = makeRaw(d, kSeedLhs, samples);
        auto const rhs = makeRaw(d, kSeedRhs, samples);
        record("add", d, runBinary<T>(lhs, rhs, plus, grid, mode));
        record("sub", d, runBinary<T>(lhs, rhs, minus, grid, mode));
        record("mul", d, runBinary<T>(lhs, rhs, times, grid, mode));
        record("div", d, runBinary<T>(lhs, rhs, divide, grid, mode));
        record("root2", d, runUnary<T>(lhs, [](auto const& x) { return root2(x); }, grid, mode));
    }

    for (int const gap : {1, 5, 18})
    {
        auto const [lhs, rhs] = makeExponentGapPairs(gap, kSeedLhs, samples);
        recordPairs("add", std::format("gap{}", gap), runBinary<T>(lhs, rhs, plus, grid, mode));
        recordPairs("sub", std::format("gap{}", gap), runBinary<T>(lhs, rhs, minus, grid, mode));
    }
    {
        auto const [lhs, rhs] = makeCancellationPairs(kSeedLhs, samples);
        recordPairs("sub", "cancel", runBinary<T>(lhs, rhs, minus, grid, mode));
    }
    {
        auto const [lhs, rhs] = makeTiePairs(kSeedLhs, samples);
        recordPairs("add", "tie", runBinary<T>(lhs, rhs, plus, grid, mode));
    }
    {
        auto const [lhs, rhs] = makeCuspPairs(kSeedLhs, samples);
        recordPairs("add", "cusp", runBinary<T>(lhs, rhs, plus, grid, mode));
    }

    auto const rates = makeRaw(Dataset::Rate, kSeedLhs, samples);
    for (unsigned const n : {12u, 360u})
    {
        record(
            std::format("power{}", n),
            Dataset::Rate,
            runUnary<T>(rates, [n](auto const& x) { return power(x, n); }, grid, mode));
    }

    for (auto const d : {Dataset::Fraction, Dataset::Half})
        record("to_int64", d, runToInt64<T>(makeRaw(d, kSeedLhs, samples), mode));

    T::setround(saved);
}

void
print(std::vector<Row> const& rows, bool csv)
{
    if (csv)
        std::cout << "subject,mode,op,data,samples,correct,wrong_neighbor,worse,flushed,threw,max_"
                     "error\n";
    else
        std::cout << "| subject | mode | op | data | samples | correct | wrong neighbor | worse | "
                     "flushed | threw | max error |\n"
                     "|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for (auto const& r : rows)
    {
        auto const& t = r.tally;
        auto const mode = to_string(r.mode);
        auto const fmt = csv ? "{},{},{},{},{},{},{},{},{},{},{:.4g}\n"
                             : "| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {:.4g} |\n";
        std::cout << std::vformat(
            fmt,
            std::make_format_args(
                r.subject,
                mode,
                r.op,
                r.data,
                t.samples,
                t.correct,
                t.wrongNeighbor,
                t.worse,
                t.flushed,
                t.threw,
                t.maxError));
    }
}

int
run(int argc, char** argv)
{
    std::size_t samples = 10'000;
    bool csv = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view const arg = argv[i];
        if (arg == "--samples" && i + 1 < argc)
            samples = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--csv")
            csv = true;
        else
        {
            std::cerr << "usage: " << argv[0] << " [--samples N] [--csv]\n";
            return 2;
        }
    }

    using enum Number::RoundingMode;
    using enum MantissaRange::MantissaScale;
    constexpr auto kModes = std::to_array({ToNearest, TowardsZero, Downward, Upward});

    std::vector<Row> rows;
    for (auto const mode : kModes)
    {
        for (auto const scale : {Small, LargeLegacy, Large320, Large330})
        {
            NumberMantissaScaleGuard const guard{scale};
            auto const grid =
                scale == Small ? Grid{.digits = 16} : Grid{.digits = 19, .int64Cap = true};
            runSubject<Number>("Number." + to_string(scale), grid, mode, samples, rows);
        }
        // Validate the oracle and the grid logic against IEEE implementations,
        // which must be correctly rounded for + - * / and sqrt.
        runSubject<BoostDecimal64>("BoostDecimal64", Grid{.digits = 16}, mode, samples, rows);
        runSubject<BoostDecimalFast64>(
            "BoostDecimalFast64", Grid{.digits = 16}, mode, samples, rows);
        runSubject<BoostDecimal128>("BoostDecimal128", Grid{.digits = 34}, mode, samples, rows);
        runSubject<BoostDecimalFast128>(
            "BoostDecimalFast128", Grid{.digits = 34}, mode, samples, rows);
#ifdef XRPL_BENCH_INTEL_DFP
        runSubject<IntelBid64>("IntelBid64", Grid{.digits = 16}, mode, samples, rows);
        runSubject<IntelBid128>("IntelBid128", Grid{.digits = 34}, mode, samples, rows);
#endif
#ifdef XRPL_BENCH_HAS_NUMBER128
        runSubject<Number128>("Number128", Grid{.digits = Number128::kDigits}, mode, samples, rows);
#endif
    }
    print(rows, csv);
    return 0;
}

}  // namespace
}  // namespace xrpl::number_bench

int
main(int argc, char** argv)
{
    return xrpl::number_bench::run(argc, argv);
}
