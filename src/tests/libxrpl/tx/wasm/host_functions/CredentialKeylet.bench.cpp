#include <xrpl/basics/Slice.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
credentialKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"credential_id"};
    static constexpr auto kType = std::string_view{"termsandconditions"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) {
            return host.credentialKeylet(
                benchAlice().id(), benchBob().id(), Slice{kType.data(), kType.size()});
        });
}
BENCHMARK(credentialKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
