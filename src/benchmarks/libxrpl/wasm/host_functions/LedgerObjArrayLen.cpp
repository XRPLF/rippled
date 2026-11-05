#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
ledgerObjArrayLenImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"le_arr_len"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().cachedSignerListHost(); },
        [](auto& host) { return host.getLedgerObjArrayLen(1, sfSignerEntries); });
}
BENCHMARK(ledgerObjArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
