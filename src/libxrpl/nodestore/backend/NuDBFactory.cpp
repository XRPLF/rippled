/** @file
 *  NuDB storage backend for the XRPL NodeStore.
 *
 *  Implements `NuDBBackend`, the default persistence engine for `rippled`.
 *  NuDB is an append-mostly, hash-keyed store whose I/O profile matches the
 *  XRPL access pattern (random reads, append writes, no deletes) with lower
 *  write-amplification than general-purpose engines like RocksDB.
 *
 *  Every stored value passes through the codec layer (`detail/codec.h`): LZ4
 *  by default, with a specialized sparse-hash encoding for SHAMap inner nodes.
 *  See `nodeobjectCompress` / `nodeobjectDecompress` for the format details.
 *
 *  `NuDBFactory` registers itself with the global `Manager` singleton so that
 *  a `[node_db]` section specifying `type=NuDB` resolves to this backend.
 */
#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>
#include <xrpl/nodestore/detail/codec.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/detail/errc.hpp>

#include <nudb/context.hpp>
#include <nudb/create.hpp>  // IWYU pragma: keep
#include <nudb/detail/buffer.hpp>
#include <nudb/error.hpp>
#include <nudb/file.hpp>
#include <nudb/progress.hpp>
#include <nudb/store.hpp>
#include <nudb/verify.hpp>  // IWYU pragma: keep
#include <nudb/visit.hpp>   // IWYU pragma: keep
#include <nudb/xxhasher.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::NodeStore {

/** NuDB storage backend for the NodeStore.
 *
 *  Wraps a NuDB database (three on-disk files: `.dat`, `.key`, `.log`) and
 *  implements the `Backend` interface. Construction is lightweight; all I/O is
 *  deferred until `open()`. Every stored value is compressed by the codec layer
 *  before insertion and decompressed inside the `db_.fetch()` callback on read.
 *
 *  @note `fetch()` and `store()` are thread-safe. `forEach()` and `verify()`
 *      close and reopen the database and must not be called concurrently with
 *      any other operation.
 *  @see nodeobjectCompress, nodeobjectDecompress, NuDBFactory
 */
class NuDBBackend : public Backend
{
public:
    /** Application-defined tag stored in the NuDB file header.
     *
     *  Originally used to distinguish shard databases; that code has been
     *  removed. Today the sole purpose is a sanity check on `open()` that the
     *  files were created by xrpld and not by another NuDB application.
     */
    static constexpr std::uint64_t kAPPNUM = 1;

    beast::Journal const j;       /**< Logging sink for diagnostics. */
    size_t const keyBytes;        /**< Fixed key width in bytes (always 32). */
    std::size_t const burstSize;  /**< NuDB in-memory write buffer limit in bytes. */
    std::string const name;       /**< Path to the database directory (from `path=` config). */
    std::size_t const blockSize;  /**< NuDB key-file block size in bytes. */
    nudb::store db;               /**< Underlying NuDB database handle. */
    /** Set to `true` when `close()` should delete the database directory.
     *
     *  Written from a potentially different thread than the one that calls
     *  `close()`, so declared atomic to avoid a data race.
     */
    std::atomic<bool> deletePath;
    Scheduler& scheduler; /**< Async task dispatcher for write telemetry. */

    /** Construct a NuDBBackend without a shared NuDB I/O context.
     *
     *  Parses configuration and validates the `path` key but does not touch
     *  the filesystem. Call `open()` to initialize the database files.
     *
     *  @param keyBytes Fixed key width in bytes (always 32 in production).
     *  @param keyValues Configuration key/value pairs from `[node_db]`.
     *  @param burstSize NuDB in-memory write buffer size in bytes.
     *  @param scheduler Async task dispatcher used for write telemetry.
     *  @param journal Logging sink for backend diagnostics.
     *  @throws std::runtime_error if `path` is absent from `keyValues` or if
     *      `nudb_block_size` is present but invalid.
     */
    NuDBBackend(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal)
        : j(journal)
        , keyBytes(keyBytes)
        , burstSize(burstSize)
        , name(get(keyValues, "path"))
        , blockSize(parseBlockSize(name, keyValues, journal))
        , deletePath(false)
        , scheduler(scheduler)
    {
        if (name.empty())
            Throw<std::runtime_error>("nodestore: Missing path in NuDB backend");
    }

    /** Construct a NuDBBackend sharing an existing NuDB I/O context.
     *
     *  The `nudb::context` owns background I/O threads used by NuDB for
     *  asynchronous buffering. Providing one enables shared I/O across
     *  multiple backends (e.g., when several shards are open simultaneously).
     *  Apart from the shared context, construction semantics are identical to
     *  the context-free overload.
     *
     *  @param keyBytes Fixed key width in bytes.
     *  @param keyValues Configuration key/value pairs from `[node_db]`.
     *  @param burstSize NuDB in-memory write buffer size in bytes.
     *  @param scheduler Async task dispatcher for write telemetry.
     *  @param context Shared NuDB I/O thread pool.
     *  @param journal Logging sink for backend diagnostics.
     *  @throws std::runtime_error if `path` is absent or `nudb_block_size` is invalid.
     */
    NuDBBackend(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        nudb::context& context,
        beast::Journal journal)
        : j(journal)
        , keyBytes(keyBytes)
        , burstSize(burstSize)
        , name(get(keyValues, "path"))
        , blockSize(parseBlockSize(name, keyValues, journal))
        , db(context)
        , deletePath(false)
        , scheduler(scheduler)
    {
        if (name.empty())
            Throw<std::runtime_error>("nodestore: Missing path in NuDB backend");
    }

    /** Close the database and optionally delete its directory.
     *
     *  Any `nudb::system_error` thrown by `close()` is caught and suppressed
     *  because destructors must not propagate exceptions. The error is already
     *  logged at `fatal` level inside `close()` before being swallowed here.
     */
    ~NuDBBackend() override
    {
        try
        {
            close();
        }
        catch (nudb::system_error const&)  // NOLINT(bugprone-empty-catch)
        {
            // Don't allow exceptions to propagate out of destructors.
            // close() has already logged the error.
        }
    }

    /** Return the database directory path used in diagnostics. */
    std::string
    getName() override
    {
        return name;
    }

    /** Return the NuDB key-file block size in bytes.
     *
     *  Callers can use this to make storage-layout decisions without
     *  downcasting. Always set at construction from `nudb_block_size` config
     *  or the filesystem-native default (typically 4096).
     *
     *  @return Key-file block size in bytes.
     */
    [[nodiscard]] std::optional<std::size_t>
    getBlockSize() const override
    {
        return blockSize;
    }

    /** Open the backend with deterministic NuDB header parameters.
     *
     *  When `createIfMissing` is `true` the three NuDB files are created with
     *  the given `appType`, `uid`, and `salt` embedded in their headers. If the
     *  files already exist the `file_exists` error is silently cleared and
     *  `db_.open()` proceeds normally — creation is idempotent. After opening,
     *  `appnum` is checked against `kAPPNUM` to confirm the files belong to
     *  xrpld, and `db_.set_burst()` is called to configure the in-memory write
     *  buffer.
     *
     *  @param createIfMissing Create the database directory and files if absent.
     *  @param appType Application type tag written into the NuDB file header.
     *  @param uid Deterministic unique identifier for this database instance.
     *  @param salt Deterministic salt used during NuDB key-file construction.
     *  @throws nudb::system_error on I/O failure during creation or open.
     *  @throws std::runtime_error if the existing database `appnum` does not
     *      equal `kAPPNUM`.
     */
    void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt) override
    {
        using namespace boost::filesystem;
        if (db.is_open())
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NodeStore::NuDBBackend::open : database is already "
                "open");
            JLOG(j.error()) << "database is already open";
            return;
            // LCOV_EXCL_STOP
        }
        auto const folder = path(name);
        auto const dp = (folder / "nudb.dat").string();
        auto const kp = (folder / "nudb.key").string();
        auto const lp = (folder / "nudb.log").string();
        nudb::error_code ec;
        if (createIfMissing)
        {
            create_directories(folder);
            nudb::create<nudb::xxhasher>(
                dp, kp, lp, appType, uid, salt, keyBytes, blockSize, 0.50, ec);
            if (ec == nudb::errc::file_exists)
                ec = {};
            if (ec)
                Throw<nudb::system_error>(ec);
        }
        db.open(dp, kp, lp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);

        if (db.appnum() != kAPPNUM)
            Throw<std::runtime_error>("nodestore: unknown appnum");
        db.set_burst(burstSize);
    }

    /** Return `true` if the underlying NuDB database is currently open. */
    bool
    isOpen() override
    {
        return db.is_open();
    }

    /** Open the backend with randomly generated uid and salt.
     *
     *  Delegates to the four-argument overload using `nudb::make_uid()` and
     *  `nudb::make_salt()` for the identifier fields. Appropriate for the main
     *  node store where exactly one instance is created for a given path and
     *  reproducible identifiers are not required.
     *
     *  @param createIfMissing Create the database files if they do not exist.
     *  @throws nudb::system_error on I/O failure.
     *  @throws std::runtime_error if an existing database has an unexpected appnum.
     */
    void
    open(bool createIfMissing) override
    {
        open(createIfMissing, kAPPNUM, nudb::make_uid(), nudb::make_salt());
    }

    /** Flush pending writes and close all NuDB files.
     *
     *  A NuDB close failure is logged at `fatal` level before the exception is
     *  thrown, ensuring the error reaches the operator even if the caller
     *  catches and discards the exception (as the destructor does). If
     *  `deletePath` was set, the entire database directory is removed with
     *  `boost::filesystem::remove_all()` after a successful close; removal
     *  failures are logged at `fatal` but do not throw.
     *
     *  @throws nudb::system_error if NuDB reports an error during close.
     */
    void
    close() override
    {
        if (db.is_open())
        {
            nudb::error_code ec;
            db.close(ec);
            if (ec)
            {
                // Log to make sure the nature of the error gets to the user.
                JLOG(j.fatal()) << "NuBD close() failed: " << ec.message();
                Throw<nudb::system_error>(ec);
            }

            if (deletePath)
            {
                boost::filesystem::remove_all(name, ec);
                if (ec)
                {
                    JLOG(j.fatal())
                        << "Filesystem remove_all of " << name << " failed with: " << ec.message();
                }
            }
        }
    }

    /** Fetch a single object by its 256-bit hash.
     *
     *  Calls `db_.fetch()` with a lambda callback that receives a raw pointer
     *  into NuDB's internal read buffer (valid only for the callback's
     *  duration). Decompression via `nodeobjectDecompress()` and
     *  `DecodedBlob::createObject()` both happen inside the callback — the
     *  zero-copy design means no intermediate heap allocation for the raw blob.
     *
     *  @param hash The 256-bit hash key identifying the object.
     *  @param pno Output parameter; set to the reconstructed `NodeObject` on
     *      success, or reset on any non-Ok outcome.
     *  @return `Status::Ok` on success, `Status::NotFound` if the key is
     *      absent, or `Status::DataCorrupt` if the stored blob fails
     *      `DecodedBlob` validation.
     *  @throws nudb::system_error on unexpected I/O errors.
     */
    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pno) override
    {
        Status status = Status::Ok;
        pno->reset();
        nudb::error_code ec;
        db.fetch(
            hash.data(),
            [&hash, pno, &status](void const* data, std::size_t size) {
                nudb::detail::buffer bf;
                auto const result = nodeobjectDecompress(data, size, bf);
                DecodedBlob decoded(hash.data(), result.first, result.second);
                if (!decoded.wasOk())
                {
                    status = Status::DataCorrupt;
                    return;
                }
                *pno = decoded.createObject();
                status = Status::Ok;
            },
            ec);
        if (ec == nudb::error::key_not_found)
            return Status::NotFound;
        if (ec)
            Throw<nudb::system_error>(ec);
        return status;
    }

    /** Fetch a batch of objects by their 256-bit hashes.
     *
     *  Implemented as a sequential loop over individual `fetch()` calls —
     *  NuDB provides no native batch-read operation. Missing or corrupt entries
     *  produce empty slots (`{}`) in the result vector rather than aborting
     *  the entire batch; the aggregate status is always `Status::Ok`.
     *
     *  @param hashes Ordered list of 256-bit hash keys to fetch.
     *  @return A pair of (results vector, `Status::Ok`). Each element in the
     *      results vector is the fetched `NodeObject`, or an empty
     *      `shared_ptr` if the corresponding hash was not found or corrupted.
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

    /** Compress and insert a single `NodeObject` into the NuDB store.
     *
     *  The write path is: `EncodedBlob` serializes the object to raw bytes,
     *  `nodeobjectCompress()` applies LZ4 or inner-node sparse encoding, then
     *  `db_.insert()` appends to the data file. A `key_exists` error from
     *  NuDB is silently discarded — the store is content-addressed, so the
     *  same hash always maps to the same data.
     *
     *  @param no The `NodeObject` to serialize and store.
     *  @throws nudb::system_error on any NuDB error other than `key_exists`.
     */
    void
    doInsert(std::shared_ptr<NodeObject> const& no)
    {
        EncodedBlob const e(no);
        nudb::error_code ec;
        nudb::detail::buffer bf;
        auto const result = nodeobjectCompress(e.getData(), e.getSize(), bf);
        db.insert(e.getKey(), result.first, result.second, ec);
        if (ec && ec != nudb::error::key_exists)
            Throw<nudb::system_error>(ec);
    }

    /** Store a single object and report write telemetry to the scheduler.
     *
     *  NuDB writes are synchronous — `doInsert()` returns only after the data
     *  is in the NuDB write buffer. There is no internal queue; `getWriteLoad()`
     *  therefore always returns 0.
     *
     *  @param no The `NodeObject` to persist.
     *  @throws nudb::system_error on I/O failure.
     */
    void
    store(std::shared_ptr<NodeObject> const& no) override
    {
        BatchWriteReport report{};
        report.writeCount = 1;
        auto const start = std::chrono::steady_clock::now();
        doInsert(no);
        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        scheduler.onBatchWrite(report);
    }

    /** Store a batch of objects as sequential synchronous inserts.
     *
     *  Unlike RocksDB's `WriteBatch`, NuDB has no native atomic multi-write
     *  operation. Each object is inserted via `doInsert()` in order. Telemetry
     *  covers the entire batch as a single `BatchWriteReport`.
     *
     *  @param batch The collection of `NodeObject`s to persist.
     *  @throws nudb::system_error if any individual insert fails.
     */
    void
    storeBatch(Batch const& batch) override
    {
        BatchWriteReport report{};
        report.writeCount = batch.size();
        auto const start = std::chrono::steady_clock::now();
        for (auto const& e : batch)
            doInsert(e);
        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        scheduler.onBatchWrite(report);
    }

    /** No-op: NuDB's write-ahead log provides implicit durability. */
    void
    sync() override
    {
    }

    /** Invoke a callback for every object in the database.
     *
     *  `nudb::visit()` reads the data file sequentially, which is incompatible
     *  with concurrent access through a live `nudb::store`. This method
     *  therefore closes the database before visiting and reopens it afterward.
     *  It must not be called while any other I/O operation is in flight.
     *
     *  @param f Callback invoked once per stored object with the reconstructed
     *      `shared_ptr<NodeObject>`. Entries that fail `DecodedBlob` validation
     *      are skipped and cause `nudb::error::missing_value` to be set,
     *      aborting the visit.
     *  @throws nudb::system_error on close, visit, or reopen failure.
     */
    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        auto const dp = db.dat_path();
        auto const kp = db.key_path();
        auto const lp = db.log_path();
        nudb::error_code ec;
        db.close(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        nudb::visit(
            dp,
            [&](void const* key,
                std::size_t keyBytes,
                void const* data,
                std::size_t size,
                nudb::error_code&) {
                nudb::detail::buffer bf;
                auto const result = nodeobjectDecompress(data, size, bf);
                DecodedBlob decoded(key, result.first, result.second);
                if (!decoded.wasOk())
                {
                    ec = make_error_code(nudb::error::missing_value);
                    return;
                }
                f(decoded.createObject());
            },
            nudb::no_progress{},
            ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        db.open(dp, kp, lp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
    }

    /** Return 0: NuDB writes are synchronous with no internal queue. */
    int
    getWriteLoad() override
    {
        return 0;
    }

    /** Schedule the database directory for deletion on the next `close()`.
     *
     *  Thread-safe: `deletePath` is `std::atomic<bool>` to allow this to be
     *  called from a different thread than the one that calls `close()`.
     */
    void
    setDeletePath() override
    {
        deletePath = true;
    }

    /** Perform an offline consistency check of the NuDB key and data files.
     *
     *  `nudb::verify<nudb::xxhasher>()` re-hashes every stored key and
     *  confirms it matches the key-file index. The `xxhasher` template
     *  parameter must match the one used at database creation time.
     *
     *  Because `nudb::verify` needs exclusive file access the live database is
     *  closed before the check and reopened afterward. Must not be called
     *  concurrently with any other operation.
     *
     *  @throws nudb::system_error on close, verification failure, or reopen.
     *  @note Not currently called at startup; if it were, on-disk corruption
     *      could be caught before it causes a runtime crash.
     */
    void
    verify() override
    {
        auto const dp = db.dat_path();
        auto const kp = db.key_path();
        auto const lp = db.log_path();
        nudb::error_code ec;
        db.close(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        nudb::verify_info vi;
        nudb::verify<nudb::xxhasher>(vi, dp, kp, 0, nudb::no_progress{}, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        db.open(dp, kp, lp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
    }

    /** Return 3: the data, key, and log files NuDB keeps open simultaneously.
     *
     *  The `Database` base class aggregates this value across all backends so
     *  the process can verify its file-descriptor budget before opening any
     *  databases.
     *
     *  @return 3
     */
    [[nodiscard]] int
    fdRequired() const override
    {
        return 3;
    }

private:
    /** Parse and validate the `nudb_block_size` configuration key.
     *
     *  Returns the filesystem-native block size (via `nudb::block_size()`,
     *  typically 4096 bytes) if the key is absent. When present, the value
     *  must be a power of 2 in the range [4096, 32768]; values outside this
     *  range cause I/O amplification or exceed NuDB's supported limits. A
     *  valid custom size is logged at `info` level.
     *
     *  @param name Database directory path (used to derive the key-file path
     *      for the `nudb::block_size()` default lookup).
     *  @param keyValues Configuration key/value pairs from `[node_db]`.
     *  @param journal Logging sink for the custom-size info message.
     *  @return Block size in bytes to use for the NuDB key file.
     *  @throws std::runtime_error if `nudb_block_size` is present but not a
     *      valid power-of-2 integer in [4096, 32768], or cannot be parsed.
     */
    static std::size_t
    parseBlockSize(std::string const& name, Section const& keyValues, beast::Journal journal)
    {
        using namespace boost::filesystem;
        auto const folder = path(name);
        auto const kp = (folder / "nudb.key").string();

        std::size_t const defaultSize = nudb::block_size(kp);
        std::size_t const blockSize = defaultSize;
        std::string blockSizeStr;

        if (!getIfExists(keyValues, "nudb_block_size", blockSizeStr))
            return blockSize;

        try
        {
            std::size_t const parsedBlockSize = beast::lexicalCastThrow<std::size_t>(blockSizeStr);

            if (parsedBlockSize < 4096 || parsedBlockSize > 32768 ||
                (parsedBlockSize & (parsedBlockSize - 1)) != 0)
            {
                std::stringstream s;
                s << "Invalid nudb_block_size: " << parsedBlockSize
                  << ". Must be power of 2 between 4096 and 32768.";
                Throw<std::runtime_error>(s.str());
            }

            JLOG(journal.info()) << "Using custom NuDB block size: " << parsedBlockSize << " bytes";
            return parsedBlockSize;
        }
        catch (std::exception const& e)
        {
            std::stringstream s;
            s << "Invalid nudb_block_size value: " << blockSizeStr << ". Error: " << e.what();
            Throw<std::runtime_error>(s.str());
        }
    }
};

//------------------------------------------------------------------------------

/** `Factory` implementation that creates `NuDBBackend` instances.
 *
 *  Registers itself with the global `Manager` at construction time so that
 *  `[node_db]` sections specifying `type=NuDB` (case-insensitive) resolve to
 *  this factory. The instance is held as a function-local static by
 *  `registerNuDBFactory()`, giving it program lifetime and safe destruction
 *  order relative to `Manager`.
 *
 *  @see NuDBBackend, registerNuDBFactory, Manager
 */
class NuDBFactory : public Factory
{
private:
    Manager& manager_;

public:
    /** Register this factory with `manager` on construction. */
    explicit NuDBFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    /** Return `"NuDB"` — the configuration type string for this backend. */
    [[nodiscard]] std::string
    getName() const override
    {
        return "NuDB";
    }

    /** Construct a `NuDBBackend` without a shared NuDB I/O context.
     *
     *  @param keyBytes Fixed key width in bytes (always 32 in production).
     *  @param keyValues Configuration key/value pairs from `[node_db]`.
     *  @param burstSize NuDB in-memory write buffer size in bytes.
     *  @param scheduler Async task dispatcher for write telemetry.
     *  @param journal Logging sink for backend diagnostics.
     *  @return An unopened `NuDBBackend` instance.
     */
    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override
    {
        return std::make_unique<NuDBBackend>(keyBytes, keyValues, burstSize, scheduler, journal);
    }

    /** Construct a `NuDBBackend` sharing an existing NuDB I/O context.
     *
     *  Enables shared background I/O threads across multiple backends (e.g.,
     *  when several shards are open simultaneously).
     *
     *  @param keyBytes Fixed key width in bytes.
     *  @param keyValues Configuration key/value pairs from `[node_db]`.
     *  @param burstSize NuDB in-memory write buffer size in bytes.
     *  @param scheduler Async task dispatcher for write telemetry.
     *  @param context Shared NuDB I/O thread pool.
     *  @param journal Logging sink for backend diagnostics.
     *  @return An unopened `NuDBBackend` instance.
     */
    std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        nudb::context& context,
        beast::Journal journal) override
    {
        return std::make_unique<NuDBBackend>(
            keyBytes, keyValues, burstSize, scheduler, context, journal);
    }
};

/** Register the NuDB backend factory with the global NodeStore `Manager`.
 *
 *  Creates a function-local `static NuDBFactory` instance, which registers
 *  itself with `manager` via `Manager::insert()` in its constructor. The
 *  function-local static has program lifetime and initializes exactly once,
 *  guaranteeing that the factory outlives any backend instance it creates and
 *  is safely destroyed after `Manager` (reverse construction order).
 *
 *  Called once at program startup from `ManagerImp`'s constructor.
 *
 *  @param manager The global `Manager` singleton to register with.
 */
void
registerNuDBFactory(Manager& manager)
{
    static NuDBFactory const kINSTANCE{manager};
}

}  // namespace xrpl::NodeStore
