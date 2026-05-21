#pragma once

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

// Shared helpers for the NodeStore benchmarks.
//
// The deterministic data generators (`rngcpy`, `Sequence`) were moved here from
// `src/test/nodestore/Timing_test.cpp`: this benchmark reaches parity with that
// hand-rolled timing suite, which is then retired. There is deliberately no
// shared header with the soon-to-be-deleted test.
namespace xrpl::NodeStore {

// Fill `bytes` of memory at `buffer` with random bits drawn from `g`.
template <class Generator>
inline void
rngcpy(void* buffer, std::size_t bytes, Generator& g)
{
    using result_type = typename Generator::result_type;
    while (bytes >= sizeof(result_type))
    {
        auto const v = g();
        std::memcpy(buffer, &v, sizeof(v));
        buffer = reinterpret_cast<std::uint8_t*>(buffer) + sizeof(v);
        bytes -= sizeof(v);
    }

    if (bytes > 0)
    {
        auto const v = g();
        std::memcpy(buffer, &v, bytes);
    }
}

/** Deterministic generator of a reproducible sequence of random NodeObjects.

    Indexing is stable: `obj(n)` and `key(n)` always return the same value for a
    given `n`, regardless of call order, because the engine is reseeded from `n`
    on every call.

    `prefix` selects a key space. The benchmarks use prefix 1 for the "present"
    objects that get stored, and prefix 2 for the "missing" keys that never are.
    `obj(n)` and `key(n)` lay their bytes out differently, so an object's hash
    and a same-index key never collide - that is what keeps the two spaces
    disjoint for the fetch-miss workloads.
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
    uint256
    key(std::size_t n)
    {
        gen_.seed(n + 1);
        uint256 result;
        rngcpy(&*result.begin(), result.size(), gen_);
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
            b.emplace_back(obj(n++));
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
    for (std::size_t i = 0; i < count; ++i)
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
    for (std::size_t i = 0; i < count; ++i)
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
inline void
prepopulate(Backend& backend, Batch const& objects)
{
    backend.storeBatch(objects);
    backend.sync();
}

// Partition a pool into fixed-size batches. Any trailing remainder shorter than
// `batchSize` is dropped, so every returned batch has exactly `batchSize`.
inline std::vector<Batch>
sliceBatches(Batch const& pool, std::size_t batchSize)
{
    std::vector<Batch> batches;
    if (batchSize == 0)
        return batches;
    batches.reserve(pool.size() / batchSize);
    for (std::size_t i = 0; i + batchSize <= pool.size(); i += batchSize)
        batches.emplace_back(pool.begin() + i, pool.begin() + i + batchSize);
    return batches;
}

// Like sliceBatches, but yields the hashes only - the input to fetchBatch.
inline std::vector<std::vector<uint256>>
sliceHashes(Batch const& pool, std::size_t batchSize)
{
    std::vector<std::vector<uint256>> batches;
    if (batchSize == 0)
        return batches;
    batches.reserve(pool.size() / batchSize);
    for (std::size_t i = 0; i + batchSize <= pool.size(); i += batchSize)
    {
        std::vector<uint256> hashes;
        hashes.reserve(batchSize);
        for (std::size_t j = i; j < i + batchSize; ++j)
            hashes.push_back(pool[j]->getHash());
        batches.push_back(std::move(hashes));
    }
    return batches;
}

/** RAII owner of a NodeStore Backend opened on a private temporary directory.

    Member declaration order matters: `tempDir` is declared first so it is
    destroyed last, after the backend has closed and released its files.
*/
struct BackendHarness
{
    beast::TempDir tempDir;
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

    BackendHarness(BackendHarness const&) = delete;
    BackendHarness&
    operator=(BackendHarness const&) = delete;
};

/** RAII owner of a NodeStore Database - the application-facing wrapper around a
    Backend, which adds fetch/store accounting and the async read-thread pool.
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

    DatabaseHarness(DatabaseHarness const&) = delete;
    DatabaseHarness&
    operator=(DatabaseHarness const&) = delete;
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

}  // namespace xrpl::NodeStore
