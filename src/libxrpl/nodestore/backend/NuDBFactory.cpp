#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>
#include <xrpl/nodestore/detail/codec.h>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/filesystem.hpp>

#include <nudb/nudb.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

namespace xrpl {
namespace NodeStore {

class NuDBBackend : public Backend
{
public:
    // "appnum" is an application-defined constant stored in the header of a
    // NuDB database. We used it to identify shard databases before that code
    // was removed. For now, its only use is a sanity check that the database
    // was created by xrpld.
    static constexpr std::uint64_t appnum = 1;

    beast::Journal const j_;
    size_t const keyBytes_;
    std::size_t const burstSize_;
    std::string const name_;
    std::size_t const blockSize_;
    nudb::store db_;
    std::atomic<bool> deletePath_;
    Scheduler& scheduler_;
    boost::asio::thread_pool threadPool_;

    NuDBBackend(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal)
        : j_(journal)
        , keyBytes_(keyBytes)
        , burstSize_(burstSize)
        , name_(get(keyValues, "path"))
        , blockSize_(parseBlockSize(name_, keyValues, journal))
        , deletePath_(false)
        , scheduler_(scheduler)
        , threadPool_(numHardwareThreads)
    {
        if (name_.empty())
            Throw<std::runtime_error>("nodestore: Missing path in NuDB backend");
    }

    NuDBBackend(
        size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        nudb::context& context,
        beast::Journal journal)
        : j_(journal)
        , keyBytes_(keyBytes)
        , burstSize_(burstSize)
        , name_(get(keyValues, "path"))
        , blockSize_(parseBlockSize(name_, keyValues, journal))
        , db_(context)
        , deletePath_(false)
        , scheduler_(scheduler)
        , threadPool_(numHardwareThreads)
    {
        if (name_.empty())
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
        return name_;
    }

    std::optional<std::size_t>
    getBlockSize() const override
    {
        return blockSize_;
    }

    void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt) override
    {
        using namespace boost::filesystem;
        if (db_.is_open())
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "xrpl::NodeStore::NuDBBackend::open : database is already "
                "open");
            JLOG(j_.error()) << "database is already open";
            return;
            // LCOV_EXCL_STOP
        }
        auto const folder = path(name_);
        auto const dp = (folder / "nudb.dat").string();
        auto const kp = (folder / "nudb.key").string();
        auto const lp = (folder / "nudb.log").string();
        nudb::error_code ec;
        if (createIfMissing)
        {
            create_directories(folder);
            nudb::create<nudb::xxhasher>(
                dp, kp, lp, appType, uid, salt, keyBytes_, blockSize_, 0.50, ec);
            if (ec == nudb::errc::file_exists)
                ec = {};
            if (ec)
                Throw<nudb::system_error>(ec);
        }
        db_.open(dp, kp, lp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);

        if (db_.appnum() != appnum)
            Throw<std::runtime_error>("nodestore: unknown appnum");
        db_.set_burst(burstSize_);
    }

    bool
    isOpen() override
    {
        return db_.is_open();
    }

    void
    open(bool createIfMissing) override
    {
        open(createIfMissing, appnum, nudb::make_uid(), nudb::make_salt());
    }

    void
    close() override
    {
        if (db_.is_open())
        {
            nudb::error_code ec;
            db_.close(ec);
            if (ec)
            {
                // Log to make sure the nature of the error gets to the user.
                JLOG(j_.fatal()) << "NuBD close() failed: " << ec.message();
                Throw<nudb::system_error>(ec);
            }

            if (deletePath_)
            {
                boost::filesystem::remove_all(name_, ec);
                if (ec)
                {
                    JLOG(j_.fatal())
                        << "Filesystem remove_all of " << name_ << " failed with: " << ec.message();
                }
            }
        }
    }

    Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pno) override
    {
        Status status = ok;
        pno->reset();
        nudb::error_code ec;

        db_.fetch(
            hash.data(),
            [&hash, pno, &status](void const* data, std::size_t size) {
                nudb::detail::buffer bf;
                auto const result = nodeobject_decompress(data, size, bf);
                DecodedBlob decoded(hash.data(), result.first, result.second);
                if (!decoded.wasOk())
                {
                    status = dataCorrupt;
                    return;
                }
                *pno = decoded.createObject();
                status = ok;
            },
            ec);

        if (ec == nudb::error::key_not_found)
            return notFound;
        if (ec)
            Throw<nudb::system_error>(ec);
        return status;
    }

    std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results(hashes.size());

        // Determine the number of threads to use for data compression from the number of available
        // cores and the size of the batch. We would like each thread to at least process 4 items,
        // except for the last thread that might process fewer items.
        auto const numThreads = std::min(
            std::max(static_cast<unsigned int>(hashes.size()) / 4u, 1u), numHardwareThreads);

        // If we need only one thread, just do it sequentially.
        if (numThreads == 1u)
        {
            for (size_t i = 0; i < hashes.size(); ++i)
            {
                std::shared_ptr<NodeObject> nObj;
                if (fetch(hashes[i], &nObj) == ok)
                {
                    results[i] = nObj;
                }
            }
            return {results, ok};
        }

        // Use a latch to synchronize task completion.
        std::latch taskCompletion(numThreads);

        // Submit fetch tasks to the thread pool.
        auto const itemsPerThread = (hashes.size() + numThreads - 1) / numThreads;
        for (auto t = 0u; t < numThreads; ++t)
        {
            auto const startIdx = t * itemsPerThread;
            XRPL_ASSERT(
                startIdx < hashes.size(),
                "xrpl::NuDBFactory::fetchBatch : startIdx < hashes.size()");
            if (startIdx >= hashes.size())
            {
                taskCompletion.count_down();
                continue;
            }
            auto const endIdx = std::min(startIdx + itemsPerThread, hashes.size());

            auto task = [this, &hashes, &results, &taskCompletion, startIdx, endIdx]() {
                // Fetch the items assigned to this task.
                for (size_t i = startIdx; i < endIdx; ++i)
                {
                    std::shared_ptr<NodeObject> nObj;
                    if (fetch(hashes[i], &nObj) == ok)
                    {
                        results[i] = nObj;
                    }
                }
                // Signal task completion.
                taskCompletion.count_down();
            };

            boost::asio::post(threadPool_, std::move(task));
        }

        // Wait for all fetch tasks to complete.
        taskCompletion.wait();

        return {results, ok};
    }

    void
    do_insert(std::shared_ptr<NodeObject> const& no)
    {
        EncodedBlob e(no);

        nudb::detail::buffer bf;
        auto const result = nodeobject_compress(e.getData(), e.getSize(), bf);

        nudb::error_code ec;
        db_.insert(e.getKey(), result.first, result.second, ec);
        if (ec && ec != nudb::error::key_exists)
            Throw<nudb::system_error>(ec);
    }

    void
    store(std::shared_ptr<NodeObject> const& no) override
    {
        BatchWriteReport report{};
        report.writeCount = 1;
        auto const start = std::chrono::steady_clock::now();

        ++pendingWrites_;
        try
        {
            do_insert(no);
        }
        catch (...)
        {
            --pendingWrites_;
            throw;
        }
        --pendingWrites_;

        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        scheduler_.onBatchWrite(report);
    }

    void
    storeBatch(Batch const& batch) override
    {
        BatchWriteReport report{};
        report.writeCount = batch.size();
        auto const start = std::chrono::steady_clock::now();

        pendingWrites_ += static_cast<int>(batch.size());

        // Determine the number of threads to use for data compression from the number of available
        // cores and the size of the batch. We would like each thread to at least process 4 items,
        // except for the last thread that might process fewer items.
        auto const numThreads = std::min(
            std::max(static_cast<unsigned int>(batch.size()) / 4u, 1u), numHardwareThreads);

        // If we need only one thread, just do it sequentially.
        if (numThreads == 1u)
        {
            for (auto const& e : batch)
            {
                try
                {
                    do_insert(e);
                }
                catch (...)
                {
                    pendingWrites_ -= static_cast<int>(batch.size());
                    throw;
                }
            }
            pendingWrites_ -= static_cast<int>(batch.size());

            report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            scheduler_.onBatchWrite(report);
            return;
        }

        // Helper struct that stores actual item data, not pointers, to avoid dangling references
        // after EncodedBlob and buffer go out of scope in the thread.
        struct CompressedData
        {
            std::vector<std::uint8_t> key;
            std::vector<std::uint8_t> data;
            std::exception_ptr eptr;
        };
        std::vector<CompressedData> compressed(batch.size());

        // Use a latch to synchronize task completion.
        std::latch taskCompletion(numThreads);

        // Submit compression tasks to the thread pool.
        auto const itemsPerThread = (batch.size() + numThreads - 1) / numThreads;
        for (auto t = 0u; t < numThreads; ++t)
        {
            auto const startIdx = t * itemsPerThread;
            XRPL_ASSERT(
                startIdx < batch.size(), "xrpl::NuDBFactory::storeBatch : startIdx < batch.size()");
            if (startIdx >= batch.size())
            {
                taskCompletion.count_down();
                continue;
            }
            auto const endIdx = std::min(startIdx + itemsPerThread, batch.size());

            auto task =
                [&batch, &compressed, &taskCompletion, startIdx, endIdx, keyBytes = keyBytes_]() {
                    // Compress the items assigned to this task.
                    for (size_t i = startIdx; i < endIdx; ++i)
                    {
                        auto& item = compressed[i];
                        try
                        {
                            EncodedBlob e(batch[i]);

                            // Copy the key data to avoid dangling pointer.
                            auto const* keyPtr = static_cast<std::uint8_t const*>(e.getKey());
                            item.key.assign(keyPtr, keyPtr + keyBytes);

                            // Compress and copy the data to avoid dangling pointer.
                            nudb::detail::buffer bf;
                            auto const comp = nodeobject_compress(e.getData(), e.getSize(), bf);
                            auto const* dataPtr = static_cast<std::uint8_t const*>(comp.first);
                            item.data.assign(dataPtr, dataPtr + comp.second);
                        }
                        catch (...)
                        {
                            // Store the exception so it can be rethrown in the sequential phase
                            // below.
                            item.eptr = std::current_exception();
                        }
                    }
                    // Signal task completion.
                    taskCompletion.count_down();
                };

            boost::asio::post(threadPool_, std::move(task));
        }

        // Wait for all compression tasks to complete.
        taskCompletion.wait();

        // Insert the compressed data sequentially, since NuDB is designed as an append-only data
        // store that only supports one writer.
        for (auto const& item : compressed)
        {
            if (item.eptr)
            {
                pendingWrites_ -= static_cast<int>(batch.size());
                std::rethrow_exception(item.eptr);
            }

            nudb::error_code ec;
            db_.insert(item.key.data(), item.data.data(), item.data.size(), ec);
            if (ec && ec != nudb::error::key_exists)
            {
                pendingWrites_ -= static_cast<int>(batch.size());
                Throw<nudb::system_error>(ec);
            }
        }

        pendingWrites_ -= static_cast<int>(batch.size());

        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        scheduler_.onBatchWrite(report);
    }

    void
    sync() override
    {
    }

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        auto const dp = db_.dat_path();
        auto const kp = db_.key_path();
        auto const lp = db_.log_path();

        nudb::error_code ec;
        db_.close(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        nudb::visit(
            dp,
            [&](void const* key,
                std::size_t key_bytes,
                void const* data,
                std::size_t size,
                nudb::error_code&) {
                nudb::detail::buffer bf;
                auto const result = nodeobject_decompress(data, size, bf);
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
        db_.open(dp, kp, lp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
    }

    int
    getWriteLoad() override
    {
        return pendingWrites_.load();
    }

    void
    setDeletePath() override
    {
        deletePath_ = true;
    }

    void
    verify() override
    {
        auto const dp = db_.dat_path();
        auto const kp = db_.key_path();
        auto const lp = db_.log_path();
        nudb::error_code ec;
        db_.close(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        nudb::verify_info vi;
        nudb::verify<nudb::xxhasher>(vi, dp, kp, 0, nudb::no_progress{}, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        db_.open(dp, kp, lp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
    }

    int
    fdRequired() const override
    {
        return 3;
    }

private:
    std::atomic<int> pendingWrites_{0};

    static std::size_t
    parseBlockSize(std::string const& name, Section const& keyValues, beast::Journal journal)
    {
        using namespace boost::filesystem;
        auto const folder = path(name);
        auto const kp = (folder / "nudb.key").string();

        std::size_t const defaultSize = nudb::block_size(kp);  // Default 4K from NuDB
        std::size_t blockSize = defaultSize;
        std::string blockSizeStr;

        if (!get_if_exists(keyValues, "nudb_block_size", blockSizeStr))
        {
            return blockSize;  // Early return with default
        }

        try
        {
            std::size_t const parsedBlockSize = beast::lexicalCastThrow<std::size_t>(blockSizeStr);

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

    std::string
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
    static NuDBFactory instance{manager};
}

}  // namespace NodeStore
}  // namespace xrpl
