#include <xrpl/basics/Slice.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "credential_id";

// Declared 350. Subject, issuer, and a variable-length credential type — the only keylet with an
// input whose size the guest chooses, so the only one where a flat price could be wrong for a
// reason other than the hash. Measured here at a typical length.
void
credentialKeyletImpl(benchmark::State& state)
{
    static constexpr auto kType = std::string_view{"termsandconditions"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) {
            return host.credentialKeylet(
                benchAlice().id(), benchBob().id(), Slice{kType.data(), kType.size()});
        });
}
BENCHMARK(credentialKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
