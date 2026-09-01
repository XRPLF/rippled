#pragma once

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// The gas-calibration harness: how a `*.bench.cpp` measures a host call, and how that
// measurement becomes a suggested price. What the numbers mean and how to read a report are in
// ../README.md.

namespace xrpl::test::bench {

// Enough gas that a thousand-call benchmark loop never ends early; a benchmark measures
// work, so running out of budget would silently measure something shorter instead.
inline constexpr std::int64_t kBenchGas = 2'000'000'000;

// How many host calls a benchmarked contract makes per run. Large enough that the
// per-call cost dominates the residue left by the baseline subtraction, small enough that
// one run stays in the microsecond range.
inline constexpr std::int32_t kCallsPerRun = 1000;

// How many timed iterations each case runs. Pinned rather than left to Google Benchmark's
// automatic sizing, which cannot work here: a case reports the subtraction's residue — tens
// of nanoseconds — while the iteration that produced it ran two whole contracts, module
// compilation included, and cost milliseconds. Automatic sizing sees only the reported time,
// so it would ask for millions of iterations to accumulate its default `min_time` and the
// case would never finish. Every registration therefore ends
// `->UseManualTime()->Iterations(kBenchIterations)`.
inline constexpr std::int32_t kBenchIterations = 50;

// How many pairs the one-off calibration averages. Higher than `kBenchIterations` because
// `secondsPerGas` is the divisor for *every* reported number, so its noise is common-mode across
// the whole report, and because calibration runs a bare wasm loop with no ledger and no host
// calls — a few hundred extra pairs cost milliseconds. More samples of a mean is only more
// precision, not a different estimator, so this does not reintroduce the bias that mixing a
// best-of with a mean did.
inline constexpr std::int32_t kCalibrationPairs = 400;

// How much a run may write into guest memory before `charge_transfer` starts refusing calls
// (`TRANSFER_LIMIT_BYTES` in crates/xrpl-wasm-vm/src/vm.rs). Per run, so it resets between the
// runs a benchmark makes — but one run of `kCallsPerRun` calls could exhaust it partway through
// and spend the rest of the loop measuring the refusal path instead of the host function.
inline constexpr std::int64_t kTransferLimitBytes = 1 << 20;

// How many calls a run can afford, given how many bytes each one has the host **write into guest
// memory**.
//
// One direction only: the budget is charged in `write_into` / `write_buffered` / `write_mant_exp`
// and nowhere else. What the guest passes *in* is borrowed rather than copied and costs nothing
// against it, so a caller passes the size of its output region, not of its input.
//
// Never raises the count to meet a floor — that would be the one thing this function exists to
// prevent. A case that cannot afford a single call cannot be measured, so that fails loudly.
int
callsWithinTransferBudget(std::int64_t bytesWrittenPerCall);

// One run of a contract: how long it took, and what the engine charged it.
struct Timing
{
    double seconds{};
    std::int64_t gas{};
};

// A `(data ...)` segment placing `bytes` at `offset` in the guest's memory, so a case's
// input is in place before the timed loop starts and the loop measures the host call rather
// than the guest arranging its arguments. See `watEscaped` in WasmRun.h for why zeroed
// memory will not do.
std::string
dataSegment(int offset, std::span<std::uint8_t const> bytes);

std::string
dataSegment(int offset, Bytes const& bytes);

// A contract that runs `body` `count` times and returns the last result.
std::string
makeLoopWat(std::string_view imports, std::string_view data, std::string_view body, int count);

// Run pre-assembled `wasm` once through the real VM, reporting wall time and gas.
Timing
timeRun(HostFunctions& host, Bytes const& wasm);

// What this machine costs, measured once and shared by every case.
class Calibration
{
public:
    // The machine's calibration, measured on first use. Measuring runs a few hundred short
    // contracts, so the first case to ask pays for it and every later case reads this answer.
    static Calibration const&
    instance();

    // Seconds of wall time one unit of gas buys here.
    [[nodiscard]] double
    secondsPerGas() const
    {
        return secondsPerGas_;
    }

    // The gas a host call costs before it does anything: region decode, bounds checks, the cxx
    // hop.
    [[nodiscard]] double
    crossingFloorGas() const
    {
        return crossingFloorGas_;
    }

private:
    Calibration();

    double secondsPerGas_{};
    double crossingFloorGas_{};
};

// What the gas table says a host function costs, by its guest import name.
// Read from the declaration through the `wasm_testkit` bridge.
double
declaredGas(std::string_view wasmName);

// Attach the calibration counters to a finished case.
void
report(
    benchmark::State& state,
    double secondsPerCall,
    double chargedGas,
    std::string_view wasmName,
    bool throughVm);

// Measure a host function *through the whole stack* — guest, VM, marshalling, real impl,
// real ledger — with everything but the host calls subtracted away.
template <class SetUp>
void
benchmarkThroughVm(
    benchmark::State& state,
    std::string_view wasmName,
    std::string_view imports,
    std::string_view data,
    std::string_view body,
    SetUp&& setUp,
    int calls = kCallsPerRun)
{
    auto const loaded = assembleWat(makeLoopWat(imports, data, body, calls));
    auto const baseline = assembleWat(makeLoopWat(imports, data, body, 0));

    // A host serves exactly one run: it caches the current ledger object, the slot table and
    // the contract's data for that run's length, and `runEscrowWasm` asserts it was handed a
    // clean one (see `checkSelf` in WasmVM.cpp). So every run below builds its own. That
    // costs the measurement nothing — `timeRun` starts its clock after the host exists —
    // and it is why `setUp` is a factory rather than a host.
    auto probe = setUp();

    // Confirm the contract actually succeeds before measuring it — and note that "the run
    // succeeded" is not enough to establish that.
    // So require both: the run completed, and the contract's last host call returned a
    // non-negative result. Every body here leaves that result in `$r`, which the module
    // returns.
    auto const check = runEscrowWasm(loaded, *probe, kBenchGas);
    if (!check.has_value())
    {
        state.SkipWithError("the benchmarked contract did not run to completion");
        return;
    }
    if (check->result < 0)
    {
        state.SkipWithError(
            "the benchmarked host call returned error code " + std::to_string(check->result) +
            "; the case would be measuring the rejection path, not the work");
        return;
    }

    auto totalSeconds = 0.0;
    auto totalGas = 0.0;
    auto rounds = std::int64_t{0};
    for (auto _ : state)
    {
        auto hotHost = setUp();
        auto const hot = timeRun(*hotHost, loaded);
        auto coldHost = setUp();
        auto const cold = timeRun(*coldHost, baseline);

        // Clamped at zero: on a noisy machine a single pair can invert, and a negative
        // iteration time would make Google Benchmark's statistics meaningless.
        auto const perCall = std::max(0.0, hot.seconds - cold.seconds) / calls;
        state.SetIterationTime(perCall);

        totalSeconds += perCall;
        totalGas += static_cast<double>(hot.gas - cold.gas) / calls;
        ++rounds;
    }

    if (rounds > 0)
    {
        report(state, totalSeconds / rounds, totalGas / rounds, wasmName, true);
    }
}

// Measure a host function's *impl alone* — the computation, with no guest, no VM and no
// marshalling. Paired with the `ThroughVm` case for the same function, the difference is
// what crossing the guest/host boundary costs.
// Register with `->UseManualTime()`.
template <class SetUp, class Call>
void
benchmarkImpl(benchmark::State& state, std::string_view wasmName, SetUp&& setUp, Call&& call)
{
    auto host = setUp();

    auto totalSeconds = 0.0;
    auto rounds = std::int64_t{0};
    for (auto _ : state)
    {
        auto const start = std::chrono::steady_clock::now();
        for (int i = 0; i < kCallsPerRun; ++i)
        {
            // `trace` is the one host function that answers nothing, so there is no result
            // to hold onto; `ClobberMemory` stands in for `DoNotOptimize` to keep the call
            // from being elided.
            if constexpr (std::is_void_v<decltype(call(*host))>)
            {
                call(*host);
                benchmark::ClobberMemory();
            }
            else
            {
                auto result = call(*host);
                benchmark::DoNotOptimize(result);
            }
        }
        auto const elapsed = std::chrono::steady_clock::now() - start;

        auto const perCall = std::chrono::duration<double>(elapsed).count() / kCallsPerRun;
        state.SetIterationTime(perCall);

        totalSeconds += perCall;
        ++rounds;
    }

    // No VM ran, so nothing was charged — and the crossing this case leaves out is added back
    // into `suggested_gas`, because a guest cannot make the call without paying it.
    if (rounds > 0)
    {
        report(state, totalSeconds / rounds, 0.0, wasmName, false);
    }
}

}  // namespace xrpl::test::bench
