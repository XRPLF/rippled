#include <xrpl/protocol/UintTypes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "trustline_id";

// Declared 400 against the family's 350. Three inputs rather than two — two accounts and a
// currency — so the surcharge prices one extra 20-byte value going into the hash. That is the
// most concrete per-input claim in the whole table, and the easiest to check.
void
trustLineKeyletImpl(benchmark::State& state)
{
    auto const currency = toCurrency("USD");

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&currency](auto& host) {
            return host.trustLineKeylet(benchAlice().id(), benchBob().id(), currency);
        });
}
BENCHMARK(trustLineKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
