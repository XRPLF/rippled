#include <xrpl/protocol/Feature.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "amendment_enabled";

std::string const&
benchAmendment()
{
    static auto const kValue = std::string{"TokenEscrow"};
    return kValue;
}

void
isAmendmentEnabledByIdImpl(benchmark::State& state)
{
    auto const feature = getRegisteredFeature(benchAmendment());
    if (!feature.has_value())
    {
        state.SkipWithError("the benchmarked amendment is not registered");
        return;
    }
    auto const id = *feature;

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&id](auto& host) { return host.isAmendmentEnabled(id); });
}
BENCHMARK(isAmendmentEnabledByIdImpl)->UseManualTime()->Iterations(kBenchIterations);

void
isAmendmentEnabledByNameImpl(benchmark::State& state)
{
    // The gap over the id form is precisely what the shared price of 100 asserts does not exist.
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.isAmendmentEnabled(std::string_view{benchAmendment()}); });
}
BENCHMARK(isAmendmentEnabledByNameImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
