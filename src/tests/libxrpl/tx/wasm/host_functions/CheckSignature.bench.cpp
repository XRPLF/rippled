#include <xrpl/basics/Slice.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "check_sig";

// Declared 300 — and the one host function with a documented pricing disagreement, which makes it
// the first one worth measuring.
//
// A prior C++ integration priced the same operation at 35000, a factor of over a hundred apart.
// One of those is wrong, and a signature verification underpriced by 100x is the cheapest
// denial-of-service a contract could buy: a secp256k1 verify, among the most expensive things the
// host can be asked to do, for the price of three hundred guest instructions.
//
// The pair settles it. `Impl` is the verification; `ThroughVm` adds the crossing for three input
// regions. If the two come out nearly equal, the cost is all verification and the crossing is
// noise beside it — which is itself the answer.

constexpr std::string_view kImport =
    R"(  (import "host_lib" "check_sig" (func $check_sig (param i32 i32 i32 i32 i32 i32) (result i32)))
)";

// Message, signature and public key are all variable-length, so each gets a fixed offset with room
// to spare and the lengths come from the data itself.
constexpr int kMessageOffset = 0;
constexpr int kSignatureOffset = 256;
constexpr int kPubkeyOffset = 512;

void
checkSignatureThroughVm(benchmark::State& state)
{
    auto const& m = benchSignedMessage();
    static auto const kData = dataSegment(kMessageOffset, m.message) +
        dataSegment(kSignatureOffset, m.signature) + dataSegment(kPubkeyOffset, m.publicKey);
    static auto const kBody = std::string{"(call $check_sig (i32.const "} +
        std::to_string(kMessageOffset) + ") (i32.const " + std::to_string(m.message.size()) +
        ") (i32.const " + std::to_string(kSignatureOffset) + ") (i32.const " +
        std::to_string(m.signature.size()) + ") (i32.const " + std::to_string(kPubkeyOffset) +
        ") (i32.const " + std::to_string(m.publicKey.size()) + "))";

    benchmarkThroughVm(state, kWasmName, kImport, kData, kBody, [] { return benchHost(); });
}
BENCHMARK(checkSignatureThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
checkSignatureImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) {
            auto const& m = benchSignedMessage();
            return host.checkSignature(
                Slice{m.message.data(), m.message.size()},
                Slice{m.signature.data(), m.signature.size()},
                Slice{m.publicKey.data(), m.publicKey.size()});
        });
}
BENCHMARK(checkSignatureImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
