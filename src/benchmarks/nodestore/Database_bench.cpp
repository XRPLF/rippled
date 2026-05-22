#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <benchmark/benchmark.h>
#include <benchmarks/nodestore/NodeStoreBench.h>

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
    Batch present;                 // prefix-1 objects, eligible to be stored
    Batch recent;                  // prefix-1 objects in the "future" key space
    std::vector<uint256> missing;  // prefix-2 keys that are never stored
    std::size_t avgPayload = 0;    // mean getData().size() over `present`
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
        ->Arg(kDefaultPoolSize);
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

// 80% present-key hits, 20% missing-key misses, deterministically split.
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
            auto& db = *rs->harness->db;
            prepopulate(db, rs->present);
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                std::shared_ptr<NodeObject> obj;
                if (index % 5 == 0)
                {
                    obj = db.fetchNodeObject(rs->missing[index % poolSize], seq);
                }
                else
                {
                    obj = db.fetchNodeObject(rs->present[index % poolSize]->getHash(), seq);
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
            auto& db = *rs->harness->db;
            prepopulate(db, rs->present);
            auto const seq = db.earliestLedgerSeq();

            std::size_t index = 0;
            for (auto _ : state)
            {
                auto historical = db.fetchNodeObject(rs->present[index % poolSize]->getHash(), seq);
                benchmark::DoNotOptimize(historical);

                auto recent = db.fetchNodeObject(rs->recent[index % poolSize]->getHash(), seq);
                benchmark::DoNotOptimize(recent);

                auto const& obj = rs->recent[index % poolSize];
                Blob data(obj->getData());
                db.store(obj->getType(), std::move(data), obj->getHash(), seq);

                ++index;
            }
            benchmark::ClobberMemory();

            // One "item" is one cycle: fetch-hit + fetch-recent + store.
            state.SetItemsProcessed(state.iterations());
            rs->harness.reset();
        })
        ->Arg(kDefaultPoolSize);
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
