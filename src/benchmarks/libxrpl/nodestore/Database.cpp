#include <xrpl/nodestore/Database.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/nodestore/NodeStoreBench.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Database-layer NodeStore benchmarks.
//
// The Database is the application-facing wrapper around a Backend: it owns the
// backend, adds fetch/store accounting, and runs an async read-thread pool.
// These benchmarks drive it through the public API - store(type, Blob&&, hash,
// ledgerSeq) and fetchNodeObject(hash, ledgerSeq) - so they measure the path
// xrpld actually takes.
//
// They are single-threaded at the Google Benchmark level: the Database has its
// own internal read-thread pool, sized by `kReadThreads`. That pool only serves
// asyncFetch(); a benchmark that exercises it is a natural follow-up.
//
// Because every benchmark here is single-threaded, the per-run setup runs
// unguarded at the top of each lambda. If a future workload calls
// applyThreadAxis (or any Threads(N>1) variant), wrap the setup in
// `if (state.thread_index() == 0) { ... }` to avoid racing on rs->harness and
// double-spawning kReadThreads detached threads per concurrent assignment.
namespace xrpl::NodeStore {
namespace {

// Number of distinct objects pre-generated per run.
constexpr std::size_t kDefaultPoolSize = 100000;

// Async read threads the Database spawns. Unused by the synchronous fetch path
// these benchmarks take; kept fixed so runs are comparable.
constexpr int kReadThreads = 4;

// State for a single benchmark run, rebuilt before each timed loop.
struct RunState
{
    std::unique_ptr<DatabaseHarness> harness;
    Batch present;                     // prefix-1 objects, eligible to be stored
    Batch recent;                      // prefix-1 objects in the "future" key space
    std::vector<uint256> missing;      // prefix-2 keys that are never stored
    std::vector<std::size_t> shuffle;  // [0, poolSize) permutation for random-like access
    std::size_t avgPayload = 0;        // mean getData().size() over `present`
};

// Store every object through the Database API and flush. store() consumes the
// caller's Blob, so each object's payload is copied into a fresh one.
void
prepopulate(Database& db, Batch const& objects)
{
    auto const seq = db.earliestLedgerSeq();
    for (auto const& obj : objects)
    {
        Blob data(obj->getData());
        db.store(obj->getType(), std::move(data), obj->getHash(), seq);
    }
    db.sync();
}

// --- Store -------------------------------------------------------------------

// One store() per iteration. store() takes ownership of the Blob, so a fresh
// copy of the payload is handed over each time - the realistic caller cost.
void
registerStore(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    benchmark::RegisterBenchmark(
        std::string("BM_Database_Store/") + bc.name,
        [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            rs->harness = std::make_unique<DatabaseHarness>(cfg, kReadThreads);
            rs->present = makePool(1, poolSize);
            rs->avgPayload = averagePayload(rs->present);
            auto& db = *rs->harness->db;
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto const& obj = rs->present[index % poolSize];
                Blob data(obj->getData());
                db.store(obj->getType(), std::move(data), obj->getHash(), seq);
                ++index;
            }

            state.SetItemsProcessed(state.iterations());
            state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rs->avgPayload));
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize)
        ->Iterations(kDefaultPoolSize);
}

// --- Fetch -------------------------------------------------------------------

// One fetchNodeObject() of a stored key (a hit) per iteration.
void
registerFetch(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    benchmark::RegisterBenchmark(
        std::string("BM_Database_Fetch/") + bc.name,
        [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            rs->harness = std::make_unique<DatabaseHarness>(cfg, kReadThreads);
            rs->present = makePool(1, poolSize);
            rs->avgPayload = averagePayload(rs->present);
            auto& db = *rs->harness->db;
            prepopulate(db, rs->present);
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto obj = db.fetchNodeObject(rs->present[index % poolSize]->getHash(), seq);
                benchmark::DoNotOptimize(obj);
                ++index;
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rs->avgPayload));
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize);
}

// --- Missing -----------------------------------------------------------------

// One fetchNodeObject() of a never-stored key (a miss) per iteration.
void
registerMissing(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    benchmark::RegisterBenchmark(
        std::string("BM_Database_Missing/") + bc.name,
        [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            rs->harness = std::make_unique<DatabaseHarness>(cfg, kReadThreads);
            rs->missing = makeMissingKeys(poolSize);
            auto& db = *rs->harness->db;
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto obj = db.fetchNodeObject(rs->missing[index % poolSize], seq);
                benchmark::DoNotOptimize(obj);
                ++index;
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize);
}

// --- Mixed -------------------------------------------------------------------

// 80% present-key hits, 20% missing-key misses, deterministically split. The
// fetch index is taken from a pre-built shuffle table so the access pattern is
// random-like (matching Timing_test's per-fetch uniform_int_distribution)
// without paying for the distribution inside the timed region. A sequential
// `index % poolSize` would be artificially cache-friendly.
void
registerMixed(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    benchmark::RegisterBenchmark(
        std::string("BM_Database_Mixed/") + bc.name,
        [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            rs->harness = std::make_unique<DatabaseHarness>(cfg, kReadThreads);
            rs->present = makePool(1, poolSize);
            rs->missing = makeMissingKeys(poolSize);
            rs->shuffle = makeShuffle(poolSize, /*seed=*/1);
            auto& db = *rs->harness->db;
            prepopulate(db, rs->present);
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto const pick = rs->shuffle[index % poolSize];
                std::shared_ptr<NodeObject> obj;
                if (index % 5 == 0)
                {
                    obj = db.fetchNodeObject(rs->missing[pick], seq);
                }
                else
                {
                    obj = db.fetchNodeObject(rs->present[pick]->getHash(), seq);
                }
                benchmark::DoNotOptimize(obj);
                ++index;
            }
            benchmark::ClobberMemory();

            state.SetItemsProcessed(state.iterations());
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize);
}

// --- Work --------------------------------------------------------------------

// An xrpld-like cycle per iteration: a historical lookup that always hits, a
// recent lookup that may miss, and the store of a new (recent) object.
//
// The recent fetch picks its index via a shuffle table so the lookup is random
// within the recent space - if it used `index % poolSize` directly it would
// fetch the very item it is about to store in the same iteration body,
// producing an all-miss-then-all-hit step function instead of a smooth ramp.
// The store still walks the pool sequentially (`slot`) so every recent object
// is stored exactly once.
void
registerWork(BackendConfig const& bc)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    benchmark::RegisterBenchmark(
        std::string("BM_Database_Work/") + bc.name,
        [rs, cfg](benchmark::State& state) {
            auto const poolSize = static_cast<std::size_t>(state.range(0));
            rs->harness = std::make_unique<DatabaseHarness>(cfg, kReadThreads);
            rs->present = makePool(1, poolSize);
            // "recent" objects live in the future key space and are not stored
            // yet; the store step below populates them over time.
            rs->recent = makePool(1, poolSize, poolSize);
            rs->shuffle = makeShuffle(poolSize, /*seed=*/2);
            auto& db = *rs->harness->db;
            prepopulate(db, rs->present);
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto const slot = index % poolSize;
                auto const pick = rs->shuffle[slot];

                auto historical = db.fetchNodeObject(rs->present[pick]->getHash(), seq);
                benchmark::DoNotOptimize(historical);

                auto recent = db.fetchNodeObject(rs->recent[pick]->getHash(), seq);
                benchmark::DoNotOptimize(recent);

                auto const& obj = rs->recent[slot];
                Blob data(obj->getData());
                db.store(obj->getType(), std::move(data), obj->getHash(), seq);

                ++index;
            }
            benchmark::ClobberMemory();

            // One "item" is one cycle: fetch-hit + fetch-recent + store.
            state.SetItemsProcessed(state.iterations());
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize)
        ->Iterations(kDefaultPoolSize);
}

// Register every workload against every configured backend, before main().
[[maybe_unused]] bool const kRegistered = [] {
    for (auto const& bc : backendConfigs())
    {
        registerStore(bc);
        registerFetch(bc);
        registerMissing(bc);
        registerMixed(bc);
        registerWork(bc);
    }
    return true;
}();

}  // namespace
}  // namespace xrpl::NodeStore
