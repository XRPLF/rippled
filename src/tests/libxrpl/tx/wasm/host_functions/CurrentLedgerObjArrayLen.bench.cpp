#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
currentLedgerObjArrayLenImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"home_le_arr_len"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().signerListHost(); },
        [](auto& host) { return host.getCurrentLedgerObjArrayLen(sfSignerEntries); });
}
BENCHMARK(currentLedgerObjArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
