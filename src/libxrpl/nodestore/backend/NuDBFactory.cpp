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
                boost::filesystem::remove_all(name, ec);
                if (ec)
                {
                    JLOG(j.fatal())
                        << "Filesystem remove_all of " << name << " failed with: " << ec.message();
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
        return 0;
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
    static std::size_t
    parseBlockSize(std::string const& name, Section const& keyValues, beast::Journal journal)
    {
        using namespace boost::filesystem;
        auto const folder = path(name);
        auto const kp = (folder / "nudb.key").string();

        std::size_t const defaultSize = nudb::block_size(kp);  // Default 4K from NuDB
        std::size_t const blockSize = defaultSize;
        std::string blockSizeStr;

        if (!getIfExists(keyValues, "nudb_block_size", blockSizeStr))
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

}  // namespace xrpl::NodeStore
