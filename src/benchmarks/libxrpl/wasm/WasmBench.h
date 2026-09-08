#pragma once

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// The gas-calibration harness. What the numbers mean and how to read a report are in ../README.md.

namespace xrpl::test::bench {

// Enough that a benchmark loop never ends early; running out would measure something shorter.
inline constexpr std::int64_t kBenchGas = 2'000'000'000;

// Host calls per run: enough that the per-call cost dominates the baseline subtraction's residue.
inline constexpr std::int32_t kCallsPerRun = 1000;

// Timed iterations per case. Pinned because automatic sizing cannot work here: a case reports a
// residue of nanoseconds while the iteration producing it ran two contracts and cost milliseconds,
// so sizing would ask for millions.
inline constexpr std::int32_t kBenchIterations = 50;

// Pairs the one-off calibration averages. Higher than `kBenchIterations` because `secondsPerGas`
// divides every reported number, and a bare wasm loop is cheap to repeat.
inline constexpr std::int32_t kCalibrationPairs = 400;

// Above this relative uncertainty, `suggested_gas` is reported as unreliable.
inline constexpr double kMaxRelativeSpread = 0.25;

// `TRANSFER_LIMIT_BYTES` in crates/xrpl-wasm-vm/src/vm.rs: what a run may write into guest memory
// before `charge_transfer` starts refusing calls.
inline constexpr std::int64_t kTransferLimitBytes = 1 << 20;

// How many calls a run can afford, given the bytes each has the host **write into guest memory**.
// Output direction only — what the guest passes in is borrowed, and costs nothing against the
// budget. Never raises the count to meet a floor; a case that cannot afford one call fails loudly.
int
callsWithinTransferBudget(std::int64_t bytesWrittenPerCall);

// One run of a contract: how long it took, and what the engine charged it.
struct Timing
{
    double seconds{};
    std::int64_t gas{};
};

// A `(data ...)` segment placing `bytes` at `offset` in guest memory, so the timed loop measures
// the host call rather than the guest arranging its arguments. `watEscaped` in WasmRun.h says why
// zeroed memory will not do.
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

// What this machine costs, measured once and shared by every case: two cases pricing one function
// two ways (`Impl` + crossing floor, versus `ThroughVm`) must divide by the *same* `secondsPerGas`
// or they disagree for reasons unrelated to the function.
class Calibration
{
public:
    static Calibration const&
    instance();

    // Seconds of wall time one unit of gas buys here.
    [[nodiscard]] double
    secondsPerGas() const
    {
        return secondsPerGas_;
    }

    // The gas a host call costs before it does anything: region decode, bounds checks, the cxx hop.
    [[nodiscard]] double
    crossingFloorGas() const
    {
        return crossingFloorGas_;
    }

    // Propagated into every case's `rel_error`, which is what lets one snapshot stay shared:
    // `--benchmark_repetitions` resamples per-case timings but never this divisor.
    [[nodiscard]] double
    secondsPerGasRelStdErr() const
    {
        return secondsPerGasRelStdErr_;
    }

    // An additive term in every `Impl` case's `suggested_gas`, and most of the cheap ones.
    [[nodiscard]] double
    crossingFloorRelStdErr() const
    {
        return crossingFloorRelStdErr_;
    }

private:
    Calibration();

    double secondsPerGasRelStdErr_{};
    double crossingFloorRelStdErr_{};
    double secondsPerGas_{};
    double crossingFloorGas_{};
};

// What the gas table declares for a host function, by guest import name, read through the
// `wasm_testkit` bridge so it cannot drift.
double
declaredGas(std::string_view wasmName);

// Attach the calibration counters to a finished case.
//
// `guestOverheadGas` is fuel the guest burned around the call — loop bookkeeping and the
// `i32.const`s pushing arguments. From the fuel meter, so it is exact. Zero for `Impl` cases.
void
report(
    benchmark::State& state,
    double secondsPerCall,
    double chargedGas,
    double guestOverheadGas,
    double relativeSpread,
    std::int64_t rounds,
    std::string_view wasmName,
    bool throughVm);

// Accumulates one whole-operation case and turns it into counters.
//
// The two harnesses below subtract setup away, amortizing over `kCallsPerRun` calls against a
// baseline. Here that is inverted: setup is the subject, timed whole, nothing amortized.
class StageTimer
{
public:
    // Pass zero for `moduleBytes` unless the case belongs to a sweep that *varies* module size —
    // the per-byte counter it enables is an average over the whole operation, meaningless where the
    // module is constant.
    StageTimer(benchmark::State& state, std::int64_t moduleBytes);

    void
    add(double seconds);

    // Attach the counters. Call once, after the loop.
    void
    report();

private:
    benchmark::State& state_;
    std::int64_t moduleBytes_{};
    double total_{};
    double sumSquares_{};
    std::int64_t rounds_{};
};

// Measure `preflightEscrowWasm`: compile, then the walk over the module's imports and exports.
//
// `expectAccepted` is what the module *should* do. A refused module stops at the first fault and is
// far cheaper, so a case that silently flipped verdict would report a confident number for an
// operation it never performed.
void
benchmarkPreflight(
    benchmark::State& state,
    Bytes const& wasm,
    bool expectAccepted = true,
    bool sizeSweep = false);

// Measure a whole `runEscrowWasm` — every stage, unamortized.
template <class SetUp>
void
benchmarkRun(benchmark::State& state, Bytes const& wasm, SetUp&& setUp, bool sizeSweep = false)
{
    // Force the calibration and the engine's lazy construction before the clock starts, so the
    // first case does not absorb them into its own first iteration.
    [[maybe_unused]] auto const& calibration = Calibration::instance();

    auto probe = setUp();
    if (auto const check = runEscrowWasm(wasm, *probe, kBenchGas); !check.has_value())
    {
        state.SkipWithError("the benchmarked contract did not run to completion");
        return;
    }

    auto timer = StageTimer{state, sizeSweep ? static_cast<std::int64_t>(wasm.size()) : 0};
    for (auto _ : state)
    {
        auto host = setUp();
        timer.add(timeRun(*host, wasm).seconds);
    }
    timer.report();
}

// Measure a host function through the whole stack — guest, VM, marshalling, real impl, real ledger
// — with everything but the host calls subtracted away.
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

    // A host serves exactly one run — `runEscrowWasm` asserts it was handed a clean one
    // (`checkSelf` in WasmVM.cpp). Hence `setUp` being a factory rather than a host.
    auto probe = setUp();

    // A soft host error still completes the run, so require both that it completed and that the
    // last host call returned a non-negative result, which every body leaves in `$r`.
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
    auto sumSquares = 0.0;
    auto totalGas = 0.0;
    auto rounds = std::int64_t{0};
    for (auto _ : state)
    {
        auto hotHost = setUp();
        auto const hot = timeRun(*hotHost, loaded);
        auto coldHost = setUp();
        auto const cold = timeRun(*coldHost, baseline);

        // Clamped: on a noisy machine a pair can invert, and a negative iteration time would make
        // Google Benchmark's statistics meaningless.
        auto const perCall = std::max(0.0, hot.seconds - cold.seconds) / calls;
        state.SetIterationTime(perCall);

        totalSeconds += perCall;
        sumSquares += perCall * perCall;
        totalGas += static_cast<double>(hot.gas - cold.gas) / calls;
        ++rounds;
    }

    if (rounds > 0)
    {
        auto const meanSeconds = totalSeconds / rounds;
        auto const variance = std::max(0.0, (sumSquares / rounds) - (meanSeconds * meanSeconds));
        auto const spread = meanSeconds > 0.0 ? std::sqrt(variance) / meanSeconds : 0.0;

        // Zero for the empty-name case — `guestInstruction`, which prices no host function. There
        // is no host call to separate scaffolding *from*, and subtracting the full charge would
        // leave `implied_gas = measured - charged`, ~0 by construction: it would turn the harness's
        // one self-test into a tautology that cannot fail.
        auto const chargedPerCall = totalGas / rounds;
        auto const overhead = wasmName.empty() ? 0.0 : chargedPerCall - declaredGas(wasmName);

        report(
            state,
            meanSeconds,
            chargedPerCall,
            std::max(0.0, overhead),
            spread,
            rounds,
            wasmName,
            true);
    }
}

// Measure a host function's impl alone — no guest, no VM, no marshalling. Against the `ThroughVm`
// case for the same function, the difference is what crossing the boundary costs.
template <class SetUp, class Call>
void
benchmarkImpl(benchmark::State& state, std::string_view wasmName, SetUp&& setUp, Call&& call)
{
    auto host = setUp();

    // A call that errors returns early and is far cheaper than one that works, so a subtly wrong
    // argument yields a confident and always *too low* price. Probed either side of the loop rather
    // than inside it, where the branch would land in the measurement: *before* catches wrong
    // arguments, *after* catches a call that stopped working once the loop exhausted something.
    //
    // The `requires` skips host functions that answer nothing (`trace`) or answer a bare value.
    auto const checkSucceeds = [&](char const* when) {
        if constexpr (requires { call(*host).has_value(); })
        {
            if (auto const probe = call(*host); !probe.has_value())
            {
                state.SkipWithError(
                    std::string{"the benchmarked host call returned error code "} +
                    std::to_string(static_cast<int>(probe.error())) + " " + when +
                    " the timed loop; the case would be measuring the rejection path");
                return false;
            }
        }
        return true;
    };

    if (!checkSucceeds("before"))
    {
        return;
    }

    auto totalSeconds = 0.0;
    auto sumSquares = 0.0;
    auto rounds = std::int64_t{0};
    for (auto _ : state)
    {
        auto const start = std::chrono::steady_clock::now();
        for (int i = 0; i < kCallsPerRun; ++i)
        {
            // `trace` answers nothing, so `ClobberMemory` stands in for `DoNotOptimize`.
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
        sumSquares += perCall * perCall;
        ++rounds;
    }

    if (!checkSucceeds("after"))
    {
        return;
    }

    if (rounds > 0)
    {
        auto const meanSeconds = totalSeconds / rounds;
        auto const variance = std::max(0.0, (sumSquares / rounds) - (meanSeconds * meanSeconds));
        auto const spread = meanSeconds > 0.0 ? std::sqrt(variance) / meanSeconds : 0.0;

        // Nothing charged and no guest scaffolding; `report` adds the crossing back in.
        report(state, meanSeconds, 0.0, 0.0, spread, rounds, wasmName, false);
    }
}

}  // namespace xrpl::test::bench
