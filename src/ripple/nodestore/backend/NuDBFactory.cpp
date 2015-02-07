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
#include <beast/nudb/detail/varint.h>
#include <beast/nudb/identity_codec.h>
#include <beast/nudb/visit.h>
#include <beast/hash/xxhasher.h>
#include <lz4/lib/lz4.h>
#include <lz4/lib/lz4hc.h>
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

class snappy_codec
{
public:
    template <class... Args>
    explicit
    snappy_codec(Args&&... args)
    {
    }

    char const*
    name() const
    {
        return "snappy";
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    compress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        std::pair<void const*, std::size_t> result;
        auto const out_max =
            snappy::MaxCompressedLength(in_size);
        void* const out = bf(out_max);
        result.first = out;
        snappy::RawCompress(
            reinterpret_cast<char const*>(in),
                in_size, reinterpret_cast<char*>(out),
                    &result.second);
        return result;
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    decompress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        std::pair<void const*, std::size_t> result;
        if (! snappy::GetUncompressedLength(
                reinterpret_cast<char const*>(in),
                    in_size, &result.second))
            throw beast::nudb::codec_error(
                "snappy decompress");
        void* const out = bf(result.second);
        result.first = out;
        if (! snappy::RawUncompress(
            reinterpret_cast<char const*>(in), in_size,
                reinterpret_cast<char*>(out)))
            throw beast::nudb::codec_error(
                "snappy decompress");
        return result;
    }
};

class lz4_codec
{
public:
    template <class... Args>
    explicit
    lz4_codec(Args&&... args)
    {
    }

    char const*
    name() const
    {
        return "lz4";
    }
     
    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    decompress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        using beast::nudb::codec_error;
        using namespace beast::nudb::detail;
        std::pair<void const*, std::size_t> result;
        std::uint8_t const* p = reinterpret_cast<
            std::uint8_t const*>(in);
        auto const n = read_varint(
            p, in_size, result.second);
        if (n == 0)
            throw codec_error(
                "lz4 decompress");
        void* const out = bf(result.second);
        result.first = out;
        if (LZ4_decompress_fast(
            reinterpret_cast<char const*>(in) + n,
                reinterpret_cast<char*>(out),
                    result.second) + n != in_size)
            throw codec_error(
                "lz4 decompress");
        return result;
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    compress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        using beast::nudb::codec_error;
        using namespace beast::nudb::detail;
        std::pair<void const*, std::size_t> result;
        std::array<std::uint8_t, varint_traits<
            std::size_t>::max> vi;
        auto const n = write_varint(
            vi.data(), in_size);
        auto const out_max =
            LZ4_compressBound(in_size);
        std::uint8_t* out = reinterpret_cast<
            std::uint8_t*>(bf(n + out_max));
        result.first = out;
        std::memcpy(out, vi.data(), n);
        auto const out_size = LZ4_compress(
            reinterpret_cast<char const*>(in),
                reinterpret_cast<char*>(out + n),
                    in_size);
        if (out_size == 0)
            throw codec_error(
                "lz4 compress");
        result.second = n + out_size;
        return result;
    }
};

class lz4hc_codec
{
public:
    template <class... Args>
    explicit
    lz4hc_codec(Args&&... args)
    {
    }

    char const*
    name() const
    {
        return "lz4hc";
    }
     
    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    decompress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        using beast::nudb::codec_error;
        using namespace beast::nudb::detail;
        std::pair<void const*, std::size_t> result;
        std::uint8_t const* p = reinterpret_cast<
            std::uint8_t const*>(in);
        auto const n = read_varint(
            p, in_size, result.second);
        if (n == 0)
            throw codec_error(
                "lz4hc decompress");
        void* const out = bf(result.second);
        result.first = out;
        if (LZ4_decompress_fast(
            reinterpret_cast<char const*>(in) + n,
                reinterpret_cast<char*>(out),
                    result.second) + n != in_size)
            throw codec_error(
                "lz4hc decompress");
        return result;
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    compress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        using beast::nudb::codec_error;
        using namespace beast::nudb::detail;
        std::pair<void const*, std::size_t> result;
        std::array<std::uint8_t, varint_traits<
            std::size_t>::max> vi;
        auto const n = write_varint(
            vi.data(), in_size);
        auto const out_max =
            LZ4_compressBound(in_size);
        std::uint8_t* out = reinterpret_cast<
            std::uint8_t*>(bf(n + out_max));
        result.first = out;
        std::memcpy(out, vi.data(), n);
        auto const out_size = LZ4_compressHC(
            reinterpret_cast<char const*>(in),
                reinterpret_cast<char*>(out + n),
                    in_size);
        if (out_size == 0)
            throw codec_error(
                "lz4hc compress");
        result.second = n + out_size;
        return result;
    }
};

//------------------------------------------------------------------------------

class NuDBBackend
    : public Backend
{
public:
    enum
    {
        // This needs to be tuned for the
        // distribution of data sizes.
        arena_alloc_size = 16 * 1024 * 1024,

        currentType = 1
    };

    using api = beast::nudb::api<
        beast::xxhasher, lz4_codec>;

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

    Status
    fetch (void const* key, NodeObject::Ptr* pno)
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
    do_insert (std::shared_ptr <NodeObject> const& no)
    {
        EncodedBlob e;
        e.prepare (no);
        db_.insert (e.getKey(),
            e.getData(), e.getSize());
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
        //auto const appnum = db_.appnum();
        db_.close();
        api::visit (dp,
            [&](
                void const* key, std::size_t key_bytes,
                void const* data, std::size_t size)
            {
                DecodedBlob decoded (key, data, size);
                if (! decoded.wasOk ())
                    return false;
                f (decoded.createObject());
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
