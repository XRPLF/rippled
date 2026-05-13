/** @file
 *  RocksDB backend implementation for the XRPL NodeStore.
 *
 *  Provides `RocksDBBackend`, a concrete `Backend` that persists serialized
 *  ledger objects in a RocksDB instance, and `RocksDBFactory`, which
 *  registers the backend with the NodeStore plugin registry. The entire
 *  file is compiled only when `XRPL_ROCKSDB_AVAILABLE` is defined. Prefer
 *  NuDB for typical deployments; RocksDB is available as an alternative for
 *  operators with different I/O access patterns.
 */
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <rocksdb/advanced_options.h>
#include <rocksdb/cache.h>
#include <rocksdb/compression_type.h>
#include <rocksdb/convenience.h>
#include <rocksdb/db.h>
#include <rocksdb/env.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>

#include <bit>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if XRPL_ROCKSDB_AVAILABLE
#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/BatchWriter.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>

#include <atomic>
#include <memory>

namespace xrpl::NodeStore {

/** RocksDB environment shim that assigns human-readable names to RocksDB's
 *  internal threads.
 *
 *  RocksDB creates background threads (compaction workers, flush threads)
 *  through its `Env` abstraction. By overriding `StartThread`, this class
 *  intercepts each new thread and calls `beast::setCurrentThreadName` to
 *  assign a stable `"rocksdb #N"` identifier before handing control to the
 *  original entry point. This makes the threads visible in profilers and
 *  crash dumps with no effect on correctness.
 *
 *  A single `RocksDBEnv` instance is shared across all backends created by
 *  the `RocksDBFactory` singleton.
 */
class RocksDBEnv : public rocksdb::EnvWrapper
{
public:
    RocksDBEnv() : EnvWrapper(rocksdb::Env::Default())
    {
    }

    /** Carries the original RocksDB thread function and argument across the
     *  `StartThread` trampoline so they can be invoked after thread naming.
     */
    struct ThreadParams
    {
        ThreadParams(void (*f)(void*), void* a) : f(f), a(a)
        {
        }

        void (*f)(void*);
        void* a;
    };

    /** Trampoline invoked on each new RocksDB thread.
     *
     *  Deletes the heap-allocated `ThreadParams`, assigns a unique
     *  `"rocksdb #N"` thread name via `beast::setCurrentThreadName`, then
     *  calls the original entry point. The monotonically incrementing counter
     *  is process-wide, so names are unique across all `RocksDBEnv` instances.
     *
     *  @param ptr Heap-allocated `ThreadParams*`; ownership is transferred to
     *      this function, which deletes it before calling the original entry.
     */
    static void
    threadEntry(void* ptr)
    {
        ThreadParams const* const p(reinterpret_cast<ThreadParams*>(ptr));
        auto const f = p->f;

        void* a(p->a);
        delete p;

        static std::atomic<std::size_t> kN;
        std::size_t const id(++kN);
        beast::setCurrentThreadName("rocksdb #" + std::to_string(id));

        f(a);
    }

    /** Intercept RocksDB thread creation to install the naming trampoline.
     *
     *  Wraps `f` and `a` in a heap-allocated `ThreadParams` and delegates to
     *  `EnvWrapper::StartThread` with `threadEntry` as the actual entry point.
     *
     *  @param f The thread entry function RocksDB wants to run.
     *  @param a Opaque argument passed through to `f`.
     */
    void
    StartThread(void (*f)(void*), void* a) override
    {
        ThreadParams* const p(new ThreadParams(f, a));
        EnvWrapper::StartThread(&RocksDBEnv::threadEntry, p);
    }
};

//------------------------------------------------------------------------------

/** RocksDB storage backend for the XRPL NodeStore.
 *
 *  Implements both `Backend` (the NodeStore storage contract) and
 *  `BatchWriter::Callback` (the async-write sink). The dual inheritance is
 *  deliberate: `writeBatch()` is visible to `BatchWriter` but not to the
 *  broader `Backend` consumers. Individual `store()` calls are coalesced by
 *  `BatchWriter` and flushed as a single `rocksdb::WriteBatch`, which RocksDB
 *  writes atomically to its WAL.
 *
 *  Configuration accepts legacy defaults that may be silently promoted to
 *  production-appropriate values unless `hard_set = true` is also present.
 *  See the constructor for the full escalation table.
 *
 *  @note `fetch()` and `store()` are called concurrently; all other methods
 *      follow the `Backend` single-caller contract. @see Backend
 */
class RocksDBBackend : public Backend, public BatchWriter::Callback
{
private:
    std::atomic<bool> deletePath_;

public:
    beast::Journal journal;       /**< Logging sink used throughout the backend. */
    size_t const keyBytes;        /**< Fixed key width in bytes (always 32 in production). */
    BatchWriter batch;            /**< Write coalescer; flushes to `writeBatch()`. */
    std::string name;             /**< Filesystem path to the RocksDB directory. */
    std::unique_ptr<rocksdb::DB> db; /**< Owning handle to the open RocksDB instance; null when closed. */
    int fdMinRequired = 2048;     /**< Minimum file descriptors needed; updated from `max_open_files + 128`. */
    rocksdb::Options options;     /**< Fully resolved RocksDB options, built in the constructor. */

    /** Construct and configure a RocksDB backend from key/value config pairs.
     *
     *  Translates the `[node_db]` config section into `rocksdb::Options` and
     *  `rocksdb::BlockBasedTableOptions`. Several settings apply a legacy
     *  default escalation when `hard_set` is absent:
     *  - `cache_mb = 256` is promoted to 1024 MB
     *  - `open_files = 2000` is promoted to 8000
     *  - `file_size_mb = 8` is promoted to 256 MB
     *
     *  After construction the database is not yet open; call `open()` to
     *  start I/O. The resolved `DBOptions` and `ColumnFamilyOptions` are
     *  logged at debug level so operators can verify escalation took effect.
     *
     *  @param keyBytes Fixed key width in bytes; always 32 in production.
     *  @param keyValues Key/value config pairs from the `[node_db]` section.
     *      Must contain a `path` key.
     *  @param scheduler Scheduler for dispatching deferred batch-write tasks.
     *  @param journal Logging sink for diagnostics and error reporting.
     *  @param env Shared `RocksDBEnv` that names background threads; must
     *      outlive this object.
     *  @throws std::runtime_error if `path` is absent, if `bbt_options` or
     *      `options` strings fail to parse, or on any other config error.
     */
    RocksDBBackend(
        int keyBytes,
        Section const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal,
        RocksDBEnv* env)
        : deletePath_(false), journal(journal), keyBytes(keyBytes), batch(*this, scheduler)
    {
        if (!getIfExists(keyValues, "path", name))
            Throw<std::runtime_error>("Missing path in RocksDBFactory backend");

        rocksdb::BlockBasedTableOptions tableOptions;
        options.env = env;

        bool const hardSet = keyValues.exists("hard_set") && get<bool>(keyValues, "hard_set");

        if (keyValues.exists("cache_mb"))
        {
            auto size = get<int>(keyValues, "cache_mb");

            if (!hardSet && size == 256)
                size = 1024;

            tableOptions.block_cache = rocksdb::NewLRUCache(megabytes(size));
        }

        if (auto const v = get<int>(keyValues, "filter_bits"))
        {
            bool const filterBlocks =
                !keyValues.exists("filter_full") || (get<int>(keyValues, "filter_full") == 0);
            tableOptions.filter_policy.reset(rocksdb::NewBloomFilterPolicy(v, filterBlocks));
        }

        if (getIfExists(keyValues, "open_files", options.max_open_files))
        {
            if (!hardSet && options.max_open_files == 2000)
                options.max_open_files = 8000;

            fdMinRequired = options.max_open_files + 128;
        }

        if (keyValues.exists("file_size_mb"))
        {
            auto fileSizeMb = get<int>(keyValues, "file_size_mb");

            if (!hardSet && fileSizeMb == 8)
                fileSizeMb = 256;

            options.target_file_size_base = megabytes(fileSizeMb);
            options.max_bytes_for_level_base = 5 * options.target_file_size_base;
            options.write_buffer_size = 2 * options.target_file_size_base;
        }

        getIfExists(keyValues, "file_size_mult", options.target_file_size_multiplier);

        if (keyValues.exists("bg_threads"))
        {
            options.env->SetBackgroundThreads(get<int>(keyValues, "bg_threads"), rocksdb::Env::LOW);
        }

        if (keyValues.exists("high_threads"))
        {
            auto const highThreads = get<int>(keyValues, "high_threads");
            options.env->SetBackgroundThreads(highThreads, rocksdb::Env::HIGH);

            // If we have high-priority threads, presumably we want to
            // use them for background flushes
            if (highThreads > 0)
                options.max_background_flushes = highThreads;
        }

        options.compression = rocksdb::kSnappyCompression;

        getIfExists(keyValues, "block_size", tableOptions.block_size);

        if (keyValues.exists("universal_compaction") &&
            (get<int>(keyValues, "universal_compaction") != 0))
        {
            options.compaction_style = rocksdb::kCompactionStyleUniversal;
            options.min_write_buffer_number_to_merge = 2;
            options.max_write_buffer_number = 6;
            options.write_buffer_size = 6 * options.target_file_size_base;
        }

        if (keyValues.exists("bbt_options"))
        {
            rocksdb::ConfigOptions const configOptions;
            auto const s = rocksdb::GetBlockBasedTableOptionsFromString(
                configOptions, tableOptions, get(keyValues, "bbt_options"), &tableOptions);
            if (!s.ok())
            {
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB bbt_options: ") + s.ToString());
            }
        }

        options.table_factory.reset(NewBlockBasedTableFactory(tableOptions));

        if (keyValues.exists("options"))
        {
            auto const s =
                rocksdb::GetOptionsFromString(options, get(keyValues, "options"), &options);
            if (!s.ok())
            {
                Throw<std::runtime_error>(
                    std::string("Unable to set RocksDB options: ") + s.ToString());
            }
        }

        std::string s1, s2;
        rocksdb::GetStringFromDBOptions(&s1, options, "; ");
        rocksdb::GetStringFromColumnFamilyOptions(&s2, options, "; ");
        JLOG(journal.debug()) << "RocksDB DBOptions: " << s1;
        JLOG(journal.debug()) << "RocksDB CFOptions: " << s2;
    }

    ~RocksDBBackend() override
    {
        close();
    }

    /** Open the RocksDB database, optionally creating it if absent.
     *
     *  Sets `create_if_missing` on the options and calls `rocksdb::DB::Open`,
     *  adopting the returned raw pointer into `db`. Calling this method on an
     *  already-open backend is a programming error and triggers `UNREACHABLE`.
     *
     *  @param createIfMissing If `true`, create the database directory and
     *      files when they do not exist. Pass `false` to fail fast on a
     *      missing database.
     *  @throws std::runtime_error if RocksDB reports a failure status or
     *      returns a null pointer.
     */
    void
    open(bool createIfMissing) override
    {
        if (db)
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NodeStore::RocksDBBackend::open : database is already "
                "open");
            JLOG(journal.error()) << "database is already open";
            return;
            // LCOV_EXCL_STOP
        }
        rocksdb::DB* localDb = nullptr;
        options.create_if_missing = createIfMissing;
        rocksdb::Status const status = rocksdb::DB::Open(options, name, &localDb);
        if (!status.ok() || (localDb == nullptr))
        {
            Throw<std::runtime_error>(
                std::string("Unable to open/create RocksDB: ") + status.ToString());
        }
        db.reset(localDb);
    }

    /** Return `true` if the database has been opened and not yet closed. */
    bool
    isOpen() override
    {
        return static_cast<bool>(db);
    }

    /** Close the database and, if `setDeletePath()` was called, remove the
     *  directory from the filesystem.
     *
     *  Resetting `db` triggers RocksDB's own cleanup (WAL flush, file close).
     *  Directory removal happens only after RocksDB has cleanly shut down,
     *  ensuring no open file handles are left on the path being deleted.
     *  No-op if the database is already closed.
     */
    void
    close() override
    {
        if (db)
        {
            db.reset();
            if (deletePath_)
            {
                boost::filesystem::path const dir = name;
                boost::filesystem::remove_all(dir);
            }
        }
    }

    /** Return the filesystem path to the RocksDB directory. */
    std::string
    getName() override
    {
        return name;
    }

    //--------------------------------------------------------------------------

    /** Fetch a single object by its 256-bit hash.
     *
     *  Constructs a `rocksdb::Slice` directly over the `uint256` bytes to
     *  avoid copying the key. The value string is parsed by `DecodedBlob`.
     *  Three distinct failure modes are reported:
     *  - `Status::DataCorrupt` — RocksDB reported corruption *or* `DecodedBlob`
     *    rejected the stored value.
     *  - `Status::NotFound` — clean miss; the key is absent.
     *  - `Status::CustomCode + status.code()` — an unexpected RocksDB error;
     *    also logged at error level.
     *
     *  @param hash The 256-bit hash key identifying the object to retrieve.
     *  @param pObject Output parameter; set to the decoded `NodeObject` on
     *      `Status::Ok`, reset to null on any other outcome.
     *  @return The fetch status; see above for the three non-`Ok` cases.
     */
    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) override
    {
        XRPL_ASSERT(db, "xrpl::NodeStore::RocksDBBackend::fetch : non-null database");
        pObject->reset();

        Status status = Status::Ok;

        rocksdb::ReadOptions const options;
        rocksdb::Slice const slice(std::bit_cast<char const*>(hash.data()), keyBytes);

        std::string string;

        rocksdb::Status const getStatus = db->Get(options, slice, &string);

        if (getStatus.ok())
        {
            DecodedBlob decoded(hash.data(), string.data(), string.size());

            if (decoded.wasOk())
            {
                *pObject = decoded.createObject();
            }
            else
            {
                // Decoding failed, probably corrupted!
                //
                status = Status::DataCorrupt;
            }
        }
        else
        {
            if (getStatus.IsCorruption())
            {
                status = Status::DataCorrupt;
            }
            else if (getStatus.IsNotFound())
            {
                status = Status::NotFound;
            }
            else
            {
                status = static_cast<Status>(
                    static_cast<int>(Status::CustomCode) + unsafeCast<int>(getStatus.code()));

                JLOG(journal.error()) << getStatus.ToString();
            }
        }

        return status;
    }

    /** Fetch a batch of objects by their 256-bit hashes.
     *
     *  Serially calls `fetch()` for each hash and inserts a null `shared_ptr`
     *  for any miss or error. The aggregate status is always `Status::Ok`
     *  because per-entry failures are surfaced via null entries in the results
     *  vector, not via the return code.
     *
     *  @note Unlike `storeBatch()`, this operation has no group atomicity — each
     *      hash is fetched independently. RocksDB provides no multi-get
     *      semantics that would change this.
     *  @param hashes Ordered list of 256-bit hash keys to fetch.
     *  @return A pair of (results vector parallel to `hashes`, `Status::Ok`).
     *      A null element at position `i` means `hashes[i]` was not found or
     *      could not be decoded.
     */
    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results;
        results.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            std::shared_ptr<NodeObject> nObj;
            Status const status = fetch(h, &nObj);
            if (status != Status::Ok)
            {
                results.push_back({});
            }
            else
            {
                results.push_back(nObj);
            }
        }

        return {results, Status::Ok};
    }

    /** Enqueue a single object for the next `BatchWriter`-coalesced flush. */
    void
    store(std::shared_ptr<NodeObject> const& object) override
    {
        batch.store(object);
    }

    /** Encode and persist a batch of objects as a single atomic RocksDB write.
     *
     *  Each `NodeObject` is serialized via `EncodedBlob` and packed into a
     *  `rocksdb::WriteBatch`. The entire batch is committed with a single
     *  `db->Write()` call, which RocksDB records atomically in its WAL —
     *  either all objects in the group are recoverable after a crash, or none.
     *
     *  @param batch The collection of `NodeObject`s to persist.
     *  @throws std::runtime_error if `db->Write()` returns a non-ok status.
     */
    void
    storeBatch(Batch const& batch) override
    {
        XRPL_ASSERT(
            db,
            "xrpl::NodeStore::RocksDBBackend::storeBatch : non-null "
            "database");
        rocksdb::WriteBatch wb;

        for (auto const& e : batch)
        {
            EncodedBlob const encoded(e);

            wb.Put(
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getKey()), keyBytes),
                rocksdb::Slice(std::bit_cast<char const*>(encoded.getData()), encoded.getSize()));
        }

        rocksdb::WriteOptions const options;

        auto ret = db->Write(options, &wb);

        if (!ret.ok())
            Throw<std::runtime_error>("storeBatch failed: " + ret.ToString());
    }

    /** No-op durability barrier.
     *
     *  RocksDB's write-ahead log provides crash durability automatically for
     *  every committed `WriteBatch`. An explicit fsync barrier at the NodeStore
     *  level is unnecessary.
     */
    void
    sync() override
    {
    }

    /** Invoke a callback for every object stored in the database.
     *
     *  Creates a plain `rocksdb::Iterator` (no snapshot pinning) and decodes
     *  each entry via `DecodedBlob`. Used exclusively during database import;
     *  per the `Backend` contract it is never called concurrently with other
     *  operations.
     *
     *  Entries with an unexpected key size are logged at fatal level and
     *  skipped rather than thrown, allowing the iterator to continue past
     *  potential on-disk corruption.
     *
     *  @param f Callback invoked once per valid object; receives a
     *      `shared_ptr<NodeObject>` for each successfully decoded entry.
     */
    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        XRPL_ASSERT(db, "xrpl::NodeStore::RocksDBBackend::forEach : non-null database");
        rocksdb::ReadOptions const options;

        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options));

        for (it->SeekToFirst(); it->Valid(); it->Next())
        {
            if (it->key().size() == keyBytes)
            {
                DecodedBlob decoded(it->key().data(), it->value().data(), it->value().size());

                if (decoded.wasOk())
                {
                    f(decoded.createObject());
                }
                else
                {
                    // Uh oh, corrupted data!
                    JLOG(journal.fatal()) << "Corrupt NodeObject #" << it->key().ToString(true);
                }
            }
            else
            {
                // VFALCO NOTE What does it mean to find an
                //             incorrectly sized key? Corruption?
                JLOG(journal.fatal()) << "Bad key size = " << it->key().size();
            }
        }
    }

    /** Return an estimate of the number of pending write operations.
     *
     *  Delegates to `BatchWriter::getWriteLoad()`, which reports the larger of
     *  the item count currently being flushed and the count waiting in the
     *  accumulation buffer.
     *
     *  @return Approximate count of `NodeObject`s awaiting or undergoing write.
     */
    int
    getWriteLoad() override
    {
        return batch.getWriteLoad();
    }

    /** Schedule the on-disk files for deletion when the backend is closed.
     *
     *  Sets an atomic flag; the actual directory removal happens in `close()`
     *  after RocksDB cleanly shuts down. The flag is atomic because this method
     *  may be called from a different thread than the one that destroys the
     *  backend.
     */
    void
    setDeletePath() override
    {
        deletePath_ = true;
    }

    //--------------------------------------------------------------------------

    /** `BatchWriter::Callback` sink — forwards the completed batch to `storeBatch()`.
     *
     *  Called by `BatchWriter` on the scheduler thread once per flush cycle.
     *  The thin delegation keeps batching logic separate from storage logic.
     *
     *  @param batch The coalesced collection of `NodeObject`s to persist.
     */
    void
    writeBatch(Batch const& batch) override
    {
        storeBatch(batch);
    }

    /** Return the number of file descriptors this backend expects to consume.
     *
     *  Computed as `max_open_files + 128` when `open_files` is configured,
     *  giving the process headroom beyond RocksDB's own limit. Defaults to
     *  2048 when `open_files` is not set in the config.
     *
     *  @return Expected file descriptor count for this backend instance.
     */
    [[nodiscard]] int
    fdRequired() const override
    {
        return fdMinRequired;
    }
};

//------------------------------------------------------------------------------

/** NodeStore `Factory` that creates `RocksDBBackend` instances.
 *
 *  Owns a single `RocksDBEnv` shared across all backends it produces, so
 *  RocksDB's background threads are all named through the same counter.
 *  Registers itself with the `Manager` on construction; `Manager::find("RocksDB")`
 *  (case-insensitive) resolves to this factory.
 *
 *  @note Instantiated as a function-local static inside
 *      `registerRocksDBFactory()`, guaranteeing a single registration per
 *      process and safe destruction order relative to the `Manager` singleton.
 */
class RocksDBFactory : public Factory
{
private:
    Manager& manager_;

public:
    RocksDBEnv env; /**< Shared environment; assigns names to all RocksDB background threads. */

    /** Register this factory with the NodeStore `Manager`.
     *
     *  @param manager The singleton `Manager` that this factory is inserted into.
     */
    RocksDBFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    /** Return `"RocksDB"` — the config `type=` string for this backend. */
    [[nodiscard]] std::string
    getName() const override
    {
        return "RocksDB";
    }

    /** Construct an unopened `RocksDBBackend` from configuration.
     *
     *  The `burstSize` parameter is intentionally ignored; RocksDB manages its
     *  own internal buffering independently of any externally imposed burst
     *  limit. Call `Backend::open()` on the returned instance before doing I/O.
     *
     *  @param keyBytes Fixed key width in bytes; always 32 in production.
     *  @param keyValues Key/value pairs from the `[node_db]` config section.
     *  @param scheduler Scheduler for dispatching deferred batch-write tasks.
     *  @param journal Logging sink passed through to the new backend.
     *  @return An unopened, uniquely-owned `RocksDBBackend`.
     */
    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t,
        Scheduler& scheduler,
        beast::Journal journal) override
    {
        return std::make_unique<RocksDBBackend>(keyBytes, keyValues, scheduler, journal, &env);
    }
};

/** Register the RocksDB backend with the NodeStore `Manager` singleton.
 *
 *  Uses a function-local static `RocksDBFactory` to guarantee exactly one
 *  registration per process and correct destruction order: the factory is
 *  initialized after `ManagerImp` and destroyed before it, avoiding
 *  use-after-free on teardown.
 *
 *  @param manager The `Manager` to register with; typically the singleton
 *      returned by `Manager::instance()`.
 */
void
registerRocksDBFactory(Manager& manager)
{
    static RocksDBFactory const kINSTANCE{manager};
}

}  // namespace xrpl::NodeStore

#endif
