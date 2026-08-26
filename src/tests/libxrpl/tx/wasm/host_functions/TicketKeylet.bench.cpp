#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
ticketKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"ticket_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.ticketKeylet(benchAlice().id(), kBenchSeq); });
}
BENCHMARK(ticketKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
