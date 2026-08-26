#pragma once

#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmRun.h>
#include <xrpl_wasm_testkit_cxxbridge/lib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// Gas calibration for the wasm host functions.
//
// Every `#[gas = N]` in `crates/xrpl-host-functions/src/lib.rs` is a promise about how
// much work a host call does relative to a guest instruction: the engine meters both from
// one pool (`set_fuel(gas)` in `crates/xrpl-wasm-vm/src/vm.rs`), so one unit of gas *is*
// roughly one wasm instruction. That makes the calibration question answerable and, more
// importantly, machine-independent: not "how many nanoseconds does `sha512_half` take"
// (a property of the box) but "how many guest instructions' worth of work is it" (a
// property of the code). Only the second can be written into a consensus rule.
//
// Two costs hide inside one host call, and pricing needs them apart:
//
//   * the *impl* — what `WasmHostFunctionsImpl` computes. Measured by calling the host
//     method directly, with no VM in the picture (`benchmarkImpl`).
//   * the *crossing* — region decode, bounds checks, memory copies, the cxx bridge hop.
//     Paid on every call regardless of what the call does (`benchmarkThroughVm`).
//
// So the whole design here is a subtraction, and it appears twice:
//
//   1. Inside a VM benchmark, between a contract that makes N host calls and an otherwise
//      byte-identical contract that makes none. That removes module compilation,
//      instantiation and the guest's own loop from the number. The subtraction happens
//      per iteration, so Google Benchmark's variance statistics describe the isolated
//      host call rather than the run that contains it.
//   2. Between the `ThroughVm` and `Impl` cases for the same function, read off the
//      report afterwards. That difference is the crossing, which should come out roughly
//      constant across functions plus a term in the byte count. Functions whose cost
//      grows with input size are registered over a `Range` so that term is visible.
//
// The reported counters are the point; wall time is only the raw material:
//
//   `suggested_gas`  **the answer** — what this function should be priced at. For a
//                    `ThroughVm` case that is what was measured; for an `Impl` case the
//                    crossing is added back, since a guest cannot call without paying it.
//   `declared_gas`   what `lib.rs` currently says, read through the `wasm_testkit` bridge so
//                    it can never drift from the declaration.
//   `price_ratio`    `declared / suggested`. 1.0 is correct. Above 1 the table overcharges;
//                    **below 1 it undercharges**, which is the direction that matters — an
//                    underpriced call is one a contract can buy too cheaply.
//   `implied_gas`    the raw measurement, before the crossing is added back.
//   `charged_gas`    what the engine actually charged (from `EscrowResult::cost`), on VM
//                    cases. Should track the declaration, and is how the harness shows it is
//                    measuring the call it thinks it is.
//   `ns_per_call`    the underlying wall time, for debugging a suspicious ratio.
//
// Sort a report by `price_ratio` and the mispriced functions come to the top:
//   ./xrpl.bench.wasm --benchmark_format=csv | sort -t, -k

//
// One caveat on `suggested_gas` for `Impl`-only cases: the crossing added back is the *floor*,
// measured on a call with no input. A function that moves bytes pays more than the floor, so
// its suggestion is a lower bound. The swept cases (`Sha512Half`, `UpdateData`) measure that
// per-byte term where it matters.
//
// Run it:
//   ./xrpl.bench.wasm --benchmark_filter='Sha512Half'
//   ./xrpl.bench.wasm --benchmark_format=json > gas.json
// and on Linux, where Google Benchmark is built against libpfm (see `enable_libpfm` in
// conanfile.py), hardware counters are available as a cross-check on the timing:
//   ./xrpl.bench.wasm --benchmark_perf_counters=INSTRUCTIONS,CYCLES
//
// Build Release before believing anything. A Debug build inflates the crossing far more
// than it inflates the impls — the marshalling is templates and `std::expected`, none of it
// inlined — so Debug numbers overstate what leaving the guest costs and understate every
// function's own work relative to it. Google Benchmark prints a warning when it detects
// this; do not read past it.
//
// These are *calibration* runs, not tests: nothing here asserts, and a number moving is
// not a build failure. They live beside the tests for the same functions because the two
// share a fixture and should move together, but they build into their own executable
// (`xrpl.bench.wasm`) so they never run under ctest.

namespace xrpl::test::bench {

// Enough gas that a thousand-call benchmark loop never ends early; a benchmark measures
// work, so running out of budget would silently measure something shorter instead.
inline constexpr std::int64_t kBenchGas = 2'000'000'000;

// How many host calls a benchmarked contract makes per run. Large enough that the
// per-call cost dominates the residue left by the baseline subtraction, small enough that
// one run stays in the microsecond range.
inline constexpr int kCallsPerRun = 1000;

// How many timed iterations each case runs. Pinned rather than left to Google Benchmark's
// automatic sizing, which cannot work here: a case reports the subtraction's residue — tens
// of nanoseconds — while the iteration that produced it ran two whole contracts, module
// compilation included, and cost milliseconds. Automatic sizing sees only the reported time,
// so it would ask for millions of iterations to accumulate its default `min_time` and the
// case would never finish. Every registration therefore ends
// `->UseManualTime()->Iterations(kBenchIterations)`.
inline constexpr int kBenchIterations = 50;

// Every run gets this much guest<->host copying before `charge_transfer` starts refusing
// calls (`TRANSFER_LIMIT_BYTES` in `crates/xrpl-wasm-vm/src/vm.rs`). It is a per-run budget,
// so it resets between the runs a benchmark makes — but a single run of `kCallsPerRun` calls
// moving a kilobyte each would exhaust it partway through and spend the rest of the loop
// measuring the refusal path instead of the host function.
inline constexpr std::int64_t kTransferLimitBytes = 1 << 20;

// How many calls a run can afford at `bytesPerCall`, staying clear of the transfer budget.
//
// Halved because most functions move bytes in *both* directions — an input region read plus
// an output region written — and the budget counts both. A size-swept case passes this as
// its call count so the large end of the range does not silently turn into an error
// benchmark; per-call numbers stay comparable across counts, which is what the report shows.
inline int
callsWithinTransferBudget(std::int64_t bytesPerCall)
{
    if (bytesPerCall <= 0)
        return kCallsPerRun;
    auto const affordable = (kTransferLimitBytes / 2) / bytesPerCall;
    return static_cast<int>(std::clamp<std::int64_t>(affordable, 16, kCallsPerRun));
}

// True when Google Benchmark was built with libpfm and `--benchmark_perf_counters` will
// work. Reported as a counter so a JSON report records which mode produced it.
inline constexpr bool kPerfCountersAvailable =
#ifdef XRPL_BENCH_PERF_COUNTERS
    true;
#else
    false;
#endif

// The test fixtures these benchmarks measure against derive from `testing::Test`, whose pure
// virtual `TestBody` makes them abstract. A benchmark wants a fixture's ledger, host and
// setup helpers, not GTest's lifecycle, so supply the one missing member and nothing else.
// Nothing here registers or runs a test.
//
// Templated so a case can reuse whichever fixture its test uses — `Bench<NFTTest>` for the
// NFT benchmarks, `Bench<FloatTest>` for the float ones — instead of duplicating that setup.
template <class Fixture>
struct Bench : Fixture
{
    void
    TestBody() override
    {
    }
};

using BenchFixture = Bench<RealHostFixture>;

// One run of a contract: how long it took, and what the engine charged it.
struct Timing
{
    double seconds;
    std::int64_t gas;
};

// A `(data ...)` segment placing `bytes` at `offset` in the guest's memory, so a case's
// input is in place before the timed loop starts and the loop measures the host call rather
// than the guest arranging its arguments. See `watEscaped` in WasmRun.h for why zeroed
// memory will not do.
inline std::string
dataSegment(int offset, std::span<std::uint8_t const> bytes)
{
    return std::string{"  (data (i32.const "} + std::to_string(offset) + ") \"" +
        watEscaped(bytes) + "\")\n";
}

inline std::string
dataSegment(int offset, Bytes const& bytes)
{
    return dataSegment(offset, std::span<std::uint8_t const>{bytes.data(), bytes.size()});
}

// A contract that runs `body` `count` times and returns the last result.
//
// `count` is the *only* thing that varies between a loaded module and its baseline: the
// imports, the data segments, the function bodies and the module's size are identical, so
// compiling and instantiating them costs the same and cancels out of the subtraction. At
// `count == 0` the loop is entered and immediately exited, so even the branch is paid by
// both.
//
// `data` holds any `dataSegment` calls the case needs; it goes after the memory
// declaration that gives those segments something to write into.
inline std::string
makeLoopWat(std::string_view imports, std::string_view data, std::string_view body, int count)
{
    return std::string{"(module\n"} + std::string{imports} +
        R"wat(
  (memory (export "memory") 1)
)wat" + std::string{data} +
        R"wat(
  (func (export "escrow_finish") (result i32)
    (local $i i32)
    (local $r i32)
    (local.set $i (i32.const )wat" +
        std::to_string(count) + R"wat())
    (block $done
      (loop $again
        (br_if $done (i32.eqz (local.get $i)))
        (local.set $r )wat" +
        std::string{body} + R"wat()
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (br $again)))
    (local.get $r)))
)wat";
}

// Run pre-assembled `wasm` once through the real VM, reporting wall time and gas.
//
// Assembly (WAT text -> bytes) is deliberately outside the timed region: it is a
// test-only convenience from the `wasm_testkit` crate, not something a validator ever
// does. Compilation *is* inside, because a validator does pay it — but it is identical
// between a module and its baseline, so the subtraction removes it.
inline Timing
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

// Seconds of wall time one unit of gas buys on this machine.
//
// This is the conversion that makes every other number here machine-independent. It is
// measured, not assumed: a pure-wasm loop (no host calls, no ledger) run at two different
// iteration counts, with the difference in time divided by the difference in fuel. Taking
// a difference rather than a single measurement removes compilation and startup, which
// would otherwise inflate the apparent cost of a guest instruction and make every host
// function look cheap by comparison.
//
// Computed once per process and cached: it describes the machine, not the case.
inline double
secondsPerGas()
{
    static double const kValue = [] {
        // A couple of guest instructions per iteration, no memory traffic, nothing the
        // engine can fold away.
        static constexpr std::string_view kBody = "(i32.add (local.get $r) (i32.const 1))";
        auto const busy = assembleWat(makeLoopWat("", "", kBody, kCallsPerRun));
        auto const idle = assembleWat(makeLoopWat("", "", kBody, 0));

        BenchFixture fixture;

        // Warm the instruction cache and the allocator before the pairs that count, so
        // the first-run penalty does not land on one side of the subtraction.
        for (int i = 0; i < 8; ++i)
        {
            timeRun(*fixture.makeHost(), busy);
            timeRun(*fixture.makeHost(), idle);
        }

        // Best-of over several pairs: the minimum is the run least disturbed by the
        // scheduler, which is the honest floor for what this machine can do.
        auto best = std::numeric_limits<double>::max();
        auto gasDelta = std::int64_t{1};
        for (int i = 0; i < 32; ++i)
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
        return best == std::numeric_limits<double>::max() ? 0.0
                                                          : best / static_cast<double>(gasDelta);
    }();
    return kValue;
}

// What the gas table says a host function costs, by its guest import name.
//
// Read from the declaration through the `wasm_testkit` bridge rather than transcribed into
// C++: 61 copied constants would drift from `crates/xrpl-host-functions/src/lib.rs` the first
// time a price changed, and drift *silently*, because a benchmark has nothing to fail.
inline double
declaredGas(std::string_view wasmName)
{
    return static_cast<double>(
        rs::wasm_testkit::declared_gas(rust::Str{wasmName.data(), wasmName.size()}));
}

// The gas a host call costs before it does anything: region decode, bounds checks, the cxx hop.
//
// Measured once per process the same way `secondsPerGas` is, and from the same pair the suite
// uses as its floor — `ldgr_index` through the VM minus `ldgr_index` called directly. It takes
// no input and answers from a header already in hand, so what remains after the subtraction is
// the crossing and nothing else.
//
// This is what makes a *suggested* price possible for a function that has only an `Impl` case:
// the impl measures the work, and this measures the toll every call pays on top of it.
inline double
crossingFloorGas()
{
    static double const kValue = [] {
        static constexpr std::string_view kImport =
            R"(  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
)";
        static constexpr std::string_view kBody = "(call $ldgr_index (i32.const 0) (i32.const 4))";

        auto const loaded = assembleWat(makeLoopWat(kImport, "", kBody, kCallsPerRun));
        auto const baseline = assembleWat(makeLoopWat(kImport, "", kBody, 0));

        BenchFixture fixture;

        auto best = std::numeric_limits<double>::max();
        for (int i = 0; i < 16; ++i)
        {
            auto hotHost = fixture.makeHost();
            auto const hot = timeRun(*hotHost, loaded);
            auto coldHost = fixture.makeHost();
            auto const cold = timeRun(*coldHost, baseline);

            auto const perCall = (hot.seconds - cold.seconds) / kCallsPerRun;
            if (perCall > 0.0 && perCall < best)
                best = perCall;
        }
        if (best == std::numeric_limits<double>::max())
            return 0.0;

        // The impl side is the same call without the VM. Subtracting it leaves the crossing.
        auto implSeconds = std::numeric_limits<double>::max();
        auto host = fixture.makeHost();
        for (int i = 0; i < 16; ++i)
        {
            auto const start = std::chrono::steady_clock::now();
            for (int c = 0; c < kCallsPerRun; ++c)
            {
                auto result = host->getLedgerSqn();
                benchmark::DoNotOptimize(result);
            }
            auto const elapsed = std::chrono::steady_clock::now() - start;
            implSeconds = std::min(
                implSeconds, std::chrono::duration<double>(elapsed).count() / kCallsPerRun);
        }

        auto const perGas = secondsPerGas();
        return perGas > 0.0 ? std::max(0.0, best - implSeconds) / perGas : 0.0;
    }();
    return kValue;
}

// Attach the calibration counters to a finished case.
//
// `secondsPerCall` is the isolated per-call cost. `chargedGas` is what the engine billed per
// call, or 0 for a direct-impl case where no VM was involved. `wasmName` names the host
// function so its declared price can be looked up; empty for the harness's own reference cases,
// which price nothing.
//
// `suggested_gas` is the headline: what the function *should* cost, in the same units as the
// declaration. For a `ThroughVm` case that is simply what was measured, since the guest already
// paid the crossing. For an `Impl` case the crossing has to be added back, because a guest
// cannot make the call without it.
inline void
report(
    benchmark::State& state,
    double secondsPerCall,
    double chargedGas,
    std::string_view wasmName,
    bool throughVm)
{
    auto const perGas = secondsPerGas();
    auto const implied = perGas > 0.0 ? secondsPerCall / perGas : 0.0;
    auto const suggested = throughVm ? implied : implied + crossingFloorGas();

    state.counters["implied_gas"] = implied;
    state.counters["ns_per_call"] = secondsPerCall * 1e9;
    state.counters["charged_gas"] = chargedGas;
    state.counters["perf_counters"] = kPerfCountersAvailable ? 1 : 0;

    if (wasmName.empty())
        return;

    auto const declared = declaredGas(wasmName);
    state.counters["declared_gas"] = declared;
    state.counters["suggested_gas"] = suggested;
    // Above 1: the table charges more than the work costs. Below 1: underpriced, which is the
    // direction that matters — an underpriced call is one a contract can buy too cheaply.
    state.counters["price_ratio"] = suggested > 0.0 ? declared / suggested : 0.0;
}

// Measure a host function *through the whole stack* — guest, VM, marshalling, real impl,
// real ledger — with everything but the host calls subtracted away.
//
// `wasmName` is the guest import name, used to look up the declared price. `imports` declares
// the host function and `data` seeds any input bytes it reads;
// `body` is the call expression, which must leave one i32 on the stack. `setUp` prepares
// the ledger and returns the host to run against, so a case can fund accounts or create
// the object it reads.
//
// `calls` is how many host calls one run makes; a size-swept case should pass
// `callsWithinTransferBudget(bytesPerCall)` so the large end of its range stays inside the
// engine's copying budget. The reported numbers are per call either way.
//
// Register with `->UseManualTime()`: the reported time is the subtraction's result, not
// the wall time of the runs that produced it.
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
    //
    // A soft host error is an *answer*, not a fault: the engine hands the guest a negative
    // code and the run completes normally, `EscrowResult` and all. Gas is charged before the
    // body too (`charged` in crates/xrpl-wasm-vm/src/abi.rs), so `charged_gas` looks correct
    // for a call that did nothing. A case whose arguments are subtly wrong would therefore
    // report a plausible, confidently wrong number — measuring the rejection path, which is
    // much cheaper than the work. The tell is a `ThroughVm` case coming out faster than its
    // `Impl` pair, which is impossible when one contains the other.
    //
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
        report(state, totalSeconds / rounds, totalGas / rounds, wasmName, true);
}

// Measure a host function's *impl alone* — the computation, with no guest, no VM and no
// marshalling. Paired with the `ThroughVm` case for the same function, the difference is
// what crossing the guest/host boundary costs.
//
// `wasmName` is the guest import name, used to look up the declared price. `call` invokes the
// host method and returns its result; `setUp` builds the ledger and
// host once, outside the timed region, so fixture setup is not measured. The inner loop
// runs `kCallsPerRun` calls per timed iteration, matching the VM case's shape and
// amortizing the clock read over enough work that it does not dominate a cheap impl.
//
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
        report(state, totalSeconds / rounds, 0.0, wasmName, false);
}

}  // namespace xrpl::test::bench
