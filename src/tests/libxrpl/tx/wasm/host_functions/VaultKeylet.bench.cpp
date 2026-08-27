#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
vaultKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"vault_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.vaultKeylet(Fixtures::instance().alice().id(), Fixtures::kSeq);
        });
}
BENCHMARK(vaultKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
