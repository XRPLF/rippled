#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_add";

// Declared 160. The float family's reference arithmetic: every other operation in the family is
// priced as a multiple of this one, so if this number is wrong the whole block shifts with it.
//
// It also carries the family's `ThroughVm` case for the ordinary two-operands-in, one-out shape.
// `FloatPower` covers the expensive end and `FloatToMantExp` the two-output shape; the other
// eleven are `Impl`-only, because crossing cost follows a call's shape rather than its arithmetic.

constexpr std::string_view kImport =
    R"(  (import "host_lib" "float_add" (func $float_add (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
)";

// x, y, out, then the rounding mode LAST: the wasm signature is not the trait's argument order.
// `float_add(x, y, mode, out)` in Rust becomes `(x_ptr, x_len, y_ptr, y_len, out_ptr, out_len,
// mode)` on the wire, because the macro expands each slice to its pointer/length pair in place and
// moves the scalars after the output region (`HostFunctionSpec::FloatAdd` in
// crates/xrpl-wasm-vm/src/register.rs). Getting this wrong passes a byte count as the mode and the
// host answers `FloatInputMalformed` — a fast rejection that looks like a plausible measurement.
constexpr std::string_view kBody =
    "(call $float_add (i32.const 0) (i32.const 12) (i32.const 16) (i32.const 12) "
    "(i32.const 64) (i32.const 12) (i32.const 0))";

void
floatAddThroughVm(benchmark::State& state)
{
    static auto const kData = dataSegment(0, FloatTest::kPi) + dataSegment(16, FloatTest::kTwo);
    benchmarkThroughVm(state, kWasmName, kImport, kData, kBody, [] { return benchHost(); });
}
BENCHMARK(floatAddThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
floatAddImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatAdd(benchFloatX(), benchFloatY(), kBenchMode); });
}
BENCHMARK(floatAddImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
