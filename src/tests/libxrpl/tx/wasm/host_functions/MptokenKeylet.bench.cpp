
#include <xrpl/protocol/Indexes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "mptoken_id";

// Declared 500, the highest in the keylet family. A 24-byte issuance id plus a 20-byte holder —
// more input bytes than its siblings, but not obviously 43% more work than the 350 ones.
void
mptokenKeyletImpl(benchmark::State& state)
{
    auto const mptid = makeMptID(1, benchAlice().id());

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&mptid](auto& host) { return host.mptokenKeylet(mptid, benchBob().id()); });
}
BENCHMARK(mptokenKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
