#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/nodestore/WriteStats.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>
#include <xrpl/nodestore/detail/codec.h>

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
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace xrpl::node_store {

class NuDBBackend : public Backend
{
public:
    // "appnum" is an application-defined constant stored in the header of a
    // NuDB database. We used it to identify shard databases before that code
    // was removed. For now, its only use is a sanity check that the database
    // was created by xrpld.
    static constexpr std::uint64_t kAppNum = 1;

    beast::Journal const j;
    size_t const keyBytes;
    std::size_t const burstSize;
    std::string const name;
    std::size_t const blockSize;
    nudb::store db;
    std::atomic<bool> deletePath;
    Scheduler& scheduler;

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Writers currently inside doInsert. Instantaneous depth.
     */
    std::atomic<std::uint64_t> concurrentWriters{0};

    /**
     * Completed inserts.
     */
    std::atomic<std::uint64_t> insertCount{0};

    /**
     * Summed insert wall time, microseconds.
     */
    std::atomic<std::uint64_t> insertTotalUs{0};

    /**
     * Longest single insert, microseconds. Maintained as a true maximum.
     */
    std::atomic<std::uint64_t> insertMaxUs{0};

    /**
     * Summed depth observed at each insert, accumulated at insert entry.
     */
    std::atomic<std::uint64_t> depthSum{0};

    /**
     * How many depth samples make up @ref depthSum.
     *
     * Its own counter rather than reusing the completed-insert count, because
     * the two are taken at different moments: a depth sample exists as soon
     * as an insert starts, while the insert count only rises when one
     * finishes. Dividing by the wrong one biases the mean downward under
     * load, which is when the mean matters.
     */
    std::atomic<std::uint64_t> depthSamples{0};
#endif  // XRPL_ENABLE_TELEMETRY

    NuDBBackend(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal)
        : j(journal)
        , keyBytes(keyBytes)
        , burstSize(burstSize)
        , name(get(keyValues, Keys::kPath))
        , blockSize(parseBlockSize(name, keyValues, journal))
        , deletePath(false)
        , scheduler(scheduler)
    {
        if (name.empty())
            Throw<std::runtime_error>("nodestore: Missing path in NuDB backend");
    }

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
        , name(get(keyValues, Keys::kPath))
        , blockSize(parseBlockSize(name, keyValues, journal))
        , db(context)
        , deletePath(false)
        , scheduler(scheduler)
    {
        if (name.empty())
            Throw<std::runtime_error>("nodestore: Missing path in NuDB backend");
    }

    ~NuDBBackend() override
    {
        try
        {
            // close can throw and we don't want the destructor to throw.
            close();
        }
        catch (nudb::system_error const&)  // NOLINT(bugprone-empty-catch)
        {
            // Don't allow exceptions to propagate out of destructors.
            // close() has already logged the error.
        }
    }

    std::string
    getName() override
    {
        return name;
    }

    [[nodiscard]] std::optional<std::size_t>
    getBlockSize() const override
    {
        return blockSize;
    }

    void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt) override
    {
        using namespace std::filesystem;
        if (db.is_open())
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::node_store::NuDBBackend::open : database is already "
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

        if (db.appnum() != kAppNum)
            Throw<std::runtime_error>("nodestore: unknown appnum");
        db.set_burst(burstSize);
    }

    bool
    isOpen() override
    {
        return db.is_open();
    }

    void
    open(bool createIfMissing) override
    {
        open(createIfMissing, kAppNum, nudb::make_uid(), nudb::make_salt());
    }

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
                std::error_code fsec;
                std::filesystem::remove_all(name, fsec);
                if (fsec)
                {
                    JLOG(j.fatal()) << "Filesystem remove_all of " << name
                                    << " failed with: " << fsec.message();
                }
            }
        }
    }

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

    void
    doInsert(std::shared_ptr<NodeObject> const& no)
    {
        EncodedBlob const e(no);
        nudb::error_code ec;
        nudb::detail::buffer bf;
        auto const result = nodeobjectCompress(e.getData(), e.getSize(), bf);

#ifdef XRPL_ENABLE_TELEMETRY
        // NuDB takes one global mutex for the whole insert, so the wait is
        // invisible from here. Record the depth we joined at and the wall
        // time we spent; the split follows from Little's Law.
        //
        // Depth and its sample count are both folded in HERE, at entry, so
        // the mean is over the same population. Counting the sample at exit
        // instead would drop every insert still in flight, and those are the
        // slow, deep ones -- biasing the mean down exactly when queueing is
        // worst. With all writers inside their first insert the exit-counted
        // version reports no depth at all.
        //
        // Every node write reaches here, so none of it happens without
        // telemetry: this whole block is what the write-stats gauges need and
        // nothing else reads it.
        auto const depth = concurrentWriters.fetch_add(1, std::memory_order_relaxed) + 1;
        depthSum.fetch_add(depth, std::memory_order_relaxed);
        depthSamples.fetch_add(1, std::memory_order_relaxed);
        auto const begin = std::chrono::steady_clock::now();

        // A scope guard rather than straight-line code, because the insert
        // can allocate and so can throw. Leaking the depth would strand the
        // gauge above zero for the life of the process.
        ScopeExit const account([this, begin] { recordInsert(begin); });
#endif

        db.insert(e.getKey(), result.first, result.second, ec);

        if (ec && ec != nudb::error::key_exists)
            Throw<nudb::system_error>(ec);
    }

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

    void
    sync() override
    {
    }

    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        auto const dp = db.dat_path();
        auto const kp = db.key_path();
        auto const lp = db.log_path();
        // auto const appnum = db_.appnum();
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

    int
    getWriteLoad() override
    {
#ifdef XRPL_ENABLE_TELEMETRY
        // Writers in flight. Bounded by the number of writing threads, so
        // it stays far below LedgerMaster's kMaxWriteLoadAcquire of 8192
        // and cannot suppress history acquisition.
        return static_cast<int>(concurrentWriters.load(std::memory_order_relaxed));
#else
        // Nothing counts writers without telemetry, so report no load rather
        // than a stale zero-valued counter. LedgerMaster gates history
        // acquisition on this, so it must not start reporting a real depth as
        // a side effect of instrumentation.
        return 0;
#endif
    }

    [[nodiscard]] std::optional<WriteStats>
    getWriteStats() const override
    {
#ifndef XRPL_ENABLE_TELEMETRY
        // Not measured in this build, which is a different answer from
        // measured-and-idle. The base class reports absence the same way for
        // backends that never queue.
        return std::nullopt;
#else
        WriteStats stats;
        stats.concurrentWriters = concurrentWriters.load(std::memory_order_relaxed);
        stats.insertCount = insertCount.load(std::memory_order_relaxed);
        stats.insertTotalUs = insertTotalUs.load(std::memory_order_relaxed);
        stats.insertMaxUs = insertMaxUs.load(std::memory_order_relaxed);
        stats.depthSum = depthSum.load(std::memory_order_relaxed);
        stats.depthSamples = depthSamples.load(std::memory_order_relaxed);
        return stats;
#endif
    }

    void
    setDeletePath() override
    {
        deletePath = true;
    }

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

    [[nodiscard]] int
    fdRequired() const override
    {
        return 3;
    }

private:
#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Fold one finished insert into the write-path counters.
     *
     * Always runs, including on the throwing path, so the depth gauge
     * returns to its true value even when the insert fails.
     *
     * Declared only with telemetry compiled in, because every counter it
     * touches is, and its single caller is inside the same guard.
     *
     * @param begin When the insert started.
     */
    void
    recordInsert(std::chrono::steady_clock::time_point begin) noexcept
    {
        auto const elapsedUs =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - begin)
                                           .count());

        concurrentWriters.fetch_sub(1, std::memory_order_relaxed);
        insertCount.fetch_add(1, std::memory_order_relaxed);
        insertTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);

        // std::atomic has no fetch_max, so raise the maximum with a CAS
        // loop. Mirrors the clamp loop in OTelCollector.cpp.
        auto current = insertMaxUs.load(std::memory_order_relaxed);
        while (elapsedUs > current &&
               !insertMaxUs.compare_exchange_weak(current, elapsedUs, std::memory_order_relaxed))
        {
        }
    }
#endif

    static std::size_t
    parseBlockSize(std::string const& name, Section const& keyValues, beast::Journal journal)
    {
        using namespace std::filesystem;
        auto const folder = path(name);
        auto const kp = (folder / "nudb.key").string();

        std::size_t const defaultSize = nudb::block_size(kp);  // Default 4K from NuDB
        std::size_t const blockSize = defaultSize;
        std::string blockSizeStr;

        if (!getIfExists(keyValues, Keys::kNudbBlockSize, blockSizeStr))
        {
            return blockSize;  // Early return with default
        }

        try
        {
            auto const parsedBlockSize = beast::lexicalCastThrow<std::size_t>(blockSizeStr);

            // Validate: must be power of 2 between 4K and 32K
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

class NuDBFactory : public Factory
{
private:
    Manager& manager_;

public:
    explicit NuDBFactory(Manager& manager) : manager_(manager)
    {
        manager_.insert(*this);
    }

    [[nodiscard]] std::string
    getName() const override
    {
        return "NuDB";
    }

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

void
registerNuDBFactory(Manager& manager)
{
    static NuDBFactory const kInstance{manager};
}

}  // namespace xrpl::node_store
