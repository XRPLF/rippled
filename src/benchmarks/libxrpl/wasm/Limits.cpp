#include <xrpl/protocol/Fees.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>
#include <tx/wasm/fixtures/WasmLedger.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace xrpl::test::bench {
namespace {

// What `bytecodeSizeLimit` costs a run's `gasLimit`, and what the pair of them costs a validator.
//
// wasmi 2.0 bills **7 fuel per byte of reached function body** as it lowers that body to IR, out of
// the run's own fuel. The charge lands on the first call into each function, so it is paid during
// the run rather than at preflight — which couples the two limits: `7 x reached-code-size` is a
// floor a contract pays before executing one instruction of its own. README.md has the decision
// rule these cases are meant to feed.

// `Config.h`'s defaults. Hard-coded because `src/xrpld` is not on a libxrpl benchmark's include
// path — keep in step with `Config::gasLimit` and `Config::bytecodeSizeLimit`.
constexpr std::int64_t kDefaultGasLimit = 1'000'000;
constexpr std::int64_t kDefaultBytecodeSizeLimit = 100'000;

// Filler bodies per module, held constant across the sweep so that what varies with the target size
// is body *bytes* and not function count — the rate under test is per byte. Sixteen keeps every
// body well inside wasmparser's per-function ceiling even at the largest size swept.
constexpr std::size_t kFillerFunctions = 16;

// `check_sig` calls the size sweep's entry point makes. Constant across the sweep, so it moves
// `charged_gas` by a fixed amount and never its slope; small, so it does not swamp the wall time
// the sweep is there to show. `worstCase` below is where the count is turned up instead.
constexpr int kSweepWorkCalls = 4;

// `check_sig` charged 300 gas against a suggested 26,188. It is both the
// most expensive thing a contract can ask for and the most underpriced, which is exactly the
// combination a limit has to be chosen against.
constexpr std::string_view kImport =
    R"(  (import "host_lib" "check_sig" (func $check_sig (param i32 i32 i32 i32 i32 i32) (result i32)))
)";

constexpr int kMessageOffset = 0;
constexpr int kSignatureOffset = 256;
constexpr int kPubkeyOffset = 512;

// The signed message seeded into guest memory, and the call that verifies it. Offsets and shape
// follow host_functions/CheckSignature.cpp; a wrong argument would take the rejection path, which
// is far cheaper and would quietly halve every number here.
std::string const&
workData()
{
    static auto const kData = [] {
        auto const& m = Fixtures::instance().signedMessage();
        return dataSegment(kMessageOffset, m.message) + dataSegment(kSignatureOffset, m.signature) +
            dataSegment(kPubkeyOffset, m.publicKey);
    }();
    return kData;
}

std::string const&
workCall()
{
    static auto const kCall = [] {
        auto const& m = Fixtures::instance().signedMessage();
        return std::format(
            "    (local.set $r (call $check_sig (i32.const {}) (i32.const {}) (i32.const {}) "
            "(i32.const {}) (i32.const {}) (i32.const {})))\n",
            kMessageOffset,
            m.message.size(),
            kSignatureOffset,
            m.signature.size(),
            kPubkeyOffset,
            m.publicKey.size());
    }();
    return kCall;
}

// A function that is `deadBytes` bigger than it needs to be, and no more expensive to run.
//
// Everything after the `return` is unreachable, so executing this body costs two instructions
// whatever its size — but *translating* it walks the whole body, and the fuel charged is the length
// of the body slice. That separation is the point: it isolates the translation rate from execution,
// and it is also the adversarial shape, since a contract maximizing translation cost per byte would
// look exactly like this.
std::string
fillerWat(std::size_t index, std::size_t deadBytes)
{
    auto out = std::format("  (func $f{0} (result i32)\n    (return (i32.const {0}))\n    ", index);
    out.reserve(out.size() + (deadBytes * 5) + 32);
    for (auto i = 0uz; i < deadBytes; ++i)
    {
        // One byte of body each (opcode 0x01), so the binary grows by exactly `deadBytes`.
        out += "(nop)";
    }
    out += "\n    (i32.const 0))\n";
    return out;
}

// The entry point: call the first `reached` fillers, forcing each to be translated, then verify
// `calls` signatures.
//
// `compilation_mode` is `LazyTranslation` (vm.rs), so a declared-but-never-called function is
// validated at compile time and translated never. Calling it is what makes its bytes cost fuel,
// which is why `reached` is a parameter rather than "all of them".
std::string
entryWat(std::size_t reached, int calls)
{
    auto out = std::string{
        "  (func (export \"escrow_finish\") (result i32)\n"
        "    (local $r i32)\n"};
    for (auto i = 0uz; i < reached; ++i)
    {
        out += std::format("    (drop (call $f{}))\n", i);
    }
    for (auto i = 0; i < calls; ++i)
    {
        out += workCall();
    }
    out += "    (local.get $r))\n";
    return out;
}

std::string
moduleWat(std::size_t deadTotal, std::size_t reached, int calls)
{
    auto out = std::format("(module\n{}  (memory (export \"memory\") 1)\n{}", kImport, workData());
    auto const each = deadTotal / kFillerFunctions;
    auto const extra = deadTotal % kFillerFunctions;
    for (auto i = 0uz; i < kFillerFunctions; ++i)
    {
        out += fillerWat(i, each + (i == 0 ? extra : 0));
    }
    out += entryWat(reached, calls);
    out += ")\n";
    return out;
}

// The largest module of this shape that fits in `targetBytes`.
//
// Each `(nop)` is one binary byte, so size is `base + deadTotal` up to the odd byte where a LEB
// length prefix widens; solving is therefore one guess and a correction rather than a search. The
// loop is bounded and keeps the best under-target candidate, so a shape whose overhead moves does
// not turn into a hang.
//
// Padding is what makes the work count and the size limit independent: adding a `check_sig` call
// displaces nops rather than growing the module, so a run's translation charge stays pinned to the
// size limit while its execution charge varies.
Bytes const&
moduleOfSize(std::int64_t targetBytes, std::size_t reached, int calls)
{
    // Cached so that `--benchmark_repetitions` re-enters a case without re-assembling megabytes of
    // WAT, and so `worstCase`'s probes and the case they size are built once between them.
    static auto cache = std::map<std::tuple<std::int64_t, std::size_t, int>, Bytes>{};
    auto const key = std::tuple{targetBytes, reached, calls};
    if (auto const found = cache.find(key); found != cache.end())
    {
        return found->second;
    }

    auto dead = std::size_t{0};
    auto candidate = assembleWat(moduleWat(dead, reached, calls));
    if (static_cast<std::int64_t>(candidate.size()) > targetBytes)
    {
        fixtureFailed("the module's fixed overhead already exceeds the target size");
    }

    auto best = candidate;
    for (auto round = 0; round < 8; ++round)
    {
        auto const diff = targetBytes - static_cast<std::int64_t>(candidate.size());
        if (diff == 0)
        {
            break;
        }
        if (diff < 0 && -diff > static_cast<std::int64_t>(dead))
        {
            break;
        }
        dead = static_cast<std::size_t>(static_cast<std::int64_t>(dead) + diff);
        candidate = assembleWat(moduleWat(dead, reached, calls));
        if (static_cast<std::int64_t>(candidate.size()) <= targetBytes &&
            candidate.size() > best.size())
        {
            best = candidate;
        }
    }

    return cache.emplace(key, std::move(best)).first->second;
}

// One run at a caller-chosen budget. `timeRun` always passes `kBenchGas`, which is the right
// default everywhere else and wrong here: `worstCase` is about what fits inside a real `gasLimit`.
Timing
timeRunWithGas(HostFunctions& host, Bytes const& wasm, std::int64_t gas)
{
    auto const start = std::chrono::steady_clock::now();
    auto outcome = runEscrowWasm(wasm, host, gas);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    benchmark::DoNotOptimize(outcome);
    return {
        .seconds = std::chrono::duration<double>(elapsed).count(),
        .gas = outcome.has_value() ? outcome->cost : std::int64_t{0}};
}

// Fuel a run spends on nothing but being large: this module's charge minus that of a same-sized
// module with no filler reached.
//
// Both are solved to the same target, so they differ only in which bodies get translated and in the
// entry point's `call` instructions. The difference is the translation charge plus the handful of
// fuel those calls cost.
std::int64_t
translationGas(std::int64_t targetBytes, int calls, std::int64_t chargedGas)
{
    auto host = Fixtures::instance().host();
    auto const twin = runEscrowWasm(moduleOfSize(targetBytes, 0, calls), *host, kBenchGas);
    return twin.has_value() ? chargedGas - twin->cost : 0;
}

// Accumulate a whole-run case and turn it into counters.
//
// Unlike the host-function harness this subtracts nothing: the subject is the whole run, and the
// question is what that run costs against the two ceilings it has to fit inside.
class SizedRun
{
public:
    SizedRun(benchmark::State& state, std::int64_t moduleBytes, std::int64_t gasLimit)
        : state_{state}, moduleBytes_{moduleBytes}, gasLimit_{gasLimit}
    {
    }

    void
    add(Timing const& timing)
    {
        state_.SetIterationTime(timing.seconds);
        total_ += timing.seconds;
        sumSquares_ += timing.seconds * timing.seconds;
        gasTotal_ += static_cast<double>(timing.gas);
        ++rounds_;
    }

    // `translationOf` is the target size to price translation against, or zero to leave the
    // translation counters off — they cost an extra untimed run to produce.
    void
    report(std::int64_t translationOf, int calls)
    {
        if (rounds_ == 0)
        {
            return;
        }

        auto const count = static_cast<double>(rounds_);
        auto const mean = total_ / count;
        auto const variance = std::max(0.0, (sumSquares_ / count) - (mean * mean));
        auto const spread = mean > 0.0 ? std::sqrt(variance) / mean : 0.0;
        auto const charged = gasTotal_ / count;

        auto const& calibration = Calibration::instance();
        auto const perGas = calibration.secondsPerGas();
        auto const equivalent = perGas > 0.0 ? mean / perGas : 0.0;

        state_.counters["module_bytes"] = static_cast<double>(moduleBytes_);
        state_.counters["charged_gas"] = charged;
        state_.counters["gas_per_byte"] =
            moduleBytes_ > 0 ? charged / static_cast<double>(moduleBytes_) : 0.0;
        state_.counters["ns_per_op"] = mean * 1e9;
        // The run's wall time in the units the fuel meter uses, so a charge and its cost can be
        // compared. Unlike the host-function cases this covers stages the guest is *not* charged
        // for — compile, instantiate, teardown — so it reads high against `charged_gas` by
        // construction.
        state_.counters["gas_equivalent"] = equivalent;
        // Above 1, the run is billed more fuel than its wall time is worth at the rate a guest
        // instruction is priced at; below 1, less.
        state_.counters["charge_ratio"] = equivalent > 0.0 ? charged / equivalent : 0.0;
        // **The answer to the limits question.** At 100 the contract's whole budget goes to getting
        // itself running, and past 100 it cannot be finished at all.
        state_.counters["pct_gas_limit"] = 100.0 * charged / static_cast<double>(gasLimit_);

        if (translationOf > 0)
        {
            auto const translation = static_cast<double>(
                translationGas(translationOf, calls, static_cast<std::int64_t>(charged)));
            state_.counters["translation_gas"] = translation;
            state_.counters["translation_share"] = charged > 0.0 ? translation / charged : 0.0;
        }

        auto const caseStdErr = spread / std::sqrt(count);
        auto const perGasErr = calibration.secondsPerGasRelStdErr();
        auto const totalErr = std::sqrt((caseStdErr * caseStdErr) + (perGasErr * perGasErr));
        state_.counters["rel_error"] = totalErr;
        state_.counters["unreliable"] = totalErr > kMaxRelativeSpread ? 1 : 0;
    }

private:
    benchmark::State& state_;
    std::int64_t moduleBytes_{};
    std::int64_t gasLimit_{};
    double total_{};
    double sumSquares_{};
    double gasTotal_{};
    std::int64_t rounds_{};
};

// One whole `runEscrowWasm` per iteration, unamortized: compile, instantiate, translate on first
// call, run, read the meter.
void
benchmarkSizedRun(
    benchmark::State& state,
    Bytes const& wasm,
    std::int64_t gas,
    std::int64_t gasLimit,
    std::int64_t translationOf,
    int calls)
{
    // Force the shared calibration before the clock starts, as `benchmarkRun` does.
    [[maybe_unused]] auto const& calibration = Calibration::instance();

    auto probe = Fixtures::instance().host();
    auto const check = runEscrowWasm(wasm, *probe, gas);
    if (!check.has_value())
    {
        state.SkipWithError("the benchmarked contract did not run to completion");
        return;
    }
    // A soft host error still completes the run, and the rejection path is far cheaper than the
    // verification — the case would report a confident number for work it never did.
    if (check->result <= 0)
    {
        state.SkipWithError(
            "check_sig answered " + std::to_string(check->result) +
            "; the case would be measuring the rejection path, not the verification");
        return;
    }

    auto run = SizedRun{state, static_cast<std::int64_t>(wasm.size()), gasLimit};
    for (auto _ : state)
    {
        auto host = Fixtures::instance().host();
        run.add(timeRunWithGas(*host, wasm, gas));
    }
    run.report(translationOf, calls);
}

void
sweep(benchmark::State& state, std::int64_t targetBytes, std::size_t reached)
{
    benchmarkSizedRun(
        state,
        moduleOfSize(targetBytes, reached, kSweepWorkCalls),
        kBenchGas,
        kDefaultGasLimit,
        reached > 0 ? targetBytes : 0,
        kSweepWorkCalls);
}

// Every declared function reached, so the module pays translation on all of its bytes. The worst
// case a size limit has to survive, and the row to read against `gasLimit`.
void
fullyReached(benchmark::State& state)
{
    sweep(state, state.range(0), kFillerFunctions);
}
BENCHMARK(fullyReached)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Arg(25'000)
    ->Arg(50'000)
    ->Arg(kDefaultBytecodeSizeLimit)
    ->Arg(kMaxBytecodeSizeLimit);

// The same sizes with nothing reached: validated, never translated, so none of those bytes are
// charged. Against `fullyReached` per size, this separates what a big module *costs* the validator
// from what it *bills* the contract — the first is wall time nobody pays for, the second comes out
// of `gasLimit`.
void
neverReached(benchmark::State& state)
{
    sweep(state, state.range(0), 0);
}
BENCHMARK(neverReached)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Arg(25'000)
    ->Arg(50'000)
    ->Arg(kDefaultBytecodeSizeLimit)
    ->Arg(kMaxBytecodeSizeLimit);

// At the votable size ceiling, sweeping how much of the module a run actually reaches.
//
// `7 x bytecodeSizeLimit` is the bound, not the bill: a real contract runs one path and leaves the
// rest untranslated. This is the sweep to argue a *default* from, where `fullyReached` at
// `kMaxBytecodeSizeLimit` is the one to argue a *maximum* from. `Arg` is sixteenths of the module.
void
partiallyReached(benchmark::State& state)
{
    auto const reached = static_cast<std::size_t>(state.range(0));
    sweep(state, kMaxBytecodeSizeLimit, reached);
    state.counters["reached_pct"] = 100.0 * static_cast<double>(reached) / kFillerFunctions;
}
BENCHMARK(partiallyReached)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Arg(1)
    ->Arg(4)
    ->Arg(8)
    ->Arg(12);

// How many `check_sig` calls a fully-reached module of `sizeLimit` bytes can still afford inside
// `gasLimit`, once translation has taken its cut.
//
// Fuel is exact and the charge is linear in the call count, so two probes solve it: the slope is
// the per-call charge, the intercept is everything the run pays for existing. One call is then
// backed off if rounding put the answer over, which it can when a `data` segment or a LEB prefix
// shifts under the added instructions.
int
affordableCalls(std::int64_t sizeLimit, std::int64_t gasLimit)
{
    auto chargeFor = [&](int calls) {
        auto host = Fixtures::instance().host();
        auto const outcome =
            runEscrowWasm(moduleOfSize(sizeLimit, kFillerFunctions, calls), *host, kBenchGas);
        if (!outcome.has_value())
        {
            fixtureFailed("the worst-case probe did not run to completion");
        }
        return outcome->cost;
    };

    static constexpr auto kLow = 4;
    static constexpr auto kHigh = 20;
    auto const low = chargeFor(kLow);
    auto const high = chargeFor(kHigh);
    auto const perCall = static_cast<double>(high - low) / (kHigh - kLow);
    if (perCall <= 0.0)
    {
        fixtureFailed("the worst-case probes disagree about what a call costs");
    }
    auto const fixed = static_cast<double>(low) - (kLow * perCall);

    auto calls = static_cast<int>(std::max(0.0, (static_cast<double>(gasLimit) - fixed) / perCall));
    while (calls > 0 && chargeFor(calls) > gasLimit)
    {
        --calls;
    }
    return calls;
}

// The whole point of the file: the longest a validator can be made to spend on one `EscrowFinish`
// at a given `(gasLimit, bytecodeSizeLimit)` pair. A maximal, fully-reached module that spends
// every remaining unit of fuel on the most underpriced call there is.
//
// Only pairs that can actually run are listed. Size voted to `kMaxBytecodeSizeLimit` against the
// default `gasLimit` is not among them: `fullyReached/200000` charges more than that budget holds,
// so such a contract cannot be finished at all. The two limits are voted independently through
// `FeeVoteImpl`, so that pairing is reachable — see README.md.
void
worstCase(benchmark::State& state)
{
    auto const gasLimit = state.range(0);
    auto const sizeLimit = state.range(1);

    auto const calls = affordableCalls(sizeLimit, gasLimit);
    if (calls <= 0)
    {
        state.SkipWithError("translation alone exhausts this gas limit; no work fits");
        return;
    }

    benchmarkSizedRun(
        state,
        moduleOfSize(sizeLimit, kFillerFunctions, calls),
        gasLimit,
        gasLimit,
        sizeLimit,
        calls);
    state.counters["check_sig_calls"] = calls;
    state.counters["ms_per_op"] = state.counters["ns_per_op"] / 1e6;
}
BENCHMARK(worstCase)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Args({kDefaultGasLimit, kDefaultBytecodeSizeLimit})
    ->Args({kMaxGasLimit, kMaxBytecodeSizeLimit})
    ->Args({kMaxGasLimit, kDefaultBytecodeSizeLimit});

}  // namespace
}  // namespace xrpl::test::bench
