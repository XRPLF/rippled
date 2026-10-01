// Single-operation benchmarks for xrpl::Number and the decimal floating-point
// types it is being compared against. See docs/NumberDecimalBenchmark.md.
//
// Benchmark names are "<operation>/<dataset>/<type>", e.g.
// "mul/full/Number.Large330", so they can be filtered with
// --benchmark_filter='^mul/.*/(Number|BoostDecimalFast128)'.

#include <xrpl/basics/Number.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/number/BoostDecimal.h>
#include <benchmarks/libxrpl/number/Double.h>
#include <benchmarks/libxrpl/number/Number128.h>
#include <benchmarks/libxrpl/number/NumberLike.h>
#include <benchmarks/libxrpl/number/Operands.h>

#ifdef XRPL_BENCH_MPDECIMAL
#include <benchmarks/libxrpl/number/MpDecimal.h>
#endif
#ifdef XRPL_BENCH_INTEL_DFP
#include <benchmarks/libxrpl/number/IntelBid.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::number_bench {
namespace {

constexpr std::uint64_t kSeedLhs = 0x5eed'0001;
constexpr std::uint64_t kSeedRhs = 0x5eed'0002;

/**
 * Environment for types that need no per-benchmark setup.
 */
struct NoEnv
{
};

/**
 * Runs Number under a specific mantissa scale. Operands are materialized after
 * the guard is in place, so they are normalized for that scale.
 */
template <MantissaRange::MantissaScale Scale>
struct NumberScaleEnv
{
    NumberMantissaScaleGuard guard{Scale};
};

/**
 * Independent operations over a batch: measures throughput.
 */
template <NumberLike T, class Env, class Op>
auto
binaryThroughput(std::vector<RawOperand> lhs, std::vector<RawOperand> rhs, Op op)
{
    return [lhs = std::move(lhs), rhs = std::move(rhs), op](benchmark::State& state) {
        [[maybe_unused]] Env const env{};
        auto const a = materialize<T>(lhs);
        auto const b = materialize<T>(rhs);
        for (auto _ : state)
        {
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                auto r = op(a[i], b[i]);
                benchmark::DoNotOptimize(r);
            }
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * a.size()));
    };
}

template <NumberLike T, class Env, class Op>
auto
unaryThroughput(std::vector<RawOperand> operands, Op op)
{
    return [operands = std::move(operands), op](benchmark::State& state) {
        [[maybe_unused]] Env const env{};
        auto const a = materialize<T>(operands);
        for (auto _ : state)
        {
            for (auto const& x : a)
            {
                auto r = op(x);
                benchmark::DoNotOptimize(r);
            }
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * a.size()));
    };
}

/**
 * Construction from (mantissa, exponent), the path every STNumber/STAmount read takes.
 */
template <NumberLike T, class Env>
auto
constructThroughput(std::vector<RawOperand> operands)
{
    return [operands = std::move(operands)](benchmark::State& state) {
        [[maybe_unused]] Env const env{};
        for (auto _ : state)
        {
            for (auto const& raw : operands)
            {
                T x{raw.mantissa, raw.exponent};
                benchmark::DoNotOptimize(x);
            }
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * operands.size()));
    };
}

/**
 * Each result feeds the next operation: measures latency.
 */
template <NumberLike T, class Env, class Op>
auto
chainLatency(std::vector<RawOperand> operands, Op op)
{
    return [operands = std::move(operands), op](benchmark::State& state) {
        [[maybe_unused]] Env const env{};
        auto const a = materialize<T>(operands);
        for (auto _ : state)
        {
            T acc{std::int64_t{1}};
            for (auto const& x : a)
                acc = op(acc, x);
            benchmark::DoNotOptimize(acc);
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * a.size()));
    };
}

template <class T, class Fn>
void
add(std::string_view op, std::string_view data, std::string_view subject, Fn fn)
{
    benchmark::RegisterBenchmark(
        std::format("{}/{}/{}", op, data, subject), [fn = std::move(fn)](benchmark::State& state) {
            fn(state);
            state.counters["sizeof"] = sizeof(T);
        });
}

template <NumberLike T, class Env>
void
registerSubject(std::string_view subject)
{
    constexpr auto kGeneral =
        std::to_array({Dataset::Full, Dataset::Iou, Dataset::Drops, Dataset::Mpt});

    for (auto const d : kGeneral)
    {
        auto const name = toString(d);
        auto const lhs = makeRaw(d, kSeedLhs);
        auto const rhs = makeRaw(d, kSeedRhs);

        add<T>("add", name, subject, binaryThroughput<T, Env>(lhs, rhs, std::plus<>{}));
        add<T>("sub", name, subject, binaryThroughput<T, Env>(lhs, rhs, std::minus<>{}));
        add<T>("mul", name, subject, binaryThroughput<T, Env>(lhs, rhs, std::multiplies<>{}));
        add<T>("div", name, subject, binaryThroughput<T, Env>(lhs, rhs, std::divides<>{}));
        add<T>("lt", name, subject, binaryThroughput<T, Env>(lhs, rhs, std::less<>{}));
        add<T>("construct", name, subject, constructThroughput<T, Env>(lhs));
        add<T>("to_string", name, subject, unaryThroughput<T, Env>(lhs, [](T const& x) {
                   return to_string(x);
               }));
        add<T>("root2", name, subject, unaryThroughput<T, Env>(lhs, [](T const& x) {
                   return root2(x);
               }));
    }

    // Only integer-valued datasets fit in int64 without overflow.
    for (auto const d : {Dataset::Drops, Dataset::Mpt})
    {
        add<T>(
            "to_int64",
            toString(d),
            subject,
            unaryThroughput<T, Env>(
                makeRaw(d, kSeedLhs), [](T const& x) { return static_cast<std::int64_t>(x); }));
    }

    for (int const gap : {0, 1, 5, 18})
    {
        auto [lhs, rhs] = makeExponentGapPairs(gap, kSeedLhs);
        add<T>(
            std::format("add_gap{}", gap),
            toString(Dataset::Full),
            subject,
            binaryThroughput<T, Env>(std::move(lhs), std::move(rhs), std::plus<>{}));
    }

    {
        auto [lhs, rhs] = makeCancellationPairs(kSeedLhs);
        add<T>(
            "sub_cancel",
            toString(Dataset::Full),
            subject,
            binaryThroughput<T, Env>(std::move(lhs), std::move(rhs), std::minus<>{}));
    }

    // 12 and 360: monthly payments for one year and for thirty years.
    for (unsigned const n : {12u, 360u})
    {
        add<T>(
            std::format("power{}", n),
            toString(Dataset::Rate),
            subject,
            unaryThroughput<T, Env>(
                makeRaw(Dataset::Rate, kSeedLhs), [n](T const& x) { return power(x, n); }));
    }

    add<T>(
        "add_chain",
        toString(Dataset::Full),
        subject,
        chainLatency<T, Env>(makeRaw(Dataset::Full, kSeedLhs), std::plus<>{}));
    add<T>(
        "mul_chain",
        toString(Dataset::Rate),
        subject,
        chainLatency<T, Env>(makeRaw(Dataset::Rate, kSeedLhs), std::multiplies<>{}));
    add<T>(
        "div_chain",
        toString(Dataset::Rate),
        subject,
        chainLatency<T, Env>(makeRaw(Dataset::Rate, kSeedLhs), std::divides<>{}));
}

[[maybe_unused]] bool const kRegistered = [] {
    using enum MantissaRange::MantissaScale;
    registerSubject<Number, NumberScaleEnv<Small>>("Number.Small");
    registerSubject<Number, NumberScaleEnv<LargeLegacy>>("Number.LargeLegacy");
    registerSubject<Number, NumberScaleEnv<Large330>>("Number.Large330");
    registerSubject<BoostDecimal64, NoEnv>("BoostDecimal64");
    registerSubject<BoostDecimalFast64, NoEnv>("BoostDecimalFast64");
    registerSubject<BoostDecimal128, NoEnv>("BoostDecimal128");
    registerSubject<BoostDecimalFast128, NoEnv>("BoostDecimalFast128");
    registerSubject<Double, NoEnv>("Double");
#ifdef XRPL_BENCH_MPDECIMAL
    registerSubject<MpDecimal19, NoEnv>("MpDecimal19");
    registerSubject<MpDecimal34, NoEnv>("MpDecimal34");
    registerSubject<MpDecimal38, NoEnv>("MpDecimal38");
#endif
#ifdef XRPL_BENCH_INTEL_DFP
    registerSubject<IntelBid64, NoEnv>("IntelBid64");
    registerSubject<IntelBid128, NoEnv>("IntelBid128");
#endif
#ifdef XRPL_BENCH_HAS_NUMBER128
    registerSubject<Number128, NoEnv>("Number128");
#endif
    return true;
}();

}  // namespace
}  // namespace xrpl::number_bench
