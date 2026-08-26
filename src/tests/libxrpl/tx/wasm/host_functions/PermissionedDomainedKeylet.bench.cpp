#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "permissioned_domain_id";

// Declared 350. One of the nineteen keylets: gather fixed-size inputs, hash them with a type
// prefix, answer 32 bytes. The family is priced flat, so the useful reading is against its
// siblings rather than in isolation. Only `escrow_id` carries a `ThroughVm` pair — crossing
// cost follows a call's shape, not which keylet it computes (see ../README.md).
void
permissionedDomainKeyletImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.permissionedDomainKeylet(benchAlice().id(), kBenchSeq); });
}
BENCHMARK(permissionedDomainKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
