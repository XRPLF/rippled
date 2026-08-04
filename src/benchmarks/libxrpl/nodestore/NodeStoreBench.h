#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <random>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

// Shared helpers for the NodeStore benchmarks.
//
namespace xrpl::node_store {

// Fill `bytes` of memory at `buffer` with random bits drawn from `g`.
template <class Generator>
inline void
rngcpy(void* buffer, std::size_t bytes, Generator& g)
{
    using result_type = typename Generator::result_type;
    while (bytes > 0)
    {
        auto const v = g();
        auto const chunk = std::min(bytes, sizeof(result_type));
        std::memcpy(buffer, &v, chunk);
        buffer = reinterpret_cast<std::uint8_t*>(buffer) + chunk;
        bytes -= chunk;
    }
}

/**
 * @brief Deterministic generator of a reproducible sequence of random NodeObjects.
 *
 * Indexing is stable: `obj(n)` and `key(n)` always return the same value for a
 * given `n`, regardless of call order, because the engine is reseeded from `n`
 * on every call.
 *
 * Using different prefixes guarantees the two key spaces are disjoint for the fetch-miss
 * workloads.
 */
class Sequence
{
private:
    static constexpr auto kMinSize = 250;
    static constexpr auto kMaxSize = 1250;

    beast::xor_shift_engine gen_;
    std::uint8_t prefix_;
    std::discrete_distribution<std::uint32_t> dType_;
    std::uniform_int_distribution<std::uint32_t> dSize_;

public:
    explicit Sequence(std::uint8_t prefix)
        : prefix_(prefix)
        // uniform distribution over hotLEDGER - hotTRANSACTION_NODE
        // but exclude hotTRANSACTION = 2 (removed)
        , dType_({1, 1, 0, 1, 1})
        , dSize_(kMinSize, kMaxSize)
    {
    }

    // Returns the n-th key. Used to generate keys that are never stored.
    // The layout mirrors obj()'s: prefix at byte 0, RNG over the rest, so the
    // two key spaces stay disjoint by construction (not by coincidence).
    uint256
    key(std::size_t n)
    {
        gen_.seed(n + 1);
        uint256 result;
        auto const data = static_cast<std::uint8_t*>(&*result.begin());
        *data = prefix_;
        rngcpy(data + 1, result.size() - 1, gen_);
        return result;
    }

    // Returns the n-th complete NodeObject.
    std::shared_ptr<NodeObject>
    obj(std::size_t n)
    {
        gen_.seed(n + 1);
        uint256 key;
        auto const data = static_cast<std::uint8_t*>(&*key.begin());
        *data = prefix_;
        rngcpy(data + 1, key.size() - 1, gen_);
        Blob value(dSize_(gen_));
        rngcpy(&value[0], value.size(), gen_);
        return NodeObject::createObject(
            safeCast<NodeObjectType>(dType_(gen_)), std::move(value), key);
    }

    // Fills `b` with `size` consecutive NodeObjects starting at index `n`.
    void
    batch(std::size_t n, Batch& b, std::size_t size)
    {
        b.clear();
        b.reserve(size);
        while ((size--) != 0u)
            b.push_back(obj(n++));
    }
};

// Parse a comma-separated "key=value,key=value" string into a config Section.
inline Section
parseConfig(std::string const& s)
{
    Section section;
    std::vector<std::string> values;
    boost::split(values, s, boost::algorithm::is_any_of(","));
    section.append(values);
    return section;
}

// Pre-generate `count` distinct objects from key space `prefix`, starting at
// sequence index `start`.
inline Batch
makePool(std::uint8_t prefix, std::size_t count, std::size_t start = 0)
{
    Sequence seq(prefix);
    Batch pool;
    pool.reserve(count);
    for (auto i = 0uz; i < count; ++i)
        pool.push_back(seq.obj(start + i));
    return pool;
}

// Pre-generate `count` keys disjoint from every `makePool(...)` object, for
// measuring fetches that miss.
inline std::vector<uint256>
makeMissingKeys(std::size_t count)
{
    Sequence seq(2);
    std::vector<uint256> keys;
    keys.reserve(count);
    for (auto i = 0uz; i < count; ++i)
        keys.push_back(seq.key(i));
    return keys;
}

// Mean payload size across a pool, used for SetBytesProcessed throughput.
inline std::size_t
averagePayload(Batch const& pool)
{
    if (pool.empty())
        return 0;
    std::size_t total = 0;
    for (auto const& obj : pool)
        total += obj->getData().size();
    return total / pool.size();
}

// Store every object and flush, so a following fetch exercises the real read
// path rather than an in-memory write buffer.
//
// We chunk the write at kBatchWriteLimitSize because Types.h documents that as
// the maximum allowed batch size. NuDB happens to tolerate larger batches
// today, but the benchmark should not rely on that.
//
// sync() is a no-op for both NuDB and RocksDB at the moment (NuDB has a small
// internal burst buffer that the timed loop will warm up). That is a contract
// hint, not a guarantee; if either backend ever grows a real flush we get it
// here for free.
inline void
prepopulate(Backend& backend, Batch const& objects)
{
    for (std::size_t i = 0; i < objects.size(); i += kBatchWriteLimitSize)
    {
        auto const end = std::min(i + kBatchWriteLimitSize, objects.size());
        backend.storeBatch(Batch(objects.begin() + i, objects.begin() + end));
    }
    backend.sync();
}

// A deterministic permutation of [0, size). Lets the timed loop visit the
// pre-generated pool in a random-like order with zero RNG cost per iteration -
// the Timing_test workloads it replaces used uniform_int_distribution per
// fetch, and a shuffle table reproduces that access pattern without paying for
// the distribution inside the timed region.
inline std::vector<std::size_t>
makeShuffle(std::size_t size, std::uint64_t seed)
{
    std::vector<std::size_t> v(size);
    std::ranges::iota(v, 0uz);
    beast::xor_shift_engine gen(seed);
    std::ranges::shuffle(v, gen);
    return v;
}

// Partition a pool into fixed-size batches. Any trailing remainder shorter than
// `batchSize` is dropped, so every returned batch has exactly `batchSize`.
inline std::vector<Batch>
sliceFixedBatches(Batch const& pool, std::size_t batchSize)
{
    std::vector<Batch> batches;
    if (batchSize == 0)
        return batches;
    batches.reserve(pool.size() / batchSize);
    for (std::size_t i = 0; i + batchSize <= pool.size(); i += batchSize)
        batches.emplace_back(pool.begin() + i, pool.begin() + i + batchSize);
    return batches;
}

/**
 * @brief RAII owner of a NodeStore Backend opened on a private temporary directory.
 */
struct BackendHarness
{
    beast::TempDir tempDir;  ///< Declared first so it is destroyed last
    DummyScheduler scheduler;
    beast::Journal journal{beast::Journal::getNullSink()};
    std::unique_ptr<Backend> backend;

    explicit BackendHarness(std::string const& configString)
    {
        Section config = parseConfig(configString);
        // A private, unique path per harness, so concurrent or repeated runs
        // never share on-disk state.
        config.set("path", tempDir.path());
        backend =
            Manager::instance().makeBackend(config, megabytes(std::size_t{4}), scheduler, journal);
        backend->setDeletePath();
        backend->open();
    }

    ~BackendHarness()
    {
        if (backend)
            backend->close();
    }
};

/**
 * RAII owner of a NodeStore Database - the application-facing wrapper around a
 * Backend, which adds fetch/store accounting and the async read-thread pool.
 */
struct DatabaseHarness
{
    beast::TempDir tempDir;
    DummyScheduler scheduler;
    beast::Journal journal{beast::Journal::getNullSink()};
    std::unique_ptr<Database> db;

    DatabaseHarness(std::string const& configString, int readThreads)
    {
        Section config = parseConfig(configString);
        config.set("path", tempDir.path());
        db = Manager::instance().makeDatabase(
            megabytes(std::size_t{4}), scheduler, readThreads, config, journal);
    }

    ~DatabaseHarness()
    {
        if (db)
            db->stop();
    }
};

// A NodeStore backend to benchmark, named for the --benchmark_filter CLI flag.
struct BackendConfig
{
    char const* name;    // short label, e.g. "nudb"
    char const* config;  // parseConfig() string, e.g. "type=nudb"
};

// The backends every workload is registered against.
//
// The in-memory backend is intentionally excluded. It keeps its table in a
// process-global map keyed by path, with no removal API, so building a fresh
// backend per run - as a microbenchmark must - would leak the whole dataset on
// every run. Timing_test, the suite this benchmark replaces, excluded it for
// the same reason. NuDB and RocksDB are the production backends worth timing.
//
// RocksDB is included only when it was compiled in (xrpl.libxrpl carries
// XRPL_ROCKSDB_AVAILABLE transitively).
inline std::vector<BackendConfig> const&
backendConfigs()
{
    static std::vector<BackendConfig> const kConfigs = {
        {.name = "nudb", .config = "type=nudb"},
#if XRPL_ROCKSDB_AVAILABLE
        {.name = "rocksdb",
         .config = "type=rocksdb,open_files=2000,filter_bits=12,cache_mb=256,"
                   "file_size_mb=8,file_size_mult=2"},
#endif
    };
    return kConfigs;
}

}  // namespace xrpl::node_store
