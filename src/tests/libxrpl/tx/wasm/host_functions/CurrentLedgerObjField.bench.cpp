#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "home_le_field";

// Declared 70, barely above the 60 charged for reading the ledger sequence out of a header already
// in hand. But this one deserializes a field out of an `STObject`, and what that costs depends on
// the object's shape and on whether the read is served from the view's cache. A price set from a
// warm cache is a price an attacker can miss on purpose.
//
// The fixture runs against a real escrow created through the real transactor, so the object read
// is a real one. Note what that means for the number: after the first call the view has the object
// cached, so the thousand calls in a run measure the *warm* path. That is the honest floor, not
// the worst case; a cold-cache figure needs a fixture that evicts between calls, and is worth
// building before this particular 70 is trusted.
//
// This file carries the `ThroughVm` case for the field-code-in, bytes-out shape, shared with
// `tx_field` and `le_field`.

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
