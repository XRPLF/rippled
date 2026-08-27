#include <tx/wasm/WasmBench.h>

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <benchmark/benchmark.h>
#include <rust/cxx.h>
#include <tx/wasm/WasmRun.h>
#include <xrpl_wasm_testkit_cxxbridge/lib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace xrpl::test::bench {

int
callsWithinTransferBudget(std::int64_t bytesPerCall)
{
    if (bytesPerCall <= 0)
    {
        return kCallsPerRun;
    }
    auto const affordable = (kTransferLimitBytes / 2) / bytesPerCall;
    return static_cast<int>(std::clamp<std::int64_t>(affordable, 16, kCallsPerRun));
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
double
measureSecondsPerGas()
{
    // A couple of guest instructions per iteration, no memory traffic, nothing the engine can
    // fold away.
    static constexpr auto kBody = std::string_view{"(i32.add (local.get $r) (i32.const 1))"};
    auto const busy = assembleWat(makeLoopWat("", "", kBody, kCallsPerRun));
    auto const idle = assembleWat(makeLoopWat("", "", kBody, 0));

    auto fixture = BenchFixture{};

    // Warm the instruction cache and the allocator before the pairs that count, so the first-run
    // penalty does not land on one side of the subtraction.
    for (auto i = 0U; i < 8; ++i)
    {
        timeRun(*fixture.makeHost(), busy);
        timeRun(*fixture.makeHost(), idle);
    }

    // Best-of over several pairs: the minimum is the run least disturbed by the scheduler, which
    // is the honest floor for what this machine can do.
    auto best = std::numeric_limits<double>::max();
    auto gasDelta = std::int64_t{1};
    for (auto i = 0U; i < 32; ++i)
    {
        auto hotHost = fixture.makeHost();
        auto const hot = timeRun(*hotHost, busy);
        auto coldHost = fixture.makeHost();
        auto const cold = timeRun(*coldHost, idle);
        auto const delta = hot.seconds - cold.seconds;
        if (delta > 0.0 && delta < best)
        {
            best = delta;
            gasDelta = std::max(std::int64_t{1}, hot.gas - cold.gas);
        }
    }
    return best == std::numeric_limits<double>::max() ? 0.0 : best / static_cast<double>(gasDelta);
}

// The crossing, in gas: `ldgr_index` through the VM minus `ldgr_index` called directly.
// `secondsPerGas` has to be the value from the same snapshot, so it is passed in rather than
// re-measured.
double
measureCrossingFloorGas(double secondsPerGas)
{
    static constexpr std::string_view kImport =
        R"(  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
)";
    static constexpr std::string_view kBody = "(call $ldgr_index (i32.const 0) (i32.const 4))";

    auto const loaded = assembleWat(makeLoopWat(kImport, "", kBody, kCallsPerRun));
    auto const baseline = assembleWat(makeLoopWat(kImport, "", kBody, 0));

    auto fixture = BenchFixture{};

    auto best = std::numeric_limits<double>::max();
    for (auto i = 0U; i < 16; ++i)
    {
        auto hotHost = fixture.makeHost();
        auto const hot = timeRun(*hotHost, loaded);
        auto coldHost = fixture.makeHost();
        auto const cold = timeRun(*coldHost, baseline);

        auto const perCall = (hot.seconds - cold.seconds) / kCallsPerRun;
        if (perCall > 0.0 && perCall < best)
        {
            best = perCall;
        }
    }
    if (best == std::numeric_limits<double>::max())
    {
        return 0.0;
    }

    // The impl side is the same call without the VM. Subtracting it leaves the crossing.
    auto implSeconds = std::numeric_limits<double>::max();
    auto host = fixture.makeHost();
    for (auto i = 0U; i < 16; ++i)
    {
        auto const start = std::chrono::steady_clock::now();
        for (auto c = 0U; c < kCallsPerRun; ++c)
        {
            auto result = host->getLedgerSqn();
            benchmark::DoNotOptimize(result);
        }
        auto const elapsed = std::chrono::steady_clock::now() - start;
        implSeconds =
            std::min(implSeconds, std::chrono::duration<double>(elapsed).count() / kCallsPerRun);
    }

    return secondsPerGas > 0.0 ? std::max(0.0, best - implSeconds) / secondsPerGas : 0.0;
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
        rs::wasm_testkit::declared_gas(rust::Str{wasmName.data(), wasmName.size()}));
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
    state.counters["declared_gas"] = declared;
    state.counters["suggested_gas"] = suggested;
    // Above 1: the table charges more than the work costs. Below 1: underpriced, which is the
    // direction that matters — an underpriced call is one a contract can buy too cheaply.
    state.counters["price_ratio"] = suggested > 0.0 ? declared / suggested : 0.0;
}

}  // namespace xrpl::test::bench
