#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
currentLedgerObjNestedArrayLenImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"home_le_inner_arr_len"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().signerListHost(); },
        [](auto& host) {
            return host.getCurrentLedgerObjNestedArrayLen(
                FieldLocator{{sfSignerEntries.getCode()}});
        });
}
BENCHMARK(currentLedgerObjNestedArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
