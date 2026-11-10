#include <xrpl/shamap/SHAMap.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace xrpl {
namespace {

// Item payload size, close to a typical state entry.
constexpr std::size_t kPayloadSize = 100;

beast::Journal const kJournal{beast::Journal::getNullSink()};

// A Family for unbacked maps: the in-memory node store is never read.
class BenchFamily : public Family
{
    std::unique_ptr<node_store::Database> db_;
    TestStopwatch clock_;
    std::shared_ptr<FullBelowCache> fbCache_;
    std::shared_ptr<TreeNodeCache> tnCache_;
    node_store::DummyScheduler scheduler_;

public:
    BenchFamily()
        : fbCache_(std::make_shared<FullBelowCache>("bench full below", clock_, kJournal))
        , tnCache_(
              std::make_shared<TreeNodeCache>(
                  "bench tree nodes",
                  65536,
                  std::chrono::minutes{1},
                  clock_,
                  kJournal))
    {
        Section section;
        section.set(Keys::kType, "memory");
        section.set(Keys::kPath, "SHAMapBench");
        db_ = node_store::Manager::instance().makeDatabase(
            megabytes(4), scheduler_, 1, section, kJournal);
    }

    node_store::Database&
    db() override
    {
        return *db_;
    }

    [[nodiscard]] node_store::Database const&
    db() const override
    {
        return *db_;
    }

    beast::Journal const&
    journal() override
    {
        return kJournal;
    }

    std::shared_ptr<FullBelowCache>
    getFullBelowCache() override
    {
        return fbCache_;
    }

    std::shared_ptr<TreeNodeCache>
    getTreeNodeCache() override
    {
        return tnCache_;
    }

    void
    sweep() override
    {
    }

    void
    missingNodeAcquireBySeq(std::uint32_t, UInt256 const&) override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    missingNodeAcquireByHash(UInt256 const&, std::uint32_t) override
    {
        Throw<std::runtime_error>("missing node");
    }

    void
    reset() override
    {
    }
};

BenchFamily&
family()
{
    static BenchFamily f;
    return f;
}

// Hashed keys, as in a ledger. Keys at odd indices are never inserted, so
// they serve as lookup targets that fall between items.
std::vector<UInt256> const&
keys(std::size_t n)
{
    static std::map<std::size_t, std::vector<UInt256>> cache;
    auto& v = cache[n];
    if (v.empty())
    {
        v.reserve(2 * n);
        for (std::uint64_t i = 0; i < 2 * n; ++i)
            v.push_back(sha512Half(i));
    }
    return v;
}

void
fill(SHAMap& map, std::size_t n)
{
    Blob const payload(kPayloadSize, 0x5A);
    auto const& k = keys(n);
    for (std::size_t i = 0; i < n; ++i)
        map.addItem(SHAMapNodeType::TnAccountState, makeShamapitem(k[2 * i], makeSlice(payload)));
}

// An immutable map of `n` items, shared by every thread of a run.
SHAMap const&
immutableMap(std::size_t n)
{
    static std::map<std::size_t, std::shared_ptr<SHAMap>> cache;
    auto& snap = cache[n];
    if (!snap)
    {
        SHAMap map(SHAMapType::STATE, family());
        map.setUnbacked();
        fill(map, n);
        snap = map.snapShot(false);
    }
    return *snap;
}

void
bmLookup(benchmark::State& state)
{
    auto const n = static_cast<std::size_t>(state.range(0));
    auto const& map = immutableMap(n);
    auto const& k = keys(n);
    // Each thread starts at a different item.
    std::size_t i = static_cast<std::size_t>(state.thread_index()) * 7919;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(map.hasItem(k[2 * (i % n)]));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bmLookup)
    ->Name("BM_SHAMap_Lookup")
    ->Arg(100'000)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

void
bmUpperBound(benchmark::State& state)
{
    auto const n = static_cast<std::size_t>(state.range(0));
    auto const& map = immutableMap(n);
    auto const& k = keys(n);
    std::size_t i = 0;
    for (auto _ : state)
    {
        // An absent key, so the walk has to find the next item.
        benchmark::DoNotOptimize(map.upperBound(k[2 * (i % n) + 1]));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bmUpperBound)->Name("BM_SHAMap_UpperBound")->Arg(100'000);

void
bmIterate(benchmark::State& state)
{
    auto const n = static_cast<std::size_t>(state.range(0));
    auto const& map = immutableMap(n);
    for (auto _ : state)
    {
        std::size_t count = 0;
        for (auto const& item : map)
        {
            benchmark::DoNotOptimize(&item);
            ++count;
        }
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(bmIterate)->Name("BM_SHAMap_Iterate")->Arg(100'000)->Unit(benchmark::kMillisecond);

void
bmIteratorCopy(benchmark::State& state)
{
    auto const n = static_cast<std::size_t>(state.range(0));
    auto const& map = immutableMap(n);
    auto const it = map.upperBound(keys(n)[n + 1]);
    for (auto _ : state)
    {
        auto copy = it;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(bmIteratorCopy)->Name("BM_SHAMap_IteratorCopy")->Arg(100'000);

void
bmInsertDelete(benchmark::State& state)
{
    auto const n = static_cast<std::size_t>(state.range(0));
    auto const& k = keys(n);
    for (auto _ : state)
    {
        SHAMap map(SHAMapType::STATE, family());
        map.setUnbacked();
        fill(map, n);
        for (std::size_t i = 0; i < n; ++i)
            map.delItem(k[2 * i]);
        benchmark::DoNotOptimize(&map);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(bmInsertDelete)
    ->Name("BM_SHAMap_InsertDelete")
    ->Arg(100'000)
    ->Unit(benchmark::kMillisecond);

}  // namespace
}  // namespace xrpl
