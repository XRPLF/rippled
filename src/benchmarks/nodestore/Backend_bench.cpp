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
// These reach full parity with the workloads of the retired Timing_test suite
// (Insert / Fetch / Missing / Mixed / Work) and add batch store/fetch cases.
// Every workload is registered against every backend in `backendConfigs()`.
//
// Microbenchmark conventions followed by every body below:
//   - the object pool and keys are pre-generated before the timed loop; the
//     loop only indexes into them (no allocation, no RNG inside the loop);
//   - fetch workloads pre-populate the backend and sync() it untimed, so the
//     timed fetch exercises the real read path rather than a write buffer;
//   - benchmark::DoNotOptimize / ClobberMemory keep results live;
//   - the harness is built once per run, before the loop.
namespace xrpl::NodeStore {
namespace {

// Number of distinct objects pre-generated per run. Matches the release-build
// workload size of the Timing_test suite this benchmark replaces.
constexpr std::size_t kDefaultPoolSize = 100000;

// Objects per storeBatch / fetchBatch call.
constexpr std::size_t kBatchSize = kBatchWritePreallocationSize;  // 256

// State shared by every thread of a single benchmark run. Thread 0 builds it
// before the timed loop; Google Benchmark's loop-entry barrier then publishes
// it to the other threads, and the loop-exit barrier lets thread 0 tear it
// down safely.
struct RunState
{
    std::unique_ptr<BackendHarness> harness;
    Batch present;                 // prefix-1 objects, eligible to be stored
    Batch recent;                  // prefix-1 objects in the "future" key space
    std::vector<uint256> missing;  // prefix-2 keys that are never stored
    std::size_t avgPayload = 0;    // mean getData().size() over `present`
};

// Apply the thread-count axis, mirroring Timing_test's 1 / 4 / 8 parallel-for.
// UseRealTime() reports wall-clock so concurrency speedups are visible.
void
applyThreadAxis(benchmark::Benchmark* b)
{
    b->Threads(1)->Threads(4)->Threads(8)->UseRealTime();
}

// --- Insert ------------------------------------------------------------------

// One store() per iteration. Once the total iteration count exceeds the pool
// size the per-thread index wraps, and the workload becomes overwrite rather
// than cold insert.
void
registerInsert(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    auto* b = benchmark::RegisterBenchmark(
        std::string("BM_Backend_Insert/") + bc.name, [rs, cfg](benchmark::State& state) {
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
            state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rs->avgPayload));
            if (state.thread_index() == 0)
                rs->harness.reset();
        });
    b->Arg(kDefaultPoolSize);
    applyThreadAxis(b);
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
                rs->harness.reset();
        });
    b->Arg(kDefaultPoolSize);
    applyThreadAxis(b);
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
                rs->harness.reset();
        });
    b->Arg(kDefaultPoolSize);
    applyThreadAxis(b);
}

// --- Mixed -------------------------------------------------------------------

// 80% present-key hits, 20% missing-key misses - the split Timing_test drove
// with kMissingNodePercent, here made deterministic so no RNG runs in the loop.
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
                prepopulate(*rs->harness->backend, rs->present);
            }

            std::size_t index = state.thread_index();
            for (auto _ : state)
            {
                std::shared_ptr<NodeObject> result;
                if (index % 5 == 0)
                {
                    rs->harness->backend->fetch(rs->missing[index % poolSize], &result);
                }
                else
                {
                    rs->harness->backend->fetch(rs->present[index % poolSize]->getHash(), &result);
                }
                benchmark::DoNotOptimize(result);
                index += state.threads();
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            if (state.thread_index() == 0)
                rs->harness.reset();
        });
    b->Arg(kDefaultPoolSize);
    applyThreadAxis(b);
}

// --- Work --------------------------------------------------------------------

// An xrpld-like cycle per iteration: a historical lookup that always hits, a
// recent lookup that may miss, and the insert of a new (recent) object.
void
registerWork(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    auto* b = benchmark::RegisterBenchmark(
        std::string("BM_Backend_Work/") + bc.name, [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            if (state.thread_index() == 0)
            {
                rs->harness = std::make_unique<BackendHarness>(cfg);
                rs->present = makePool(1, poolSize);
                // "recent" objects live in the future key space and are not
                // stored yet; the insert step below populates them over time.
                rs->recent = makePool(1, poolSize, poolSize);
                prepopulate(*rs->harness->backend, rs->present);
            }

            std::size_t index = state.thread_index();
            for (auto _ : state)
            {
                std::shared_ptr<NodeObject> historical;
                rs->harness->backend->fetch(rs->present[index % poolSize]->getHash(), &historical);
                benchmark::DoNotOptimize(historical);

                std::shared_ptr<NodeObject> recent;
                rs->harness->backend->fetch(rs->recent[index % poolSize]->getHash(), &recent);
                benchmark::DoNotOptimize(recent);

                rs->harness->backend->store(rs->recent[index % poolSize]);

                index += state.threads();
            }
            benchmark::ClobberMemory();

            // One "item" is one cycle: fetch-hit + fetch-recent + store.
            state.SetItemsProcessed(state.iterations());
            if (state.thread_index() == 0)
                rs->harness.reset();
        });
    b->Arg(kDefaultPoolSize);
    applyThreadAxis(b);
}

// --- StoreBatch --------------------------------------------------------------

// One storeBatch() of kBatchSize objects per iteration. Single-threaded:
// Backend::storeBatch must not be called concurrently with itself or store().
void
registerStoreBatch(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
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
        })
        ->Arg(kDefaultPoolSize);
}

// --- FetchBatch --------------------------------------------------------------

// One fetchBatch() of kBatchSize present keys per iteration.
void
registerFetchBatch(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    benchmark::RegisterBenchmark(
        std::string("BM_Backend_FetchBatch/") + bc.name,
        [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            rs->harness = std::make_unique<BackendHarness>(cfg);
            rs->present = makePool(1, poolSize);
            rs->avgPayload = averagePayload(rs->present);
            prepopulate(*rs->harness->backend, rs->present);
            std::vector<std::vector<uint256>> const batches = sliceHashes(rs->present, kBatchSize);
            if (batches.empty())
            {
                state.SkipWithError("pool smaller than one batch");
                return;
            }

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto results = rs->harness->backend->fetchBatch(batches[index % batches.size()]);
                benchmark::DoNotOptimize(results);
                ++index;
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * kBatchSize));
            state.SetBytesProcessed(
                static_cast<std::int64_t>(state.iterations() * kBatchSize * rs->avgPayload));
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize);
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
        registerFetchBatch(bc);
    }
    return true;
}();

}  // namespace
}  // namespace xrpl::NodeStore
