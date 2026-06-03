#include <xrpl/basics/Number.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

using namespace xrpl;

namespace NumberNormalizeBenchNs {

constexpr std::int64_t kBenchMin = 1'000'000'000'000'000;
constexpr std::int64_t kBenchMax = (kBenchMin * 10) - 1;

template <typename T>
void
doNotOptimize(T const& val)
{
    asm volatile("" : : "r,m"(val) : "memory");
}

std::array<std::pair<std::int64_t, int>, 12> const kTestInputs = {{
    {1, 0},
    {7, 3},
    {12345, -2},
    {999'999'999, 0},
    {999'999'999'999, 5},
    {kBenchMin, 0},
    {kBenchMax, -3},
    {kBenchMax + 1, 0},
    {1'234'567'890'123'456, 0},
    {99'999'999'999'999'999, 0},
    {1'234'567'890'123'456'789, 0},
    {static_cast<std::int64_t>(9'000'000'000'000'000'000ull), 0},
}};

inline std::pair<std::int64_t, int>
twoPassNormalize(std::int64_t mantissa, int exponent)
{
    Number const v{mantissa, exponent};
    return v.normalizeToRange<kBenchMin, kBenchMax>();
}

inline std::pair<std::int64_t, int>
singlePassNormalize(std::int64_t mantissa, int exponent)
{
    return Number::normalizeToRange<kBenchMin, kBenchMax>(mantissa, exponent);
}

constexpr int kWarmupIterations = 100'000;
constexpr int kBenchIterations = 5'000'000;

}  // namespace NumberNormalizeBenchNs

using namespace NumberNormalizeBenchNs;

TEST(NumberNormalizeBench, SinglePassVsTwoPassPerformance)
{
    for (int i = 0; i < kWarmupIterations; ++i)
    {
        for (auto const& [m, e] : kTestInputs)
        {
            doNotOptimize(twoPassNormalize(m, e));
            doNotOptimize(singlePassNormalize(m, e));
        }
    }

    auto const twoStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kBenchIterations; ++i)
    {
        for (auto const& [m, e] : kTestInputs)
        {
            doNotOptimize(twoPassNormalize(m, e));
        }
    }
    auto const twoEnd = std::chrono::steady_clock::now();

    auto const oneStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kBenchIterations; ++i)
    {
        for (auto const& [m, e] : kTestInputs)
        {
            doNotOptimize(singlePassNormalize(m, e));
        }
    }
    auto const oneEnd = std::chrono::steady_clock::now();

    auto const twoNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(twoEnd - twoStart).count();
    auto const oneNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(oneEnd - oneStart).count();

    double const twoPerCall = static_cast<double>(twoNs) / (kBenchIterations * kTestInputs.size());
    double const onePerCall = static_cast<double>(oneNs) / (kBenchIterations * kTestInputs.size());
    double const speedup = twoPerCall / onePerCall;

    std::cout << "\n=== Single-Pass vs Two-Pass Normalize ===\n";
    std::cout << "Iterations: " << kBenchIterations << " x " << kTestInputs.size()
              << " inputs = " << (kBenchIterations * kTestInputs.size()) << " calls\n";
    std::cout << "Two-pass (old):    " << twoPerCall << " ns/call (" << twoNs << " ns total)\n";
    std::cout << "Single-pass (new): " << onePerCall << " ns/call (" << oneNs << " ns total)\n";
    std::cout << "Speedup: " << speedup << "x\n";
    std::cout << "==========================================\n\n";

    if (speedup > 1.0)
    {
        std::cout << "Single-pass is FASTER by " << ((speedup - 1.0) * 100.0) << "%\n";
    }
    else
    {
        std::cout << "Two-pass is faster by " << ((1.0 / speedup - 1.0) * 100.0) << "%\n";
    }
}

TEST(NumberNormalizeBench, SingleVsTwoPassBreakdown)
{
    struct InputCategory
    {
        char const* name;
        std::int64_t mantissa;
        int exponent;
    };

    std::array<InputCategory, 6> const categories = {{
        {.name = "1 (far from range)", .mantissa = 1, .exponent = 0},
        {.name = "12345 (moderate)", .mantissa = 12345, .exponent = 0},
        {.name = "10^12 (close)", .mantissa = 1'000'000'000'000, .exponent = 0},
        {.name = "10^15 (in range)", .mantissa = kBenchMin, .exponent = 0},
        {.name = "10^16 (1 over)", .mantissa = kBenchMax + 1, .exponent = 0},
        {.name = "10^18 (far over)", .mantissa = 1'234'567'890'123'456'789, .exponent = 0},
    }};

    constexpr int kIters = 10'000'000;

    std::cout << "\n=== Single vs Two Pass: Per-Input Breakdown ===\n";
    std::cout << "Input                    | 2-pass ns | 1-pass ns | Speedup\n";
    std::cout << "-------------------------|-----------|-----------|--------\n";

    for (auto const& cat : categories)
    {
        for (int i = 0; i < 100'000; ++i)
        {
            doNotOptimize(twoPassNormalize(cat.mantissa, cat.exponent));
            doNotOptimize(singlePassNormalize(cat.mantissa, cat.exponent));
        }

        auto const ts = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i)
        {
            doNotOptimize(twoPassNormalize(cat.mantissa, cat.exponent));
        }
        auto const te = std::chrono::steady_clock::now();

        auto const os = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i)
        {
            doNotOptimize(singlePassNormalize(cat.mantissa, cat.exponent));
        }
        auto const oe = std::chrono::steady_clock::now();

        double const twoNs =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(te - ts).count()) /
            kIters;
        double const oneNs =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(oe - os).count()) /
            kIters;
        double const speedup = twoNs / oneNs;

        printf("%-25s| %9.2f | %9.2f | %.2fx\n", cat.name, twoNs, oneNs, speedup);
    }
    std::cout << "================================================\n\n";
}
