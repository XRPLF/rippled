#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "trace";
constexpr std::string_view kMessage = "benchmark trace message";
constexpr std::string_view kData = "0123456789abcdef";

// The path a validator actually runs: journal pointed at a null sink.
void
traceDisabledImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.trace(kMessage, kData); });
}
BENCHMARK(traceDisabledImpl)->UseManualTime()->Iterations(kBenchIterations);

// The same call against a host whose sink records what it is given. The gap over the case above
// is the cost the flat 30 does not cover.
void
traceEnabledImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().tracingHost(); },
        [](auto& host) { return host.trace(kMessage, kData); });
}
BENCHMARK(traceEnabledImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
