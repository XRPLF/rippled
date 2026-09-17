#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "ldgr_index";

constexpr std::string_view kImport =
    R"(  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
)";

void
ledgerSqnThroughVm(benchmark::State& state)
{
    benchmarkThroughVm(
        state, kWasmName, kImport, "", "(call $ldgr_index (i32.const 0) (i32.const 4))", [] {
            return Fixtures::instance().host();
        });
}
BENCHMARK(ledgerSqnThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
ledgerSqnImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.getLedgerSqn(); });
}
BENCHMARK(ledgerSqnImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
