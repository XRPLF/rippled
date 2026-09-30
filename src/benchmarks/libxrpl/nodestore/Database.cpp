#include <xrpl/nodestore/Database.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Types.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/nodestore/NodeStoreBench.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::node_store {
namespace {

// Number of distinct objects pre-generated per run.
constexpr std::size_t kDefaultPoolSize = 100000;

// Async read threads the Database spawns. Unused by the synchronous fetch path
// these benchmarks take; kept fixed so runs are comparable.
constexpr int kReadThreads = 4;

constexpr std::string_view kNamePrefix = "BM_Database_";
constexpr std::string_view kNameSeparator = "/";

struct RunState
{
    std::unique_ptr<DatabaseHarness> harness;
    Batch present;                     // prefix-1 objects, eligible to be stored
    Batch recent;                      // prefix-1 objects in the "future" key space
    std::vector<uint256> missing;      // prefix-2 keys that are never stored
    std::vector<std::size_t> shuffle;  // [0, poolSize) permutation for random-like access
    std::size_t avgPayload = 0;        // mean getData().size() over `present`
};

struct SetupContext
{
    RunState& rs;
    Database& db;
    std::size_t poolSize;
};

struct IterateContext
{
    RunState& rs;
    Database& db;
    std::uint32_t seq;
    std::size_t index;
    std::size_t poolSize;
};

struct Workload
{
    std::string_view name;
    std::function<void(SetupContext const&)> setup;
    std::function<void(IterateContext const&)> iterate;
    bool reportBytes = false;
    bool pinIterations = false;
};

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

// One store() per iteration; a fresh Blob copy is handed over each time.
Workload const kStore{
    .name = "Store",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.avgPayload = averagePayload(ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto& [rs, db, seq, index, poolSize] = ctx;
            auto const& obj = rs.present[index % poolSize];
            Blob data(obj->getData());
            db.store(obj->getType(), std::move(data), obj->getHash(), seq);
        },
    .reportBytes = true,
    .pinIterations = true,
};

// One fetchNodeObject() of a stored key (a hit) per iteration.
Workload const kFetch{
    .name = "Fetch",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.avgPayload = averagePayload(ctx.rs.present);
            prepopulate(ctx.db, ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto& [rs, db, seq, index, poolSize] = ctx;
            auto obj = db.fetchNodeObject(rs.present[index % poolSize]->getHash(), seq);
            benchmark::DoNotOptimize(obj);
        },
    .reportBytes = true,
};

// One fetchNodeObject() of a never-stored key (a miss) per iteration.
Workload const kMissing{
    .name = "Missing",
    .setup = [](SetupContext const& ctx) { ctx.rs.missing = makeMissingKeys(ctx.poolSize); },
    .iterate =
        [](IterateContext const& ctx) {
            auto& [rs, db, seq, index, poolSize] = ctx;
            auto obj = db.fetchNodeObject(rs.missing[index % poolSize], seq);
            benchmark::DoNotOptimize(obj);
        },
};

// 80% hits / 20% misses. The fetch index comes from a shuffle table so access
// is random-like without per-iteration RNG cost; sequential `index % poolSize`
// would be artificially cache-friendly.
Workload const kMixed{
    .name = "Mixed",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.missing = makeMissingKeys(ctx.poolSize);
            ctx.rs.shuffle = makeShuffle(ctx.poolSize, /*seed=*/1);
            prepopulate(ctx.db, ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto& [rs, db, seq, index, poolSize] = ctx;
            auto const pick = rs.shuffle[index % poolSize];
            std::shared_ptr<NodeObject> obj;
            if (index % 5 == 0)
            {
                obj = db.fetchNodeObject(rs.missing[pick], seq);
            }
            else
            {
                obj = db.fetchNodeObject(rs.present[pick]->getHash(), seq);
            }
            benchmark::DoNotOptimize(obj);
        },
};

// An xrpld-like cycle: a hit, a maybe-miss recent fetch, and a store. The
// recent fetch uses the shuffle table (not `slot`) so it doesn't fetch the item
// it's about to store this iteration - which would give an all-miss-then-hit
// step instead of a smooth ramp. The store walks sequentially so each recent
// object is stored once.
Workload const kWork{
    .name = "Work",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.recent = makePool(1, ctx.poolSize, ctx.poolSize);
            ctx.rs.shuffle = makeShuffle(ctx.poolSize, /*seed=*/2);
            prepopulate(ctx.db, ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto& [rs, db, seq, index, poolSize] = ctx;
            auto const slot = index % poolSize;
            auto const pick = rs.shuffle[slot];

            auto historical = db.fetchNodeObject(rs.present[pick]->getHash(), seq);
            benchmark::DoNotOptimize(historical);

            auto recent = db.fetchNodeObject(rs.recent[pick]->getHash(), seq);
            benchmark::DoNotOptimize(recent);

            auto const& obj = rs.recent[slot];
            Blob data(obj->getData());
            db.store(obj->getType(), std::move(data), obj->getHash(), seq);
        },
    .pinIterations = true,
};

void
registerWorkload(BackendConfig const& bc, Workload const& w)
{
    auto rs = std::make_shared<RunState>();
    std::string const cfg = bc.config;
    std::string name{kNamePrefix};
    name += w.name;
    name += kNameSeparator;
    name += bc.name;
    auto* b = benchmark::RegisterBenchmark(name, [rs, cfg, w](benchmark::State& state) {
        auto const poolSize = static_cast<std::size_t>(state.range(0));
        rs->harness = std::make_unique<DatabaseHarness>(cfg, kReadThreads);
        auto& db = *rs->harness->db;
        w.setup(SetupContext{.rs = *rs, .db = db, .poolSize = poolSize});
        auto const seq = db.earliestLedgerSeq();

        std::size_t index = 0;
        for (auto _ : state)
        {
            w.iterate(
                IterateContext{
                    .rs = *rs, .db = db, .seq = seq, .index = index, .poolSize = poolSize});
            ++index;
        }
        benchmark::ClobberMemory();

        state.SetItemsProcessed(state.iterations());
        if (w.reportBytes)
        {
            state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rs->avgPayload));
        }
        rs->harness.reset();
    });

    b->Arg(kDefaultPoolSize);

    if (w.pinIterations)
        b->Iterations(kDefaultPoolSize);
}

[[maybe_unused]] bool const kRegistered = [] {
    auto const workloads = std::to_array({&kStore, &kFetch, &kMissing, &kMixed, &kWork});
    for (auto const& bc : backendConfigs())
    {
        for (auto const* w : workloads)
            registerWorkload(bc, *w);
    }
    return true;
}();

}  // namespace
}  // namespace xrpl::node_store
