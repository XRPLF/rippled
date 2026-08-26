#include <xrpl/protocol/Feature.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart. Both cases below
// share it — one price covers both forms, which is the thing being questioned.
constexpr std::string_view kWasmName = "amendment_enabled";

// A real registered amendment, so both forms resolve and measure a lookup that succeeds rather
// than one that fails fast. The same one `IsAmendmentEnabled.cpp` uses.
//
// A `std::string` because `getRegisteredFeature` takes one; the name form of the host call takes
// a `string_view`, so both spellings are needed and this is the one that converts to the other.
std::string const&
benchAmendment()
{
    static std::string const kValue = "TokenEscrow";
    return kValue;
}

// Declared 100 for *either* form, but the two forms do different work: by id is a set membership
// test against the ledger's rules, while by name has to resolve the name through
// `ServiceRegistry::getAmendmentTable().find()` first. One flat price for a lookup and a compare
// is exactly the kind of claim this suite exists to question, so both are measured.

void
isAmendmentEnabledByIdImpl(benchmark::State& state)
{
    // `getRegisteredFeature` answers an optional; a missing amendment would make this case
    // measure a lookup that fails rather than one that succeeds, so fail loudly instead.
    auto const feature = getRegisteredFeature(benchAmendment());
    if (!feature.has_value())
    {
        state.SkipWithError("the benchmarked amendment is not registered");
        return;
    }
    auto const id = *feature;

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&id](auto& host) { return host.isAmendmentEnabled(id); });
}
BENCHMARK(isAmendmentEnabledByIdImpl)->UseManualTime()->Iterations(kBenchIterations);

void
isAmendmentEnabledByNameImpl(benchmark::State& state)
{
    // The gap over the id form is precisely what the shared price of 100 asserts does not exist.
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.isAmendmentEnabled(std::string_view{benchAmendment()}); });
}
BENCHMARK(isAmendmentEnabledByNameImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
