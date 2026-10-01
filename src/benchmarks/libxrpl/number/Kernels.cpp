// Ledger-formula benchmarks: AMM swaps, vault share conversion, and loan
// amortization (see Kernels.h), run identically on every NumberLike type.
// See docs/NumberDecimalBenchmark.md.
//
// Benchmark names are "kernel/<kernel>/<type>". Each reports throughput and,
// when built with mpdecimal (bench_mpdecimal), accuracy counters comparing the
// type's result with the same formula evaluated at 96 digits from the type's
// own inputs, so construction rounding is excluded:
//
//   digits_min / digits_median  correct significant digits, -log10(relative
//                               error), worst case and median over the
//                               cases; 40 means exact. For loan_amortize the
//                               error is the final balance relative to the
//                               principal.
//   mismatches                  for integer results: cases that differ from
//                               the exact truncated result.

#include <benchmarks/libxrpl/number/Kernels.h>

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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::number_bench {
namespace {

constexpr std::size_t kCases = 256;
constexpr std::uint64_t kSeed = 0x6b65'726e;

/**
 * Inputs for one kernel evaluation: up to three decimal values and two
 * integer parameters (fee, rate, interval, payment count).
 */
struct KernelCase
{
    std::array<RawOperand, 3> values;
    std::array<std::uint32_t, 3> integers;
};

template <class T>
using Values = std::array<T, 3>;

// ---- Case generation ----

/**
 * A positive value with a 16-digit mantissa and magnitude 10^lo to 10^hi.
 */
RawOperand
amount(std::mt19937_64& rng, int lo, int hi)
{
    constexpr auto kE15 = static_cast<std::int64_t>(kPowerOfTen[15]);
    auto const mantissa = std::uniform_int_distribution<std::int64_t>{kE15, (kE15 * 10) - 1}(rng);
    return {mantissa, std::uniform_int_distribution<int>{lo, hi}(rng)-15};
}

/**
 * `base` scaled by a factor 10^lo to 10^hi, with a fresh mantissa.
 */
RawOperand
fractionOf(std::mt19937_64& rng, RawOperand base, int lo, int hi)
{
    auto result = amount(rng, 0, 0);
    result.exponent = base.exponent + std::uniform_int_distribution<int>{lo, hi}(rng);
    return result;
}

/**
 * Pools of 10^6 to 10^12 per side, trades of 10^-6 to 10^-1 of the pool,
 * fees of 0 to 1%.
 */
std::vector<KernelCase>
ammCases()
{
    std::mt19937_64 rng{kSeed};
    std::vector<KernelCase> cases;
    for (std::size_t i = 0; i < kCases; ++i)
    {
        auto const poolIn = amount(rng, 6, 12);
        auto const poolOut = amount(rng, 6, 12);
        // Trades are a fraction of the out pool for swap-out to stay solvent.
        auto const trade = fractionOf(rng, poolOut, -6, -2);
        auto const fee = std::uniform_int_distribution<std::uint32_t>{0, 1000}(rng);
        cases.push_back({{poolIn, poolOut, trade}, {fee, 0, 0}});
    }
    return cases;
}

/**
 * Vaults holding 10^9 to 10^17 drops-like integer assets, share supply 10^6
 * times larger (vault scale 6) within a factor of 2, but at most 10^18 since
 * MPT supply is capped at 2^63-1; deposits of 10^-6 to 10^-1 of the assets.
 */
std::vector<KernelCase>
vaultCases()
{
    std::mt19937_64 rng{kSeed + 1};
    std::vector<KernelCase> cases;
    for (std::size_t i = 0; i < kCases; ++i)
    {
        auto const assetDigits = std::uniform_int_distribution<int>{10, 17}(rng);
        auto const assetTotal = std::uniform_int_distribution<std::int64_t>{
            static_cast<std::int64_t>(kPowerOfTen[assetDigits - 1]),
            static_cast<std::int64_t>(kPowerOfTen[assetDigits]) - 1}(rng);
        auto const ratio = std::uniform_real_distribution<double>{0.5, 2.0}(rng);
        auto const shareMantissa =
            static_cast<std::int64_t>(static_cast<double>(assetTotal) * ratio) | 1;
        auto const shareExponent = std::min(6, 18 - assetDigits);
        auto const deposit = std::uniform_int_distribution<std::int64_t>{
            std::max<std::int64_t>(1, assetTotal / 1'000'000), assetTotal / 10}(rng);
        cases.push_back(
            {{RawOperand{shareMantissa, shareExponent},
              RawOperand{assetTotal, 0},
              RawOperand{deposit, 0}},
             {}});
    }
    return cases;
}

/**
 * Principal of 10^3 to 10^9, annual rates of 0.1% to 30% (in 1/10 bips),
 * monthly payments.
 */
std::vector<KernelCase>
loanCases(std::uint32_t payments)
{
    constexpr std::uint32_t kMonth = 30 * 24 * 60 * 60;
    std::mt19937_64 rng{kSeed + 2 + payments};
    std::vector<KernelCase> cases;
    for (std::size_t i = 0; i < kCases; ++i)
    {
        auto const principal = amount(rng, 3, 9);
        auto const rate = std::uniform_int_distribution<std::uint32_t>{100, 30'000}(rng);
        cases.push_back(
            {{principal, RawOperand{0, 0}, RawOperand{0, 0}}, {rate, kMonth, payments}});
    }
    return cases;
}

// ---- Kernels, adapted to the common case shape ----

auto const kAmmSwapIn = [](auto const& v, auto const& i) {
    return ammSwapIn(v[0], v[1], v[2], static_cast<std::uint16_t>(i[0]));
};
auto const kAmmSwapOut = [](auto const& v, auto const& i) {
    return ammSwapOut(v[0], v[1], v[2], static_cast<std::uint16_t>(i[0]));
};
auto const kVaultAssetsToShares = [](auto const& v, auto const&) {
    return vaultAssetsToShares(v[0], v[1], v[2]);
};
auto const kVaultSharesToAssets = [](auto const& v, auto const&) {
    // Redeem the same magnitude of shares as the deposit's asset amount.
    return vaultSharesToAssets(v[1], v[0], v[2]);
};
auto const kLoanPayment = [](auto const& v, auto const& i) {
    using T = std::decay_t<decltype(v[0])>;
    return loanPeriodicPayment(v[0], loanPeriodicRate<T>(i[0], i[1]), i[2]);
};
auto const kLoanAmortize = [](auto const& v, auto const& i) {
    using T = std::decay_t<decltype(v[0])>;
    return loanAmortize(v[0], loanPeriodicRate<T>(i[0], i[1]), i[2]);
};

// ---- Accuracy against a 96-digit evaluation ----

#ifdef XRPL_BENCH_MPDECIMAL
using Exact = MpDecimal<96>;

template <NumberLike T>
Exact
toExact(T const& x)
{
    if constexpr (DecomposableNumber<T>)
    {
        return Exact::fromDecomposed(decompose(x));
    }
    else
    {
        Exact result;
        mpd_context_t ctx;
        mpd_maxcontext(&ctx);
        std::uint32_t status = 0;
        mpd_qset_string(result.get(), to_string(x).c_str(), &ctx, &status);
        return result;
    }
}

double
correctDigits(Exact const& error, Exact const& scale)
{
    constexpr double kExact = 40;
    if (mpd_iszero(error.get()) != 0)
        return kExact;
    auto const relative = std::stod(to_string(abs(error) / abs(scale)));
    return std::min(kExact, -std::log10(relative));
}

/**
 * Sets digits_min/digits_median (or mismatches) on `state`.
 */
template <NumberLike T, class Fn>
void
measureAccuracy(
    benchmark::State& state,
    std::vector<Values<T>> const& inputs,
    std::vector<KernelCase> const& cases,
    Fn fn,
    bool relativeToFirstInput)
{
    std::vector<double> digits;
    std::size_t mismatches = 0;
    for (std::size_t c = 0; c < inputs.size(); ++c)
    {
        Values<Exact> const exactInputs{
            toExact(inputs[c][0]), toExact(inputs[c][1]), toExact(inputs[c][2])};
        auto const result = fn(inputs[c], cases[c].integers);
        auto const exact = fn(exactInputs, cases[c].integers);
        if constexpr (std::is_integral_v<std::decay_t<decltype(result)>>)
        {
            mismatches += result == exact ? 0 : 1;
        }
        else
        {
            auto const& scale = relativeToFirstInput ? exactInputs[0] : exact;
            digits.push_back(correctDigits(toExact(result) - exact, scale));
        }
    }
    if (digits.empty())
    {
        state.counters["mismatches"] = static_cast<double>(mismatches);
        return;
    }
    std::ranges::sort(digits);
    state.counters["digits_min"] = digits.front();
    state.counters["digits_median"] = digits[digits.size() / 2];
}
#endif

// ---- Benchmarks ----

struct NoEnv
{
};

template <MantissaRange::MantissaScale Scale>
struct NumberScaleEnv
{
    NumberMantissaScaleGuard guard{Scale};
};

template <NumberLike T, class Env, class Fn>
void
registerKernel(
    std::string_view kernel,
    std::string_view subject,
    std::vector<KernelCase> const& cases,
    Fn fn,
    bool relativeToFirstInput = false)
{
    benchmark::RegisterBenchmark(
        std::format("kernel/{}/{}", kernel, subject),
        [cases, fn, relativeToFirstInput](benchmark::State& state) {
            [[maybe_unused]] Env const env{};
            std::vector<Values<T>> inputs;
            inputs.reserve(cases.size());
            for (auto const& c : cases)
            {
                inputs.push_back(
                    {T{c.values[0].mantissa, c.values[0].exponent},
                     T{c.values[1].mantissa, c.values[1].exponent},
                     T{c.values[2].mantissa, c.values[2].exponent}});
            }
            for (auto _ : state)
            {
                for (std::size_t c = 0; c < inputs.size(); ++c)
                {
                    auto r = fn(inputs[c], cases[c].integers);
                    benchmark::DoNotOptimize(r);
                }
            }
            state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * inputs.size()));
#ifdef XRPL_BENCH_MPDECIMAL
            measureAccuracy<T>(state, inputs, cases, fn, relativeToFirstInput);
#else
            (void)relativeToFirstInput;
#endif
        });
}

template <NumberLike T, class Env>
void
registerSubject(std::string_view subject)
{
    static auto const kAmm = ammCases();
    static auto const kVault = vaultCases();
    static auto const kLoan12 = loanCases(12);
    static auto const kLoan360 = loanCases(360);

    registerKernel<T, Env>("amm_swap_in", subject, kAmm, kAmmSwapIn);
    registerKernel<T, Env>("amm_swap_out", subject, kAmm, kAmmSwapOut);
    registerKernel<T, Env>("vault_assets_to_shares", subject, kVault, kVaultAssetsToShares);
    registerKernel<T, Env>("vault_shares_to_assets", subject, kVault, kVaultSharesToAssets);
    registerKernel<T, Env>("loan_payment12", subject, kLoan12, kLoanPayment);
    registerKernel<T, Env>("loan_payment360", subject, kLoan360, kLoanPayment);
    registerKernel<T, Env>("loan_amortize12", subject, kLoan12, kLoanAmortize, true);
    registerKernel<T, Env>("loan_amortize360", subject, kLoan360, kLoanAmortize, true);
}

[[maybe_unused]] bool const kRegistered = [] {
    using enum MantissaRange::MantissaScale;
    registerSubject<Number, NumberScaleEnv<Small>>("Number.Small");
    registerSubject<Number, NumberScaleEnv<Large330>>("Number.Large330");
    registerSubject<BoostDecimal64, NoEnv>("BoostDecimal64");
    registerSubject<BoostDecimal128, NoEnv>("BoostDecimal128");
    registerSubject<Double, NoEnv>("Double");
#ifdef XRPL_BENCH_MPDECIMAL
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
