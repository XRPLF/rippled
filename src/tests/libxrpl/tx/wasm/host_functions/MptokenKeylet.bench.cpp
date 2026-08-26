
#include <xrpl/protocol/Indexes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
mptokenKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"mptoken_id"};

    auto const mptid = makeMptID(1, benchAlice().id());

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&mptid](auto& host) { return host.mptokenKeylet(mptid, benchBob().id()); });
}
BENCHMARK(mptokenKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
