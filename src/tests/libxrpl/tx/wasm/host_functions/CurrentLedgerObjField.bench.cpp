#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "home_le_field";
constexpr std::string_view kImport =
    R"(  (import "host_lib" "home_le_field" (func $home_le_field (param i32 i32 i32) (result i32)))
)";

void
currentLedgerObjFieldThroughVm(benchmark::State& state)
{
    static auto const kBody = std::string{"(call $home_le_field (i32.const "} +
        std::to_string(sfAccount.getCode()) + ") (i32.const 0) (i32.const 32))";

    benchmarkThroughVm(state, kWasmName, kImport, "", kBody, [] { return benchEscrowHost(); });
}
BENCHMARK(currentLedgerObjFieldThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
currentLedgerObjFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchEscrowHost(); },
        [](auto& host) { return host.getCurrentLedgerObjField(sfAccount); });
}
BENCHMARK(currentLedgerObjFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
