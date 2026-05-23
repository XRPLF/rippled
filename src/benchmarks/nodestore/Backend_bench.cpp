#include <xrpl/basics/base_uint.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <benchmark/benchmark.h>
#include <benchmarks/nodestore/NodeStoreBench.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Backend-layer NodeStore benchmarks.
//
// Workloads (Insert / Fetch / Missing / Mixed / Work / StoreBatch) cover the
// throughput cases the retired Timing_test suite used to. Correctness of the
// fetch and store paths is verified by
// test/nodestore/Backend_test.cpp's round-trip assertions, so these
// benchmarks are deliberately measurement-only: they do not check return
// values, only DoNotOptimize them.
//
// Microbenchmark conventions followed by every body below:
//   - the object pool, keys, and access permutation are pre-generated before
//     the timed loop; the loop only indexes into them (no allocation, no RNG
//     inside the loop);
//   - fetch workloads pre-populate the backend and sync() it untimed, so the
//     timed fetch exercises the real read path rather than a write buffer
//     (caveat: NuDB sync() is a no-op, see prepopulate's comment);
//   - workloads that would otherwise wrap the pool and degrade into duplicate
//     no-op stores (Insert, StoreBatch, Work) pin total iterations to one
//     pool sweep via Iterations(poolSize); fetch workloads keep autotune
//     since repeating a fetch is the realistic steady-state read pattern;
//   - benchmark::DoNotOptimize / ClobberMemory keep results live;
//   - the harness is built once per run, before the loop.
namespace xrpl::NodeStore {
namespace {

// Pool sizes registered for every workload. The smallest is a Debug-mode
// spot-check size; the largest matches Timing_test's release workload.
constexpr std::size_t kPoolSizes[] = {1000, 10000, 100000};

// Thread counts for the thread axis, mirroring Timing_test's 1 / 4 / 8.
constexpr int kThreadCounts[] = {1, 4, 8};

// Objects per storeBatch call. Owned by the benchmark - do not
// couple this to libxrpl's kBatchWritePreallocationSize, which is documented
// as a vector::reserve hint that does not affect the amount written.
constexpr std::size_t kBatchSize = 256;

// State shared by every thread of a single benchmark run. Thread 0 builds it
// before the timed loop; Google Benchmark's loop-entry barrier then publishes
// it to the other threads, and the loop-exit barrier lets thread 0 tear it
// down safely.
struct RunState
{
    std::unique_ptr<BackendHarness> harness;
    Batch present;                     // prefix-1 objects, eligible to be stored
    Batch recent;                      // prefix-1 objects in the "future" key space
    std::vector<uint256> missing;      // prefix-2 keys that are never stored
    std::vector<std::size_t> shuffle;  // [0, poolSize) permutation for random-like access
    std::size_t avgPayload = 0;        // mean getData().size() over `present`
};

// Release the per-run pools (which are large - 100k * ~700B each) so the
// process resident set drops after each benchmark completes, instead of all
// RunStates piling up until exit.
void
releasePools(RunState& rs)
{
    Batch{}.swap(rs.present);
    Batch{}.swap(rs.recent);
    std::vector<uint256>{}.swap(rs.missing);
    std::vector<std::size_t>{}.swap(rs.shuffle);
}

// Apply the (size, threads) axes to a read workload. Read workloads can
// safely repeat their access pattern across iterations, so Google Benchmark's
// iteration autotune is left in place. UseRealTime() reports wall-clock so
// concurrency speedups are visible.
void
applyReadAxes(benchmark::Benchmark* b)
{
    b->RangeMultiplier(10)->Range(kPoolSizes[0], kPoolSizes[std::size(kPoolSizes) - 1]);
    b->Threads(1)->Threads(4)->Threads(8)->UseRealTime();
}

// --- Insert ------------------------------------------------------------------

// One store() per iteration. Total stores across all threads are pinned to
// poolSize (Iterations(N) is the per-thread iteration count in Google
// Benchmark, so we divide by threads) - otherwise the per-thread index would
// wrap past the pre-generated pool, NuDB::doInsert would silently swallow
// key_exists, and the workload would degenerate into duplicate-detection
// no-ops past one sweep.
void
registerInsert(BackendConfig const& bc)
{
    std::string const cfg = bc.config;
    for (auto const poolSize : kPoolSizes)
    {
        for (auto const threads : kThreadCounts)
        {
            if (poolSize % static_cast<std::size_t>(threads) != 0)
                continue;
            auto rs = std::make_shared<RunState>();
            benchmark::RegisterBenchmark(
                std::string("BM_Backend_Insert/") + bc.name,
                [rs, cfg](benchmark::State& state) {
                    auto const poolSize = static_cast<std::size_t>(state.range(0));
                    if (state.thread_index() == 0)
                    {
                        rs->harness = std::make_unique<BackendHarness>(cfg);
                        rs->present = makePool(1, poolSize);
                        rs->avgPayload = averagePayload(rs->present);
                    }

                    std::size_t index = state.thread_index();
                    for (auto _ : state)
                    {
                        rs->harness->backend->store(rs->present[index % poolSize]);
                        index += state.threads();
                    }

                    state.SetItemsProcessed(state.iterations());
                    state.SetBytesProcessed(
                        static_cast<std::int64_t>(state.iterations() * rs->avgPayload));
                    if (state.thread_index() == 0)
                    {
                        rs->harness.reset();
                        releasePools(*rs);
                    }
                })
                ->Arg(poolSize)
                ->Iterations(poolSize / static_cast<std::size_t>(threads))
                ->Threads(threads)
                ->UseRealTime();
        }
    }
}

// --- Fetch -------------------------------------------------------------------

// One fetch() of a present key (a hit) per iteration.
void
registerFetch(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    auto* b = benchmark::RegisterBenchmark(
        std::string("BM_Backend_Fetch/") + bc.name, [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            if (state.thread_index() == 0)
            {
                rs->harness = std::make_unique<BackendHarness>(cfg);
                rs->present = makePool(1, poolSize);
                rs->avgPayload = averagePayload(rs->present);
                prepopulate(*rs->harness->backend, rs->present);
            }

            std::size_t index = state.thread_index();
            for (auto _ : state)
            {
                std::shared_ptr<NodeObject> result;
                rs->harness->backend->fetch(rs->present[index % poolSize]->getHash(), &result);
                benchmark::DoNotOptimize(result);
                index += state.threads();
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rs->avgPayload));
            if (state.thread_index() == 0)
            {
                rs->harness.reset();
                releasePools(*rs);
            }
        });
    applyReadAxes(b);
}

// --- Missing -----------------------------------------------------------------

// One fetch() of a prefix-2 key (a miss) per iteration. The backend is left
// empty, so every lookup misses.
void
registerMissing(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    auto* b = benchmark::RegisterBenchmark(
        std::string("BM_Backend_Missing/") + bc.name, [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            if (state.thread_index() == 0)
            {
                rs->harness = std::make_unique<BackendHarness>(cfg);
                rs->missing = makeMissingKeys(poolSize);
            }

            std::size_t index = state.thread_index();
            for (auto _ : state)
            {
                std::shared_ptr<NodeObject> result;
                rs->harness->backend->fetch(rs->missing[index % poolSize], &result);
                benchmark::DoNotOptimize(result);
                index += state.threads();
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            if (state.thread_index() == 0)
            {
                rs->harness.reset();
                releasePools(*rs);
            }
        });
    applyReadAxes(b);
}

// --- Mixed -------------------------------------------------------------------

// 80% present-key hits, 20% missing-key misses. The fetch index is taken from
// a pre-built shuffle table so the access pattern is random-like (matching
// Timing_test's per-fetch uniform_int_distribution) without paying for the
// distribution inside the timed region. A sequential `index % poolSize` would
// be artificially cache-friendly to RocksDB's block cache.
void
registerMixed(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    auto* b = benchmark::RegisterBenchmark(
        std::string("BM_Backend_Mixed/") + bc.name, [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            if (state.thread_index() == 0)
            {
                rs->harness = std::make_unique<BackendHarness>(cfg);
                rs->present = makePool(1, poolSize);
                rs->missing = makeMissingKeys(poolSize);
                rs->shuffle = makeShuffle(poolSize, /*seed=*/1);
                prepopulate(*rs->harness->backend, rs->present);
            }

            std::size_t index = state.thread_index();
            for (auto _ : state)
            {
                std::shared_ptr<NodeObject> result;
                auto const pick = rs->shuffle[index % poolSize];
                if (index % 5 == 0)
                {
                    rs->harness->backend->fetch(rs->missing[pick], &result);
                }
                else
                {
                    rs->harness->backend->fetch(rs->present[pick]->getHash(), &result);
                }
                benchmark::DoNotOptimize(result);
                index += state.threads();
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            if (state.thread_index() == 0)
            {
                rs->harness.reset();
                releasePools(*rs);
            }
        });
    applyReadAxes(b);
}

// --- Work --------------------------------------------------------------------

// An xrpld-like cycle per iteration: a historical lookup that always hits, a
// recent lookup whose hit rate grows smoothly with iteration progress, and
// the insert of a new (recent) object.
//
// The recent fetch picks its index via a shuffle table so the lookup is
// random within the recent space - if it used `index % poolSize` directly it
// would fetch the very item it is about to store at the same iteration body,
// producing an all-miss-then-all-hit step function instead of a smooth ramp.
// Total stores across all threads are pinned to poolSize (see registerInsert
// for the per-thread-vs-total accounting) so the store side does not wrap
// into duplicate-detection no-ops.
void
registerWork(BackendConfig const& bc)
{
    std::string const cfg = bc.config;
    for (auto const poolSize : kPoolSizes)
    {
        for (auto const threads : kThreadCounts)
        {
            if (poolSize % static_cast<std::size_t>(threads) != 0)
                continue;
            auto rs = std::make_shared<RunState>();
            benchmark::RegisterBenchmark(
                std::string("BM_Backend_Work/") + bc.name,
                [rs, cfg](benchmark::State& state) {
                    auto const poolSize = static_cast<std::size_t>(state.range(0));
                    if (state.thread_index() == 0)
                    {
                        rs->harness = std::make_unique<BackendHarness>(cfg);
                        rs->present = makePool(1, poolSize);
                        // "recent" objects live in the future key space and are
                        // not stored yet; the insert step below populates them
                        // over time.
                        rs->recent = makePool(1, poolSize, poolSize);
                        rs->shuffle = makeShuffle(poolSize, /*seed=*/2);
                        prepopulate(*rs->harness->backend, rs->present);
                    }

                    std::size_t index = state.thread_index();
                    for (auto _ : state)
                    {
                        auto const slot = index % poolSize;
                        auto const pick = rs->shuffle[slot];

                        std::shared_ptr<NodeObject> historical;
                        rs->harness->backend->fetch(rs->present[pick]->getHash(), &historical);
                        benchmark::DoNotOptimize(historical);

                        std::shared_ptr<NodeObject> recent;
                        rs->harness->backend->fetch(rs->recent[pick]->getHash(), &recent);
                        benchmark::DoNotOptimize(recent);

                        rs->harness->backend->store(rs->recent[slot]);

                        index += state.threads();
                    }
                    benchmark::ClobberMemory();

                    // One "item" is one cycle: fetch-hit + fetch-recent + store.
                    state.SetItemsProcessed(state.iterations());
                    if (state.thread_index() == 0)
                    {
                        rs->harness.reset();
                        releasePools(*rs);
                    }
                })
                ->Arg(poolSize)
                ->Iterations(poolSize / static_cast<std::size_t>(threads))
                ->Threads(threads)
                ->UseRealTime();
        }
    }
}

// --- StoreBatch --------------------------------------------------------------

// One storeBatch() of kBatchSize objects per iteration. Single-threaded:
// Backend::storeBatch must not be called concurrently with itself or store().
// Iterations are pinned to batches.size() so the batch index never wraps -
// otherwise per-batch stores degenerate into key_exists no-ops, same as
// BM_Backend_Insert.
void
registerStoreBatch(BackendConfig const& bc)
{
    std::string const cfg = bc.config;
    for (auto const poolSize : kPoolSizes)
    {
        // The pre-flight skip below relies on this; keep the registration in
        // sync with sliceBatches' "drop trailing remainder" rule.
        auto const numBatches = poolSize / kBatchSize;
        if (numBatches == 0)
            continue;

        auto rs = std::make_shared<RunState>();
        benchmark::RegisterBenchmark(
            std::string("BM_Backend_StoreBatch/") + bc.name,
            [rs, cfg](benchmark::State& state) {
                auto const poolSize = static_cast<std::size_t>(state.range(0));
                rs->harness = std::make_unique<BackendHarness>(cfg);
                rs->present = makePool(1, poolSize);
                rs->avgPayload = averagePayload(rs->present);
                std::vector<Batch> const batches = sliceBatches(rs->present, kBatchSize);
                if (batches.empty())
                {
                    state.SkipWithError("pool smaller than one batch");
                    return;
                }

                std::size_t index = 0;
                for (auto _ : state)
                {
                    rs->harness->backend->storeBatch(batches[index % batches.size()]);
                    ++index;
                }

                state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kBatchSize));
                state.SetBytesProcessed(
                    static_cast<std::int64_t>(state.iterations() * kBatchSize * rs->avgPayload));
                rs->harness.reset();
                releasePools(*rs);
            })
            ->Arg(poolSize)
            ->Iterations(numBatches);
    }
}

// Register every workload against every configured backend. Google Benchmark
// collects benchmarks from static initializers, before main() runs.
[[maybe_unused]] bool const kRegistered = [] {
    for (auto const& bc : backendConfigs())
    {
        registerInsert(bc);
        registerFetch(bc);
        registerMissing(bc);
        registerMixed(bc);
        registerWork(bc);
        registerStoreBatch(bc);
    }
    return true;
}();

}  // namespace
}  // namespace xrpl::NodeStore
