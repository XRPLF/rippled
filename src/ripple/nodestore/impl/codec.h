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
#include <nudb/detail/field.hpp>
#include <ripple/nodestore/impl/varint.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/protocol/HashPrefix.h>
#include <lz4/lib/lz4.h>
#include <snappy.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

namespace ripple {
namespace NodeStore {

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
        Throw<std::runtime_error> (
            "snappy decompress: GetUncompressedLength");
    void* const out = bf(result.second);
    result.first = out;
    if (! snappy::RawUncompress(
        reinterpret_cast<char const*>(in), in_size,
            reinterpret_cast<char*>(out)))
        Throw<std::runtime_error> (
            "snappy decompress: RawUncompress");
    return result;
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4_decompress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
{
    using std::runtime_error;
    using namespace nudb::detail;
    std::pair<void const*, std::size_t> result;
    std::uint8_t const* p = reinterpret_cast<
        std::uint8_t const*>(in);
    auto const n = read_varint(
        p, in_size, result.second);
    if (n == 0)
        Throw<std::runtime_error> (
            "lz4 decompress: n == 0");
    void* const out = bf(result.second);
    result.first = out;
    if (LZ4_decompress_fast(
        reinterpret_cast<char const*>(in) + n,
            reinterpret_cast<char*>(out),
                result.second) + n != in_size)
        Throw<std::runtime_error> (
            "lz4 decompress: LZ4_decompress_fast");
    return result;
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4_compress (void const* in,
    std::size_t in_size, BufferFactory&& bf)
{
    using std::runtime_error;
    using namespace nudb::detail;
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
        Throw<std::runtime_error> (
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
    using namespace nudb::detail;

    std::uint8_t const* p = reinterpret_cast<
        std::uint8_t const*>(in);
    std::size_t type;
    auto const vn = read_varint(
        p, in_size, type);
    if (vn == 0)
        Throw<std::runtime_error> (
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
    case 2: // compressed v1 inner node
    {
        auto const hs =
            field<std::uint16_t>::size; // Mask
        if (in_size < hs + 32)
            Throw<std::runtime_error> (
                "nodeobject codec v1: short inner node size: "
                + std::string("in_size = ") + std::to_string(in_size)
                + " hs = " + std::to_string(hs));
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
        write<std::uint32_t>(os,
            static_cast<std::uint32_t>(HashPrefix::innerNode));
        if (mask == 0)
            Throw<std::runtime_error> (
                "nodeobject codec v1: empty inner node");
        std::uint16_t bit = 0x8000;
        for (int i = 16; i--; bit >>= 1)
        {
            if (mask & bit)
            {
                if (in_size < 32)
                    Throw<std::runtime_error> (
                        "nodeobject codec v1: short inner node subsize: "
                        + std::string("in_size = ") + std::to_string(in_size)
                        + " i = " + std::to_string(i));
                std::memcpy(os.data(32), is(32), 32);
                in_size -= 32;
            }
            else
            {
                std::memset(os.data(32), 0, 32);
            }
        }
        if (in_size > 0)
            Throw<std::runtime_error> (
                "nodeobject codec v1: long inner node, in_size = "
                + std::to_string(in_size));
        break;
    }
    case 3: // full v1 inner node
    {
        if (in_size != 16 * 32) // hashes
            Throw<std::runtime_error> (
                "nodeobject codec v1: short full inner node, in_size = "
                + std::to_string(in_size));
        istream is(p, in_size);
        result.second = 525;
        void* const out = bf(result.second);
        result.first = out;
        ostream os(out, result.second);
        write<std::uint32_t>(os, 0);
        write<std::uint32_t>(os, 0);
        write<std::uint8_t> (os, hotUNKNOWN);
        write<std::uint32_t>(os,
            static_cast<std::uint32_t>(HashPrefix::innerNode));
        write(os, is(512), 512);
        break;
    }
    case 5: // compressed v2 inner node
    {
        auto const hs =
            field<std::uint16_t>::size; // Mask size
        if (in_size < hs + 65)
            Throw<std::runtime_error> (
                "nodeobject codec v2: short inner node size: "
                + std::string("size = ") + std::to_string(in_size)
                + " hs = " + std::to_string(hs));
        istream is(p, in_size);
        std::uint16_t mask;
        read<std::uint16_t>(is, mask);  // Mask
        in_size -= hs;
        std::uint8_t depth;
        read<std::uint8_t>(is, depth);
        in_size -= 1;
        result.second = 525 + 1 + (depth+1)/2;
        void* const out = bf(result.second);
        result.first = out;
        ostream os(out, result.second);
        write<std::uint32_t>(os, 0);
        write<std::uint32_t>(os, 0);
        write<std::uint8_t> (os, hotUNKNOWN);
        write<std::uint32_t>(os,
            static_cast<std::uint32_t>(HashPrefix::innerNodeV2));
        if (mask == 0)
            Throw<std::runtime_error> (
                "nodeobject codec v2: empty inner node");
        std::uint16_t bit = 0x8000;
        for (int i = 16; i--; bit >>= 1)
        {
            if (mask & bit)
            {
                if (in_size < 32)
                    Throw<std::runtime_error> (
                        "nodeobject codec v2: short inner node subsize: "
                        + std::string("in_size = ") + std::to_string(in_size)
                        + " i = " + std::to_string(i));
                std::memcpy(os.data(32), is(32), 32);
                in_size -= 32;
            }
            else
            {
                std::memset(os.data(32), 0, 32);
            }
        }
        write<std::uint8_t>(os, depth);
        if (in_size < (depth+1)/2)
            Throw<std::runtime_error> (
                "nodeobject codec v2: short inner node: "
                + std::string("size = ") + std::to_string(in_size)
                + " depth = " + std::to_string(depth));
        std::memcpy(os.data((depth+1)/2), is((depth+1)/2), (depth+1)/2);
        in_size -= (depth+1)/2;
        if (in_size > 0)
            Throw<std::runtime_error> (
                "nodeobject codec v2: long inner node, in_size = "
                + std::to_string(in_size));
        break;
    }
    case 6: // full v2 inner node
    {
        istream is(p, in_size);
        std::uint8_t depth;
        read<std::uint8_t>(is, depth);
        in_size -= 1;
        result.second = 525 + 1 + (depth+1)/2;
        if (in_size != 16 * 32 + (depth+1)/2) // hashes and common
            Throw<std::runtime_error> (
                "nodeobject codec v2: short full inner node: "
                + std::string("size = ") + std::to_string(in_size)
                + " depth = " + std::to_string(depth));
        void* const out = bf(result.second);
        result.first = out;
        ostream os(out, result.second);
        write<std::uint32_t>(os, 0);
        write<std::uint32_t>(os, 0);
        write<std::uint8_t> (os, hotUNKNOWN);
        write<std::uint32_t>(os,
            static_cast<std::uint32_t>(HashPrefix::innerNodeV2));
        write(os, is(512), 512);
        write<std::uint8_t>(os, depth);
        write(os, is((depth+1)/2), (depth+1)/2);
        break;
    }
    default:
        Throw<std::runtime_error> (
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
    using std::runtime_error;
    using namespace nudb::detail;

    std::size_t type = 1;
    // Check for inner node v1
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
                // 2 = v1 inner node compressed
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
            // 3 = full v1 inner node
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

    // Check for inner node v2
    if (526 <= in_size && in_size <= 556)
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
        if (prefix == HashPrefix::innerNodeV2)
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
            std::uint8_t depth;
            read<std::uint8_t>(is, depth);
            std::array<std::uint8_t, 32> common{};
            for (unsigned d = 0; d < (depth+1)/2; ++d)
                read<std::uint8_t>(is, common[d]);
            std::pair<void const*,
                std::size_t> result;
            if (n < 16)
            {
                // 5 = v2 inner node compressed
                auto const type = 5U;
                auto const vs = size_varint(type);
                result.second =
                    vs +
                    field<std::uint16_t>::size +    // mask
                    n * 32 +                        // hashes
                    1 +                             // depth
                    (depth+1)/2;                    // common prefix
                std::uint8_t* out = reinterpret_cast<
                    std::uint8_t*>(bf(result.second));
                result.first = out;
                ostream os(out, result.second);
                write<varint>(os, type);
                write<std::uint16_t>(os, mask);
                write<std::uint8_t>(os, depth);
                write(os, vh.data(), n * 32);
                for (unsigned d = 0; d < (depth+1)/2; ++d)
                    write<std::uint8_t>(os, common[d]);
                return result;
            }
            // 6 = full v2 inner node
            auto const type = 6U;
            auto const vs = size_varint(type);
            result.second =
                vs +
                n * 32 +                        // hashes
                1 +                             // depth
                (depth+1)/2;                    // common prefix
            std::uint8_t* out = reinterpret_cast<
                std::uint8_t*>(bf(result.second));
            result.first = out;
            ostream os(out, result.second);
            write<varint>(os, type);
            write<std::uint8_t>(os, depth);
            write(os, vh.data(), n * 32);
            for (unsigned d = 0; d < (depth+1)/2; ++d)
                write<std::uint8_t>(os, common[d]);
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
    // case 0 was uncompressed data; we always compress now.
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

// Modifies an inner node to erase the ledger
// sequence and type information so the codec
// verification can pass.
//
template <class = void>
void
filter_inner (void* in, std::size_t in_size)
{
    using namespace nudb::detail;

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

} // NodeStore
} // ripple

#endif
