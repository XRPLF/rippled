//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/nodestore/Factory.h>
#include <xrpld/nodestore/Manager.h>
#include <xrpld/nodestore/detail/DecodedBlob.h>
#include <xrpld/nodestore/detail/EncodedBlob.h>
#include <xrpld/nodestore/detail/codec.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <boost/filesystem.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <nudb/nudb.hpp>

namespace ripple {
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
    nudb::store db_;
    std::atomic<bool> deletePath_;
    Scheduler& scheduler_;

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
        , deletePath_(false)
        , scheduler_(scheduler)
    {
        if (name_.empty())
            Throw<std::runtime_error>(
                "nodestore: Missing path in NuDB backend");
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
        , db_(context)
        , deletePath_(false)
        , scheduler_(scheduler)
    {
        if (name_.empty())
            Throw<std::runtime_error>(
                "nodestore: Missing path in NuDB backend");
    }

    ~NuDBBackend() override
    {
        try
        {
            // close can throw and we don't want the destructor to throw.
            close();
        }
        catch (nudb::system_error const&)
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

    void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt)
        override
    {
        using namespace boost::filesystem;
        if (db_.is_open())
        {
            UNREACHABLE(
                "ripple::NodeStore::NuDBBackend::open : database is already "
                "open");
            JLOG(j_.error()) << "database is already open";
            return;
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
                dp,
                kp,
                lp,
                appType,
                uid,
                salt,
                keyBytes_,
                nudb::block_size(kp),
                0.50,
                ec);
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
                    JLOG(j_.fatal()) << "Filesystem remove_all of " << name_
                                     << " failed with: " << ec.message();
                }
            }
        }
    }

    Status
    fetch(void const* key, std::shared_ptr<NodeObject>* pno) override
    {
        Status status;
        pno->reset();
        nudb::error_code ec;
        db_.fetch(
            key,
            [key, pno, &status](void const* data, std::size_t size) {
                nudb::detail::buffer bf;
                auto const result = nodeobject_decompress(data, size, bf);
                DecodedBlob decoded(key, result.first, result.second);
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
    fetchBatch(std::vector<uint256 const*> const& hashes) override
    {
        std::vector<std::shared_ptr<NodeObject>> results;
        results.reserve(hashes.size());
        for (auto const& h : hashes)
        {
            std::shared_ptr<NodeObject> nObj;
            Status status = fetch(h->begin(), &nObj);
            if (status != ok)
                results.push_back({});
            else
                results.push_back(nObj);
        }

        return {results, ok};
    }

    void
    do_insert(std::shared_ptr<NodeObject> const& no)
    {
        EncodedBlob e(no);
        nudb::error_code ec;
        nudb::detail::buffer bf;
        auto const result = nodeobject_compress(e.getData(), e.getSize(), bf);
        db_.insert(e.getKey(), result.first, result.second, ec);
        if (ec && ec != nudb::error::key_exists)
            Throw<nudb::system_error>(ec);
    }

    void
    store(std::shared_ptr<NodeObject> const& no) override
    {
        BatchWriteReport report;
        report.writeCount = 1;
        auto const start = std::chrono::steady_clock::now();
        do_insert(no);
        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        scheduler_.onBatchWrite(report);
    }

    void
    storeBatch(Batch const& batch) override
    {
        BatchWriteReport report;
        report.writeCount = batch.size();
        auto const start = std::chrono::steady_clock::now();
        for (auto const& e : batch)
            do_insert(e);
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
        // auto const appnum = db_.appnum();
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
        return 0;
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
};

//------------------------------------------------------------------------------

class NuDBFactory : public Factory
{
public:
    NuDBFactory()
    {
        Manager::instance().insert(*this);
    }

    ~NuDBFactory() override
    {
        Manager::instance().erase(*this);
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
        return std::make_unique<NuDBBackend>(
            keyBytes, keyValues, burstSize, scheduler, journal);
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

static NuDBFactory nuDBFactory;

}  // namespace NodeStore
}  // namespace ripple
