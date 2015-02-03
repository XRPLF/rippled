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

#include <BeastConfig.h>

#include <ripple/nodestore/Factory.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <beast/nudb.h>
#include <beast/nudb/detail/bucket.h> // remove asap
#include <beast/nudb/identity_codec.h>
#include <beast/nudb/visit.h>
#include <beast/hash/xxhasher.h>
#include <snappy.h>
#include <boost/filesystem.hpp>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>

namespace ripple {
namespace NodeStore {

class NuDBBackend
    : public Backend
{
public:
    enum
    {
        // This needs to be tuned for the
        // distribution of data sizes.
        arena_alloc_size = 16 * 1024 * 1024,

        //  Version 1
        //      No compression
        //
        typeOne = 1,

        // Version 2
        //      Snappy compression
        typeTwo = 2,



        currentType = typeTwo
    };

    using api = beast::nudb::api<
        beast::xxhasher, beast::nudb::identity_codec>;

    beast::Journal journal_;
    size_t const keyBytes_;
    std::string const name_;
    api::store db_;
    std::atomic <bool> deletePath_;
    Scheduler& scheduler_;

    NuDBBackend (int keyBytes, Parameters const& keyValues,
        Scheduler& scheduler, beast::Journal journal)
        : journal_ (journal)
        , keyBytes_ (keyBytes)
        , name_ (keyValues ["path"].toStdString ())
        , deletePath_(false)
        , scheduler_ (scheduler)
    {
        if (name_.empty())
            throw std::runtime_error (
                "nodestore: Missing path in NuDB backend");
        auto const folder = boost::filesystem::path (name_);
        boost::filesystem::create_directories (folder);
        auto const dp = (folder / "nudb.dat").string();
        auto const kp = (folder / "nudb.key").string ();
        auto const lp = (folder / "nudb.log").string ();
        using beast::nudb::make_salt;
        api::create (dp, kp, lp,
            currentType, make_salt(), keyBytes,
                beast::nudb::block_size(kp),
            0.50);
        try
        {
            if (! db_.open (dp, kp, lp,
                    arena_alloc_size))
                throw std::runtime_error(
                    "nodestore: open failed");
            if (db_.appnum() != currentType)
                throw std::runtime_error(
                    "nodestore: unknown appnum");
        }
        catch (std::exception const& e)
        {
            // log and terminate?
            std::cerr << e.what();
            std::terminate();
        }
    }

    ~NuDBBackend ()
    {
        close();
    }

    std::string
    getName()
    {
        return name_;
    }

    void
    close() override
    {
        if (db_.is_open())
        {
            db_.close();
            if (deletePath_)
            {
                boost::filesystem::remove_all (name_);
            }
        }
    }

    //--------------------------------------------------------------------------

    class Buffer
    {
    private:
        std::size_t size_ = 0;
        std::size_t capacity_ = 0;
        std::unique_ptr <std::uint8_t[]> buf_;

    public:
        Buffer() = default;
        Buffer (Buffer const&) = delete;
        Buffer& operator= (Buffer const&) = delete;

        explicit
        Buffer (std::size_t n)
        {
            resize (n);
        }

        std::size_t
        size() const
        {
            return size_;
        }

        std::size_t
        capacity() const
        {
            return capacity_;
        }

        void*
        get()
        {
            return buf_.get();
        }

        void
        resize (std::size_t n)
        {
            if (capacity_ < n)
            {
                capacity_ = beast::nudb::detail::ceil_pow2(n);
                buf_.reset (new std::uint8_t[capacity_]);
            }
            size_ = n;
        }

        // Meet the requirements of BufferFactory
        void*
        operator() (std::size_t n)
        {
            resize(n);
            return get();
        }
    };

    //--------------------------------------------------------------------------
    //
    //  Version 1 Database
    //
    //      Uncompressed
    //

    Status
    fetch1 (void const* key,
        std::shared_ptr <NodeObject>* pno)
    {
        Status status;
        pno->reset();
        if (! db_.fetch (key,
            [key, pno, &status](void const* data, std::size_t size)
            {
                DecodedBlob decoded (key, data, size);
                if (! decoded.wasOk ())
                {
                    status = dataCorrupt;
                    return;
                }
                *pno = decoded.createObject();
                status = ok;
            }))
        {
            return notFound;
        }
        return status;
    }

    void
    insert1 (void const* key, void const* data,
        std::size_t size)
    {
        db_.insert (key, data, size);
    }

    //--------------------------------------------------------------------------
    //
    //  Version 2 Database
    //
    //      Snappy compression
    //

    Status
    fetch2 (void const* key,
        std::shared_ptr <NodeObject>* pno)
    {
        Status status;
        pno->reset();
        if (! db_.fetch (key,
            [&](void const* data, std::size_t size)
            {
                std::size_t actual;
                if (! snappy::GetUncompressedLength(
                        (char const*)data, size, &actual))
                {
                    status = dataCorrupt;
                    return;
                }
                std::unique_ptr <char[]> buf (new char[actual]);
                snappy::RawUncompress (
                    (char const*)data, size, buf.get());
                DecodedBlob decoded (key, buf.get(), actual);
                if (! decoded.wasOk ())
                {
                    status = dataCorrupt;
                    return;
                }
                *pno = decoded.createObject();
                status = ok;
            }))
        {
            return notFound;
        }

        return status;
    }

    void
    insert2 (void const* key, void const* data,
        std::size_t size)
    {
        std::unique_ptr<char[]> buf (
            new char[snappy::MaxCompressedLength(size)]);
        std::size_t actual;
        snappy::RawCompress ((char const*)data, size,
            buf.get(), &actual);
        db_.insert (key, buf.get(), actual);
    }

    //--------------------------------------------------------------------------

    Status
    fetch (void const* key, NodeObject::Ptr* pno)
    {
        switch (db_.appnum())
        {
        case typeOne:   return fetch1 (key, pno);
        case typeTwo:   return fetch2 (key, pno);
        }
        throw std::runtime_error(
            "nodestore: unknown appnum");
        return notFound;
    }

    void
    do_insert (std::shared_ptr <NodeObject> const& no)
    {
        EncodedBlob e;
        e.prepare (no);
        switch (db_.appnum())
        {
        case typeOne:       return insert1 (e.getKey(), e.getData(), e.getSize());
        case typeTwo:       return insert2 (e.getKey(), e.getData(), e.getSize());
        }
        throw std::runtime_error(
            "nodestore: unknown appnum");
    }

    void
    store (std::shared_ptr <NodeObject> const& no) override
    {
        BatchWriteReport report;
        report.writeCount = 1;
        auto const start =
            std::chrono::steady_clock::now();
        do_insert (no);
        report.elapsed = std::chrono::duration_cast <
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
        scheduler_.onBatchWrite (report);
    }

    void
    storeBatch (Batch const& batch) override
    {
        BatchWriteReport report;
        EncodedBlob encoded;
        report.writeCount = batch.size();
        auto const start =
            std::chrono::steady_clock::now();
        for (auto const& e : batch)
            do_insert (e);
        report.elapsed = std::chrono::duration_cast <
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
        scheduler_.onBatchWrite (report);
    }

    void
    for_each (std::function <void(NodeObject::Ptr)> f)
    {
        auto const dp = db_.dat_path();
        auto const kp = db_.key_path();
        auto const lp = db_.log_path();
        auto const appnum = db_.appnum();
        db_.close();
        api::visit (dp,
            [&](
                void const* key, std::size_t key_bytes,
                void const* data, std::size_t size)
            {
                switch (appnum)
                {
                case typeOne:
                {
                    DecodedBlob decoded (key, data, size);
                    if (! decoded.wasOk ())
                        return false;
                    f (decoded.createObject());
                    break;
                }
                case typeTwo:
                {
                    std::size_t actual;
                    if (! snappy::GetUncompressedLength(
                            (char const*)data, size, &actual))
                        return false;
                    std::unique_ptr <char[]> buf (new char[actual]);
                    if (! snappy::RawUncompress ((char const*)data,
                            size, buf.get()))
                        return false;
                    DecodedBlob decoded (key, buf.get(), actual);
                    if (! decoded.wasOk ())
                        return false;
                    f (decoded.createObject());
                    break;
                }
                }
                return true;
            });
        db_.open (dp, kp, lp,
            arena_alloc_size);
    }

    int
    getWriteLoad ()
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
        db_.close();
        api::verify (dp, kp);
        db_.open (dp, kp, lp,
            arena_alloc_size);
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

    ~NuDBFactory()
    {
        Manager::instance().erase(*this);
    }

    std::string
    getName() const
    {
        return "NuDB";
    }

    std::unique_ptr <Backend>
    createInstance (
        size_t keyBytes,
        Parameters const& keyValues,
        Scheduler& scheduler,
        beast::Journal journal)
    {
        return std::make_unique <NuDBBackend> (
            keyBytes, keyValues, scheduler, journal);
    }
};

static NuDBFactory nuDBFactory;

}
}
