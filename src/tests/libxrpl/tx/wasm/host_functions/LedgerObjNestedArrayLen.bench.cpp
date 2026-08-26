#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
ledgerObjNestedArrayLenImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"le_inner_arr_len"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchCachedSignerListHost(); },
        [](auto& host) {
            return host.getLedgerObjNestedArrayLen(1, FieldLocator{{sfSignerEntries.getCode()}});
        });
}
BENCHMARK(ledgerObjNestedArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
