#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <benchmark/benchmark.h>
#include <rust/cxx.h>
#include <tx/wasm/fixtures/WasmLedger.h>
#include <tx/wasm/fixtures/WasmRun.h>
#include <xrpl_wasm_testkit_cxxbridge/lib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace xrpl::test::bench {

int
callsWithinTransferBudget(std::int64_t bytesWrittenPerCall)
{
    // Writes nothing back to the guest, so the budget does not apply at all.
    if (bytesWrittenPerCall <= 0)
    {
        return kCallsPerRun;
    }

    auto const affordable = kTransferLimitBytes / bytesWrittenPerCall;
    if (affordable < 1)
    {
        fixtureFailed("a single call would exceed the run's transfer budget");
    }
    return static_cast<int>(std::min<std::int64_t>(affordable, kCallsPerRun));
}

std::string
dataSegment(int offset, std::span<std::uint8_t const> bytes)
{
    return std::format("  (data (i32.const {}) \"{}\")\n", offset, watEscaped(bytes));
}

std::string
dataSegment(int offset, Bytes const& bytes)
{
    return dataSegment(offset, std::span<std::uint8_t const>{bytes.data(), bytes.size()});
}

std::string
makeLoopWat(std::string_view imports, std::string_view data, std::string_view body, int count)
{
    static constexpr auto kTemplate = R"wat((module
{}
  (memory (export "memory") 1)
{}
  (func (export "escrow_finish") (result i32)
    (local $i i32)
    (local $r i32)
    (local.set $i (i32.const {}))
    (block $done
      (loop $again
        (br_if $done (i32.eqz (local.get $i)))
        (local.set $r {})
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (br $again)))
    (local.get $r)))
)wat";

    return std::format(kTemplate, imports, data, count, body);
}

Timing
timeRun(HostFunctions& host, Bytes const& wasm)
{
    auto const start = std::chrono::steady_clock::now();
    auto outcome = runEscrowWasm(wasm, host, kBenchGas);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    benchmark::DoNotOptimize(outcome);
    return {
        .seconds = std::chrono::duration<double>(elapsed).count(),
        .gas = outcome.has_value() ? outcome->cost : std::int64_t{0}};
}

namespace {

// Seconds of wall time one unit of gas buys on this machine. See `Calibration` for why this is
// a difference rather than a single measurement.
//
// The estimator has to be the *same* one the cases use — a mean over `kCalibrationPairs` pairs,
// with the same clamp at zero as `benchmarkThroughVm`. This is not a stylistic point. Since
// `implied_gas = secondsPerCall / secondsPerGas`, any systematic difference between how the
// divisor and the dividend are estimated lands directly in every reported number. A minimum
// sits below a mean, so calibrating with a best-of while measuring cases with a mean biases
// `secondsPerGas` low and every `implied_gas` and `suggested_gas` correspondingly high.
//
// `guestInstruction` in Crossing.bench.cpp is the check that this holds: it runs this exact loop
// body, so its `implied_gas` and `charged_gas` are two measurements of one quantity and must
// agree within a few percent.
double
measureSecondsPerGas()
{
    // A couple of guest instructions per iteration, no memory traffic, nothing the engine can
    // fold away.
    static constexpr auto kBody = std::string_view{"(i32.add (local.get $r) (i32.const 1))"};
    auto const busy = assembleWat(makeLoopWat("", "", kBody, kCallsPerRun));
    auto const idle = assembleWat(makeLoopWat("", "", kBody, 0));

    auto fixture = WasmLedger{};

    // Warm the instruction cache and the allocator before the pairs that count, so the first-run
    // penalty does not land on one side of the subtraction.
    for (auto i = 0U; i < 8; ++i)
    {
        timeRun(*fixture.makeHost(), busy);
        timeRun(*fixture.makeHost(), idle);
    }

    auto total = 0.0;
    // Fuel is exact and deterministic, so any pair gives the same delta.
    auto gasDelta = std::int64_t{1};
    for (auto i = 0; i < kCalibrationPairs; ++i)
    {
        auto hotHost = fixture.makeHost();
        auto const hot = timeRun(*hotHost, busy);
        auto coldHost = fixture.makeHost();
        auto const cold = timeRun(*coldHost, idle);

        total += std::max(0.0, hot.seconds - cold.seconds);
        gasDelta = std::max(std::int64_t{1}, hot.gas - cold.gas);
    }

    return (total / kCalibrationPairs) / static_cast<double>(gasDelta);
}

// The crossing, in gas: `ldgr_index` through the VM minus `ldgr_index` called directly.
// `secondsPerGas` has to be the value from the same snapshot, so it is passed in rather than
// re-measured.
//
// Both halves are means for the same reason `measureSecondsPerGas` is: the VM half has to match
// `benchmarkThroughVm`'s estimator and the impl half `benchmarkImpl`'s, or the crossing is the
// difference of two numbers computed differently.
double
measureCrossingFloorGas(double secondsPerGas)
{
    static constexpr std::string_view kImport =
        R"(  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
)";
    static constexpr std::string_view kBody = "(call $ldgr_index (i32.const 0) (i32.const 4))";

    auto const loaded = assembleWat(makeLoopWat(kImport, "", kBody, kCallsPerRun));
    auto const baseline = assembleWat(makeLoopWat(kImport, "", kBody, 0));

    auto fixture = WasmLedger{};

    auto vmTotal = 0.0;
    for (auto i = 0; i < kBenchIterations; ++i)
    {
        auto hotHost = fixture.makeHost();
        auto const hot = timeRun(*hotHost, loaded);
        auto coldHost = fixture.makeHost();
        auto const cold = timeRun(*coldHost, baseline);

        vmTotal += std::max(0.0, hot.seconds - cold.seconds) / kCallsPerRun;
    }
    auto const vmSeconds = vmTotal / kBenchIterations;

    // The impl side is the same call without the VM. Subtracting it leaves the crossing.
    auto implTotal = 0.0;
    auto host = fixture.makeHost();
    for (auto i = 0; i < kBenchIterations; ++i)
    {
        auto const start = std::chrono::steady_clock::now();
        for (auto c = 0U; c < kCallsPerRun; ++c)
        {
            auto result = host->getLedgerSqn();
            benchmark::DoNotOptimize(result);
        }
        auto const elapsed = std::chrono::steady_clock::now() - start;
        implTotal += std::chrono::duration<double>(elapsed).count() / kCallsPerRun;
    }
    auto const implSeconds = implTotal / kBenchIterations;

    return secondsPerGas > 0.0 ? std::max(0.0, vmSeconds - implSeconds) / secondsPerGas : 0.0;
}

}  // namespace

Calibration::Calibration()
    : secondsPerGas_{measureSecondsPerGas()}
    , crossingFloorGas_{measureCrossingFloorGas(secondsPerGas_)}
{
}

Calibration const&
Calibration::instance()
{
    static Calibration const kValue;
    return kValue;
}

double
declaredGas(std::string_view wasmName)
{
    return static_cast<double>(
        rs::wasm_testkit::host_function_gas(rust::Str{wasmName.data(), wasmName.size()}));
}

void
report(
    benchmark::State& state,
    double secondsPerCall,
    double chargedGas,
    std::string_view wasmName,
    bool throughVm)
{
    auto const perGas = Calibration::instance().secondsPerGas();
    auto const implied = perGas > 0.0 ? secondsPerCall / perGas : 0.0;
    auto const suggested =
        throughVm ? implied : implied + Calibration::instance().crossingFloorGas();

    state.counters["implied_gas"] = implied;
    state.counters["ns_per_call"] = secondsPerCall * 1e9;
    state.counters["charged_gas"] = chargedGas;

    if (wasmName.empty())
        return;

    auto const declared = declaredGas(wasmName);
    state.counters["host_function_gas"] = declared;
    state.counters["suggested_gas"] = suggested;
    // Above 1: the table charges more than the work costs. Below 1: underpriced, which is the
    // direction that matters — an underpriced call is one a contract can buy too cheaply.
    state.counters["price_ratio"] = suggested > 0.0 ? declared / suggested : 0.0;
}

}  // namespace xrpl::test::bench
