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

#ifndef RIPPLE_NODESTORE_CODEC_H_INCLUDED
#define RIPPLE_NODESTORE_CODEC_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/protocol/HashPrefix.h>
#include <beast/nudb/common.h>
#include <beast/nudb/detail/field.h>
#include <beast/nudb/detail/varint.h>
#include <lz4/lib/lz4.h>
#include <snappy.h>
#include <cstddef>
#include <cstring>
#include <utility>

namespace ripple {
namespace NodeStore {

namespace detail {

template <class BufferFactory>
std::pair<void const*, std::size_t>
snappy_compress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
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
snappy_decompress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
{
    std::pair<void const*, std::size_t> result;
    if (! snappy::GetUncompressedLength(
            reinterpret_cast<char const*>(in),
                in_size, &result.second))
        Throw<beast::nudb::codec_error> (
            "snappy decompress");
    void* const out = bf(result.second);
    result.first = out;
    if (! snappy::RawUncompress(
        reinterpret_cast<char const*>(in), in_size,
            reinterpret_cast<char*>(out)))
        Throw<beast::nudb::codec_error> (
            "snappy decompress");
    return result;
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4_decompress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
{
    using beast::nudb::codec_error;
    using namespace beast::nudb::detail;
    std::pair<void const*, std::size_t> result;
    std::uint8_t const* p = reinterpret_cast<
        std::uint8_t const*>(in);
    auto const n = read_varint(
        p, in_size, result.second);
    if (n == 0)
        Throw<codec_error> (
            "lz4 decompress");
    void* const out = bf(result.second);
    result.first = out;
    if (LZ4_decompress_fast(
        reinterpret_cast<char const*>(in) + n,
            reinterpret_cast<char*>(out),
                result.second) + n != in_size)
        Throw<codec_error> (
            "lz4 decompress");
    return result;
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4_compress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
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
        Throw<codec_error> (
            "lz4 compress");
    result.second = n + out_size;
    return result;
}

//------------------------------------------------------------------------------

/*
    object types:

    0 = Uncompressed
    1 = lz4 compressed
    2 = inner node compressed
    3 = full inner node
*/

template <class BufferFactory>
std::pair<void const*, std::size_t>
nodeobject_decompress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
{
    using beast::nudb::codec_error;
    using namespace beast::nudb::detail;

    std::uint8_t const* p = reinterpret_cast<
        std::uint8_t const*>(in);
    std::size_t type;
    auto const vn = read_varint(
        p, in_size, type);
    if (vn == 0)
        Throw<codec_error> (
            "nodeobject decompress");
    p += vn;
    in_size -= vn;

    std::pair<void const*, std::size_t> result;
    switch(type)
    {
    case 0: // uncompressed
    {
        result.first = p;
        result.second = in_size;
        break;
    }
    case 1: // lz4
    {
        result = lz4_decompress(
            p, in_size, bf);
        break;
    }
    case 2: // inner node
    {
        auto const hs =
            field<std::uint16_t>::size; // Mask
        if (in_size < hs + 32)
            Throw<codec_error> (
                "nodeobject codec: short inner node");
        istream is(p, in_size);
        std::uint16_t mask;
        read<std::uint16_t>(is, mask);  // Mask
        in_size -= hs;
        result.second = 525;
        void* const out = bf(result.second);
        result.first = out;
        ostream os(out, result.second);
        write<std::uint32_t>(os, 0);
        write<std::uint32_t>(os, 0);
        write<std::uint8_t> (os, hotUNKNOWN);
        write<std::uint32_t>(os, HashPrefix::innerNode);
        if (mask == 0)
            Throw<codec_error> (
                "nodeobject codec: empty inner node");
        std::uint16_t bit = 0x8000;
        for (int i = 16; i--; bit >>= 1)
        {
            if (mask & bit)
            {
                if (in_size < 32)
                    Throw<codec_error> (
                        "nodeobject codec: short inner node");
                std::memcpy(os.data(32), is(32), 32);
                in_size -= 32;
            }
            else
            {
                std::memset(os.data(32), 0, 32);
            }
        }
        if (in_size > 0)
            Throw<codec_error> (
                "nodeobject codec: long inner node");
        break;
    }
    case 3: // full inner node
    {
        if (in_size != 16 * 32) // hashes
            Throw<codec_error> (
                "nodeobject codec: short full inner node");
        istream is(p, in_size);
        result.second = 525;
        void* const out = bf(result.second);
        result.first = out;
        ostream os(out, result.second);
        write<std::uint32_t>(os, 0);
        write<std::uint32_t>(os, 0);
        write<std::uint8_t> (os, hotUNKNOWN);
        write<std::uint32_t>(os, HashPrefix::innerNode);
        write(os, is(512), 512);
        break;
    }
    default:
        Throw<codec_error> (
            "nodeobject codec: bad type=" +
                std::to_string(type));
    };
    return result;
}

template <class = void>
void const*
zero32()
{
    static std::array<char, 32> v =
        []
        {
            std::array<char, 32> v;
            v.fill(0);
            return v;
        }();
    return v.data();
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
nodeobject_compress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
{
    using beast::nudb::codec_error;
    using namespace beast::nudb::detail;

    std::size_t type = 1;
    // Check for inner node
    if (in_size == 525)
    {
        istream is(in, in_size);
        std::uint32_t index;
        std::uint32_t unused;
        std::uint8_t  kind;
        std::uint32_t prefix;
        read<std::uint32_t>(is, index);
        read<std::uint32_t>(is, unused);
        read<std::uint8_t> (is, kind);
        read<std::uint32_t>(is, prefix);
        if (prefix == HashPrefix::innerNode)
        {
            std::size_t n = 0;
            std::uint16_t mask = 0;
            std::array<
                std::uint8_t, 512> vh;
            for (unsigned bit = 0x8000;
                bit; bit >>= 1)
            {
                void const* const h = is(32);
                if (std::memcmp(
                        h, zero32(), 32) == 0)
                    continue;
                std::memcpy(
                    vh.data() + 32 * n, h, 32);
                mask |= bit;
                ++n;
            }
            std::pair<void const*,
                std::size_t> result;
            if (n < 16)
            {
                // 2 = inner node compressed
                auto const type = 2U;
                auto const vs = size_varint(type);
                result.second =
                    vs +
                    field<std::uint16_t>::size +    // mask
                    n * 32;                         // hashes
                std::uint8_t* out = reinterpret_cast<
                    std::uint8_t*>(bf(result.second));
                result.first = out;
                ostream os(out, result.second);
                write<varint>(os, type);
                write<std::uint16_t>(os, mask);
                write(os, vh.data(), n * 32);
                return result;
            }
            // 3 = full inner node
            auto const type = 3U;
            auto const vs = size_varint(type);
            result.second =
                vs +
                n * 32;                         // hashes
            std::uint8_t* out = reinterpret_cast<
                std::uint8_t*>(bf(result.second));
            result.first = out;
            ostream os(out, result.second);
            write<varint>(os, type);
            write(os, vh.data(), n * 32);
            return result;
        }
    }

    std::array<std::uint8_t, varint_traits<
        std::size_t>::max> vi;
    auto const vn = write_varint(
        vi.data(), type);
    std::pair<void const*, std::size_t> result;
    switch(type)
    {
    case 0: // uncompressed
    {
        result.second = vn + in_size;
        std::uint8_t* p = reinterpret_cast<
            std::uint8_t*>(bf(result.second));
        result.first = p;
        std::memcpy(p, vi.data(), vn);
        std::memcpy(p + vn, in, in_size);
        break;
    }
    case 1: // lz4
    {
        std::uint8_t* p;
        auto const lzr = lz4_compress(
                in, in_size, [&p, &vn, &bf]
            (std::size_t n)
            {
                p = reinterpret_cast<
                    std::uint8_t*>(
                        bf(vn + n));
                return p + vn;
            });
        std::memcpy(p, vi.data(), vn);
        result.first = p;
        result.second = vn + lzr.second;
        break;
    }
    default:
        Throw<std::logic_error> (
            "nodeobject codec: unknown=" +
                std::to_string(type));
    };
    return result;
}

} // detail

// Modifies an inner node to erase the ledger
// sequence and type information so the codec
// verification can pass.
//
template <class = void>
void
filter_inner (void* in, std::size_t in_size)
{
    using beast::nudb::codec_error;
    using namespace beast::nudb::detail;

    // Check for inner node
    if (in_size == 525)
    {
        istream is(in, in_size);
        std::uint32_t index;
        std::uint32_t unused;
        std::uint8_t  kind;
        std::uint32_t prefix;
        read<std::uint32_t>(is, index);
        read<std::uint32_t>(is, unused);
        read<std::uint8_t> (is, kind);
        read<std::uint32_t>(is, prefix);
        if (prefix == HashPrefix::innerNode)
        {
            ostream os(in, 9);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t> (os, hotUNKNOWN);
        }
    }
}

//------------------------------------------------------------------------------

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
        return snappy_compress(in, in_size, bf);
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    decompress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        return snappy_decompress(in, in_size, bf);
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
        return lz4_compress(in, in_size, bf);
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    compress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        return lz4_compress(in, in_size, bf);
    }
};

class nodeobject_codec
{
public:
    template <class... Args>
    explicit
    nodeobject_codec(Args&&... args)
    {
    }

    char const*
    name() const
    {
        return "nodeobject";
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    decompress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        return detail::nodeobject_decompress(
            in, in_size, bf);
    }

    template <class BufferFactory>
    std::pair<void const*, std::size_t>
    compress (void const* in,
        std::size_t in_size, BufferFactory&& bf) const
    {
        return detail::nodeobject_compress(
            in, in_size, bf);
    }
};

} // NodeStore
} // ripple

#endif
