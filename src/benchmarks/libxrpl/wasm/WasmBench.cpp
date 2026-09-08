#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
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
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace xrpl::test::bench {

int
callsWithinTransferBudget(std::int64_t bytesWrittenPerCall)
{
    // Writes nothing back to the guest, so the budget does not apply.
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

StageTimer::StageTimer(benchmark::State& state, std::int64_t moduleBytes)
    : state_{state}, moduleBytes_{moduleBytes}
{
}

void
StageTimer::add(double seconds)
{
    state_.SetIterationTime(seconds);
    total_ += seconds;
    sumSquares_ += seconds * seconds;
    ++rounds_;
}

void
StageTimer::report()
{
    if (rounds_ == 0)
    {
        return;
    }

    auto const count = static_cast<double>(rounds_);
    auto const mean = total_ / count;
    auto const variance = std::max(0.0, (sumSquares_ / count) - (mean * mean));
    auto const spread = mean > 0.0 ? std::sqrt(variance) / mean : 0.0;

    auto const& calibration = Calibration::instance();
    auto const perGas = calibration.secondsPerGas();
    auto const equivalent = perGas > 0.0 ? mean / perGas : 0.0;

    state_.counters["ns_per_op"] = mean * 1e9;
    // What this stage would cost if it were charged at the rate the guest inside it is charged at.
    // It is not charged, which is the point: this puts an unpriced stage in the host-function
    // table's units.
    state_.counters["gas_equivalent"] = equivalent;

    if (moduleBytes_ > 0)
    {
        state_.counters["module_bytes"] = static_cast<double>(moduleBytes_);
        // An average over the whole operation, not the marginal rate: it carries the case's fixed
        // cost, so it overestimates and falls toward the true slope as the sweep grows.
        state_.counters["gas_per_byte"] = equivalent / static_cast<double>(moduleBytes_);
    }

    // `gas_equivalent` divides by `secondsPerGas`, so the divisor's uncertainty is in every number
    // here too.
    auto const caseStdErr = spread / std::sqrt(count);
    auto const perGasErr = calibration.secondsPerGasRelStdErr();
    auto const totalErr = std::sqrt((caseStdErr * caseStdErr) + (perGasErr * perGasErr));
    state_.counters["rel_error"] = totalErr;
    state_.counters["unreliable"] = totalErr > kMaxRelativeSpread ? 1 : 0;
}

void
benchmarkPreflight(benchmark::State& state, Bytes const& wasm, bool expectAccepted, bool sizeSweep)
{
    (void)Calibration::instance();

    // Discarded: the reject case refuses on every iteration, and a real sink would put string
    // formatting and I/O inside the measurement.
    auto const journal = beast::Journal{beast::Journal::getNullSink()};

    if (isTesSuccess(preflightEscrowWasm(wasm, journal)) != expectAccepted)
    {
        state.SkipWithError(
            expectAccepted
                ? "the module was refused; the case would be measuring the reject path"
                : "the module was accepted; the case would be measuring the accept path");
        return;
    }

    StageTimer timer{state, sizeSweep ? static_cast<std::int64_t>(wasm.size()) : 0};
    for (auto _ : state)
    {
        auto const start = std::chrono::steady_clock::now();
        auto verdict = preflightEscrowWasm(wasm, journal);
        auto const elapsed = std::chrono::steady_clock::now() - start;

        benchmark::DoNotOptimize(verdict);
        timer.add(std::chrono::duration<double>(elapsed).count());
    }
    timer.report();
}

namespace {

// Seconds of wall time one unit of gas buys on this machine.
//
// The estimator must be the *same* one the cases use — a mean, with the same clamp at zero. Since
// `implied_gas = secondsPerCall / secondsPerGas`, any difference between how divisor and dividend
// are estimated lands in every reported number: calibrating with a best-of while measuring with a
// mean once biased the whole report +40%.
//
// `guestInstruction` in Crossing.cpp is the check that this holds — it runs this exact loop body.
double
measureSecondsPerGas(double& relativeStandardError)
{
    // A couple of guest instructions, no memory traffic, nothing the engine can fold away.
    static constexpr auto kBody = std::string_view{"(i32.add (local.get $r) (i32.const 1))"};
    auto const busy = assembleWat(makeLoopWat("", "", kBody, kCallsPerRun));
    auto const idle = assembleWat(makeLoopWat("", "", kBody, 0));

    auto fixture = WasmLedger{};

    // Warm up, so the first-run penalty does not land on one side of the subtraction.
    for (auto i = 0U; i < 8; ++i)
    {
        timeRun(*fixture.makeHost(), busy);
        timeRun(*fixture.makeHost(), idle);
    }

    auto total = 0.0;
    auto sumSquares = 0.0;
    // Fuel is exact and deterministic, so any pair gives the same delta.
    auto gasDelta = std::int64_t{1};
    for (auto i = 0; i < kCalibrationPairs; ++i)
    {
        auto hotHost = fixture.makeHost();
        auto const hot = timeRun(*hotHost, busy);
        auto coldHost = fixture.makeHost();
        auto const cold = timeRun(*coldHost, idle);

        auto const delta = std::max(0.0, hot.seconds - cold.seconds);
        total += delta;
        sumSquares += delta * delta;
        gasDelta = std::max(std::int64_t{1}, hot.gas - cold.gas);
    }

    auto const mean = total / kCalibrationPairs;
    auto const variance = std::max(0.0, (sumSquares / kCalibrationPairs) - (mean * mean));
    relativeStandardError = mean > 0.0 ? std::sqrt(variance / kCalibrationPairs) / mean : 0.0;

    return mean / static_cast<double>(gasDelta);
}

// The crossing, in gas: `ldgr_index` through the VM minus `ldgr_index` called directly.
//
// Both halves are means, for the reason above: the VM half has to match `benchmarkThroughVm`'s
// estimator and the impl half `benchmarkImpl`'s. `secondsPerGas` is passed in rather than
// re-measured so it comes from the same snapshot.
double
measureCrossingFloorGas(double secondsPerGas, double& relativeStandardError)
{
    static constexpr std::string_view kImport =
        R"(  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
)";
    static constexpr std::string_view kBody = "(call $ldgr_index (i32.const 0) (i32.const 4))";

    auto const loaded = assembleWat(makeLoopWat(kImport, "", kBody, kCallsPerRun));
    auto const baseline = assembleWat(makeLoopWat(kImport, "", kBody, 0));

    auto fixture = WasmLedger{};

    auto vmTotal = 0.0;
    auto vmSquares = 0.0;
    auto guestOverheadGas = 0.0;
    for (auto i = 0; i < kBenchIterations; ++i)
    {
        auto hotHost = fixture.makeHost();
        auto const hot = timeRun(*hotHost, loaded);
        auto coldHost = fixture.makeHost();
        auto const cold = timeRun(*coldHost, baseline);

        auto const perCall = std::max(0.0, hot.seconds - cold.seconds) / kCallsPerRun;
        vmTotal += perCall;
        vmSquares += perCall * perCall;
        // Exact, from the fuel meter: what the guest burned per call beyond the call itself.
        guestOverheadGas =
            (static_cast<double>(hot.gas - cold.gas) / kCallsPerRun) - declaredGas("ldgr_index");
    }
    auto const vmSeconds = vmTotal / kBenchIterations;

    // The impl side is the same call without the VM. Subtracting it leaves the crossing.
    auto implTotal = 0.0;
    auto implSquares = 0.0;
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

        auto const perCall = std::chrono::duration<double>(elapsed).count() / kCallsPerRun;
        implTotal += perCall;
        implSquares += perCall * perCall;
    }
    auto const implSeconds = implTotal / kBenchIterations;

    if (secondsPerGas <= 0.0)
    {
        return 0.0;
    }
    // Take the guest's loop bookkeeping off here too: `report` removes it from every `ThroughVm`
    // number, so leaving it in would make the two routes to one price disagree by that amount.
    auto const crossing = std::max(0.0, vmSeconds - implSeconds) / secondsPerGas;
    auto const floor = std::max(0.0, crossing - std::max(0.0, guestOverheadGas));

    // Relative to the *difference*, not to either half: both contribute their error, and the
    // denominator is what survives the subtraction. Not divided by `secondsPerGas` — that error is
    // common-mode with the rest of `suggested_gas` and is applied once, to the sum, in `report`.
    auto const vmVariance = std::max(0.0, (vmSquares / kBenchIterations) - (vmSeconds * vmSeconds));
    auto const implVariance =
        std::max(0.0, (implSquares / kBenchIterations) - (implSeconds * implSeconds));
    auto const vmStdErr = std::sqrt(vmVariance / kBenchIterations);
    auto const implStdErr = std::sqrt(implVariance / kBenchIterations);

    auto const crossingSeconds = vmSeconds - implSeconds;
    auto const crossingStdErr = std::sqrt((vmStdErr * vmStdErr) + (implStdErr * implStdErr));
    relativeStandardError = crossingSeconds > 0.0 ? crossingStdErr / crossingSeconds : 0.0;

    return floor;
}

}  // namespace

Calibration const&
Calibration::instance()
{
    static Calibration const kValue;
    return kValue;
}

Calibration::Calibration()
    : secondsPerGas_{measureSecondsPerGas(secondsPerGasRelStdErr_)}
    , crossingFloorGas_{measureCrossingFloorGas(secondsPerGas_, crossingFloorRelStdErr_)}
{
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
    double guestOverheadGas,
    double relativeSpread,
    std::int64_t rounds,
    std::string_view wasmName,
    bool throughVm)
{
    auto const& calibration = Calibration::instance();
    auto const perGas = calibration.secondsPerGas();
    auto const measured = perGas > 0.0 ? secondsPerCall / perGas : 0.0;

    // The timed number covers the host call *and* whatever the guest ran around it.
    // `guestOverheadGas` is exact, so taking it off removes a bias rather than trading estimates.
    auto const implied = std::max(0.0, measured - guestOverheadGas);
    auto const suggested = throughVm ? implied : implied + calibration.crossingFloorGas();

    state.counters["implied_gas"] = implied;
    state.counters["ns_per_call"] = secondsPerCall * 1e9;
    state.counters["charged_gas"] = chargedGas;

    if (wasmName.empty())
    {
        return;
    }

    auto const declared = declaredGas(wasmName);
    state.counters["host_function_gas"] = declared;
    state.counters["suggested_gas"] = suggested;
    // Below 1 is the direction that matters: an underpriced call is one a contract buys too
    // cheaply.
    state.counters["price_ratio"] = suggested > 0.0 ? declared / suggested : 0.0;

    // Uncertainty **of `suggested_gas`**, not of `implied_gas` — different numbers once the floor
    // is added. `implied` and the floor are independent timings, so their absolute errors add in
    // quadrature over the sum; but both divide by `secondsPerGas`, so that error is common-mode and
    // applies once to the total. Adding it per-term would count it twice.
    //
    // For `ThroughVm` the floor term is zero and `suggested == implied`, so this reduces exactly to
    // the plain `sqrt(caseErr² + perGasErr²)`. Only `Impl` cases move — and for a cheap one the
    // floor is most of `suggested_gas`, so an error bar describing `implied` alone described
    // little.
    auto const caseStdErr =
        rounds > 0 ? relativeSpread / std::sqrt(static_cast<double>(rounds)) : relativeSpread;

    auto const impliedErr = implied * caseStdErr;
    auto const floorErr =
        throughVm ? 0.0 : calibration.crossingFloorGas() * calibration.crossingFloorRelStdErr();
    auto const independentErr = suggested > 0.0
        ? std::sqrt((impliedErr * impliedErr) + (floorErr * floorErr)) / suggested
        : caseStdErr;

    auto const perGasErr = calibration.secondsPerGasRelStdErr();
    auto const totalErr = std::sqrt((independentErr * independentErr) + (perGasErr * perGasErr));
    state.counters["rel_error"] = totalErr;

    // A call whose own cost is small next to the crossing is read off the difference of two nearly
    // equal numbers, so its `suggested_gas` is scatter rather than signal.
    auto const floor = calibration.crossingFloorGas();
    state.counters["unreliable"] =
        (totalErr > kMaxRelativeSpread || (floor > 0.0 && suggested < floor)) ? 1 : 0;
}

}  // namespace xrpl::test::bench
