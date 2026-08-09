#include <xrpl/nodestore/Backend.h>

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

constexpr auto kPoolSizes = std::to_array<std::size_t>({1000, 10000, 100000});
constexpr auto kThreadCounts = std::to_array<std::size_t>({1, 4, 8});
constexpr std::size_t kBatchSize = 256;
constexpr std::size_t kMissRatio = 5;

constexpr std::string_view kNamePrefix = "BM_Backend_";
constexpr std::string_view kNameSeparator = "/";

struct RunState
{
    std::unique_ptr<BackendHarness> harness;  ///< backend under test, rebuilt per run
    Batch present;                            ///< prefix-1 objects, eligible to be stored
    Batch recent;                             ///< prefix-1 objects in the "future" key space
    std::vector<uint256> missing;             ///< prefix-2 keys that are never stored
    std::vector<std::size_t> shuffle;         ///< [0, poolSize) permutation for random-like access
    std::size_t avgPayload = 0;               ///< mean getData().size() over `present`

    void
    release()
    {
        harness.reset();
        Batch{}.swap(present);
        Batch{}.swap(recent);
        std::vector<uint256>{}.swap(missing);
        std::vector<std::size_t>{}.swap(shuffle);
    }
};

struct SetupContext
{
    RunState& rs;
    Backend& backend;
    std::size_t poolSize;
};

struct IterateContext
{
    RunState& rs;
    Backend& backend;
    std::size_t index;
    std::size_t poolSize;
};

struct Workload
{
    std::string_view name;
    std::function<void(SetupContext const&)> setup;
    std::function<void(IterateContext const&)> iterate;
    bool reportBytes = false;  // SetBytesProcessed from rs.avgPayload
    bool clobber = true;       // ClobberMemory after the loop (false for pure stores)
    bool pinToPool = false;    // pin iterations to one pool sweep instead of autotuning
};

// One store() per iteration. Iterations are pinned to one pool sweep (per
// thread) so the index never wraps past the pool - otherwise NuDB::doInsert
// swallows key_exists and the workload degenerates into duplicate-detection
// no-ops.
Workload const kInsert{
    .name = "Insert",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.avgPayload = averagePayload(ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto const& [rs, backend, index, poolSize] = ctx;
            backend.store(rs.present[index % poolSize]);
        },
    .reportBytes = true,
    .clobber = false,
    .pinToPool = true,
};

// One fetch() of a present key (a hit) per iteration.
Workload const kFetch{
    .name = "Fetch",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.avgPayload = averagePayload(ctx.rs.present);
            prepopulate(ctx.backend, ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto const& [rs, backend, index, poolSize] = ctx;
            std::shared_ptr<NodeObject> result;
            backend.fetch(rs.present[index % poolSize]->getHash(), &result);
            benchmark::DoNotOptimize(result);
        },
    .reportBytes = true,
};

// One fetch() of a never-stored key (a miss); the backend is left empty.
Workload const kMissing{
    .name = "Missing",
    .setup = [](SetupContext const& ctx) { ctx.rs.missing = makeMissingKeys(ctx.poolSize); },
    .iterate =
        [](IterateContext const& ctx) {
            auto const& [rs, backend, index, poolSize] = ctx;
            std::shared_ptr<NodeObject> result;
            backend.fetch(rs.missing[index % poolSize], &result);
            benchmark::DoNotOptimize(result);
        },
};

// 80% hits / 20% misses. The fetch index comes from a shuffle table so access
// is random-like without per-iteration RNG cost; sequential `index % poolSize`
// would be artificially cache-friendly to RocksDB's block cache.
Workload const kMixed{
    .name = "Mixed",
    .setup =
        [](SetupContext const& ctx) {
            ctx.rs.present = makePool(1, ctx.poolSize);
            ctx.rs.missing = makeMissingKeys(ctx.poolSize);
            ctx.rs.shuffle = makeShuffle(ctx.poolSize, /*seed=*/1);
            prepopulate(ctx.backend, ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto const& [rs, backend, index, poolSize] = ctx;
            std::shared_ptr<NodeObject> result;
            auto const pick = rs.shuffle[index % poolSize];
            if (index % kMissRatio == 0)
            {
                backend.fetch(rs.missing[pick], &result);
            }
            else
            {
                backend.fetch(rs.present[pick]->getHash(), &result);
            }
            benchmark::DoNotOptimize(result);
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
            prepopulate(ctx.backend, ctx.rs.present);
        },
    .iterate =
        [](IterateContext const& ctx) {
            auto const& [rs, backend, index, poolSize] = ctx;
            auto const slot = index % poolSize;
            auto const pick = rs.shuffle[slot];

            std::shared_ptr<NodeObject> historical;
            backend.fetch(rs.present[pick]->getHash(), &historical);
            benchmark::DoNotOptimize(historical);

            std::shared_ptr<NodeObject> recent;
            backend.fetch(rs.recent[pick]->getHash(), &recent);
            benchmark::DoNotOptimize(recent);

            backend.store(rs.recent[slot]);
        },
    .clobber = true,
    .pinToPool = true,
};

auto
makeRunner(Workload w, std::string cfg, std::shared_ptr<RunState> rs)
{
    return [w = std::move(w), cfg = std::move(cfg), rs = std::move(rs)](benchmark::State& state) {
        auto const poolSize = static_cast<std::size_t>(state.range(0));
        if (state.thread_index() == 0)
        {
            rs->harness = std::make_unique<BackendHarness>(cfg);
            w.setup(
                SetupContext{.rs = *rs, .backend = *rs->harness->backend, .poolSize = poolSize});
        }

        std::size_t index = state.thread_index();
        for (auto _ : state)
        {
            w.iterate(
                IterateContext{
                    .rs = *rs,
                    .backend = *rs->harness->backend,
                    .index = index,
                    .poolSize = poolSize});
            index += state.threads();
        }

        if (w.clobber)
            benchmark::ClobberMemory();

        state.SetItemsProcessed(state.iterations());
        if (w.reportBytes)
            state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * rs->avgPayload));

        if (state.thread_index() == 0)
            rs->release();
    };
}

// Register workload `w` against backend `bc`, choosing the registration shape
// from `w.pinToPool`.
void
registerWorkload(BackendConfig const& bc, Workload const& w)
{
    std::string const cfg = bc.config;
    std::string name{kNamePrefix};
    name += w.name;
    name += kNameSeparator;
    name += bc.name;

    if (!w.pinToPool)
    {
        auto rs = std::make_shared<RunState>();
        auto* b = benchmark::RegisterBenchmark(name, makeRunner(w, cfg, rs));
        b->RangeMultiplier(10)->Range(kPoolSizes.front(), kPoolSizes.back());
        b->Threads(1)->Threads(4)->Threads(8)->UseRealTime();

        return;
    }

    for (auto const poolSize : kPoolSizes)
    {
        for (auto const threads : kThreadCounts)
        {
            if (poolSize % threads != 0)
                continue;

            auto rs = std::make_shared<RunState>();
            benchmark::RegisterBenchmark(name, makeRunner(w, cfg, rs))
                ->Arg(poolSize)
                ->Iterations(poolSize / threads)
                ->Threads(static_cast<int>(threads))
                ->UseRealTime();
        }
    }
}

// One storeBatch() of kBatchSize objects per iteration. Single-threaded:
// Backend::storeBatch must not run concurrently with itself or store().
// Iterations are pinned to the batch count so the index never wraps into
// key_exists no-ops. Kept separate from Workload: batch slicing and the
// per-batch item/byte accounting don't fit the thread-axis mold.
void
registerStoreBatch(BackendConfig const& bc)
{
    std::string const cfg = bc.config;
    std::string name{kNamePrefix};
    name += "StoreBatch";
    name += kNameSeparator;
    name += bc.name;
    for (auto const poolSize : kPoolSizes)
    {
        auto const numBatches = poolSize / kBatchSize;
        if (numBatches == 0)
            continue;

        auto rs = std::make_shared<RunState>();
        benchmark::RegisterBenchmark(
            name,
            [rs, cfg](benchmark::State& state) {
                auto const poolSize = static_cast<std::size_t>(state.range(0));
                rs->harness = std::make_unique<BackendHarness>(cfg);
                rs->present = makePool(1, poolSize);
                rs->avgPayload = averagePayload(rs->present);
                std::vector<Batch> const batches = sliceFixedBatches(rs->present, kBatchSize);
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
                rs->release();
            })
            ->Arg(poolSize)
            ->Iterations(numBatches);
    }
}

[[maybe_unused]] bool const kRegistered = [] {
    auto const workloads = std::to_array({&kInsert, &kFetch, &kMissing, &kMixed, &kWork});
    for (auto const& bc : backendConfigs())
    {
        for (auto const* w : workloads)
            registerWorkload(bc, *w);

        registerStoreBatch(bc);
    }
    return true;
}();

}  // namespace
}  // namespace xrpl::node_store
