#pragma once

// Disable lz4 deprecation warning due to incompatibility with clang attributes
#define LZ4_DISABLE_DEPRECATE_WARNINGS

#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/detail/varint.h>
#include <xrpl/protocol/HashPrefix.h>

#include <nudb/detail/field.hpp>

#include <lz4.h>

#include <cstddef>
#include <cstring>
#include <string>

namespace xrpl::NodeStore {

template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4Decompress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    if (static_cast<int>(inSize) < 0)
        Throw<std::runtime_error>("lz4_decompress: integer overflow (input)");

    std::size_t outSize = 0;

    auto const n = readVarint(reinterpret_cast<std::uint8_t const*>(in), inSize, outSize);

    if (n == 0 || n >= inSize)
        Throw<std::runtime_error>("lz4_decompress: invalid blob");

    if (static_cast<int>(outSize) <= 0)
        Throw<std::runtime_error>("lz4_decompress: integer overflow (output)");

    void* const out = bf(outSize);

    if (LZ4_decompress_safe(
            reinterpret_cast<char const*>(in) + n,
            reinterpret_cast<char*>(out),
            static_cast<int>(inSize - n),
            static_cast<int>(outSize)) != static_cast<int>(outSize))
        Throw<std::runtime_error>("lz4_decompress: LZ4_decompress_safe");

    return {out, outSize};
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
lz4Compress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    using std::runtime_error;
    using namespace nudb::detail;
    std::pair<void const*, std::size_t> result;
    std::array<std::uint8_t, varint_traits<std::size_t>::kMax> vi{};
    auto const n = writeVarint(vi.data(), inSize);
    auto const outMax = LZ4_compressBound(inSize);
    std::uint8_t* out = reinterpret_cast<std::uint8_t*>(bf(n + outMax));
    result.first = out;
    std::memcpy(out, vi.data(), n);
    auto const outSize = LZ4_compress_default(
        reinterpret_cast<char const*>(in), reinterpret_cast<char*>(out + n), inSize, outMax);
    if (outSize == 0)
        Throw<std::runtime_error>("lz4 compress");
    result.second = n + outSize;
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
nodeobjectDecompress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    using namespace nudb::detail;

    std::uint8_t const* p = reinterpret_cast<std::uint8_t const*>(in);
    std::size_t type = 0;
    auto const vn = readVarint(p, inSize, type);
    if (vn == 0)
        Throw<std::runtime_error>("nodeobject decompress");
    p += vn;
    inSize -= vn;

    std::pair<void const*, std::size_t> result;
    switch (type)
    {
        case 0:  // uncompressed
        {
            result.first = p;
            result.second = inSize;
            break;
        }
        case 1:  // lz4
        {
            result = lz4Decompress(p, inSize, bf);
            break;
        }
        case 2:  // compressed v1 inner node
        {
            auto const hs = field<std::uint16_t>::size;  // Mask
            if (inSize < hs + 32)
            {
                Throw<std::runtime_error>(
                    "nodeobject codec v1: short inner node size: " + std::string("in_size = ") +
                    std::to_string(inSize) + " hs = " + std::to_string(hs));
            }
            istream is(p, inSize);
            std::uint16_t mask = 0;
            read<std::uint16_t>(is, mask);  // Mask
            inSize -= hs;
            result.second = 525;
            void* const out = bf(result.second);
            result.first = out;
            ostream os(out, result.second);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
            write<std::uint32_t>(os, static_cast<std::uint32_t>(HashPrefix::InnerNode));
            if (mask == 0)
                Throw<std::runtime_error>("nodeobject codec v1: empty inner node");
            std::uint16_t bit = 0x8000;
            for (int i = 16; i--; bit >>= 1)
            {
                if (mask & bit)
                {
                    if (inSize < 32)
                    {
                        Throw<std::runtime_error>(
                            "nodeobject codec v1: short inner node subsize: " +
                            std::string("in_size = ") + std::to_string(inSize) +
                            " i = " + std::to_string(i));
                    }
                    std::memcpy(os.data(32), is(32), 32);
                    inSize -= 32;
                }
                else
                {
                    std::memset(os.data(32), 0, 32);
                }
            }
            if (inSize > 0)
            {
                Throw<std::runtime_error>(
                    "nodeobject codec v1: long inner node, in_size = " + std::to_string(inSize));
            }
            break;
        }
        case 3:  // full v1 inner node
        {
            if (inSize != 16 * 32)
            {  // hashes
                Throw<std::runtime_error>(
                    "nodeobject codec v1: short full inner node, in_size = " +
                    std::to_string(inSize));
            }
            istream is(p, inSize);
            result.second = 525;
            void* const out = bf(result.second);
            result.first = out;
            ostream os(out, result.second);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
            write<std::uint32_t>(os, static_cast<std::uint32_t>(HashPrefix::InnerNode));
            write(os, is(512), 512);
            break;
        }
        default:
            Throw<std::runtime_error>("nodeobject codec: bad type=" + std::to_string(type));
    };
    return result;
}

template <class = void>
void const*
zero32()
{
    static std::array<char, 32> kV{};
    return kV.data();
}

template <class BufferFactory>
std::pair<void const*, std::size_t>
nodeobjectCompress(void const* in, std::size_t inSize, BufferFactory&& bf)
{
    using std::runtime_error;
    using namespace nudb::detail;

    // Check for inner node v1
    if (inSize == 525)
    {
        istream is(in, inSize);
        std::uint32_t index = 0;
        std::uint32_t unused = 0;
        std::uint8_t kind = 0;
        std::uint32_t prefix = 0;
        read<std::uint32_t>(is, index);
        read<std::uint32_t>(is, unused);
        read<std::uint8_t>(is, kind);
        read<std::uint32_t>(is, prefix);
        if (safeCast<HashPrefix>(prefix) == HashPrefix::InnerNode)
        {
            std::size_t n = 0;
            std::uint16_t mask = 0;
            std::array<std::uint8_t, 512> vh{};
            for (unsigned bit = 0x8000; bit; bit >>= 1)
            {
                void const* const h = is(32);
                if (std::memcmp(h, zero32(), 32) == 0)
                    continue;
                std::memcpy(vh.data() + (32 * n), h, 32);
                mask |= bit;
                ++n;
            }
            std::pair<void const*, std::size_t> result;
            if (n < 16)
            {
                // 2 = v1 inner node compressed
                auto const type = 2U;
                auto const vs = sizeVarint(type);
                result.second = vs + field<std::uint16_t>::size +  // mask
                    (n * 32);                                      // hashes
                std::uint8_t* out = reinterpret_cast<std::uint8_t*>(bf(result.second));
                result.first = out;
                ostream os(out, result.second);
                write<varint>(os, type);
                write<std::uint16_t>(os, mask);
                write(os, vh.data(), n * 32);
                return result;
            }
            // 3 = full v1 inner node
            auto const type = 3U;
            auto const vs = sizeVarint(type);
            result.second = vs + (n * 32);  // hashes
            std::uint8_t* out = reinterpret_cast<std::uint8_t*>(bf(result.second));
            result.first = out;
            ostream os(out, result.second);
            write<varint>(os, type);
            write(os, vh.data(), n * 32);
            return result;
        }
    }

    std::array<std::uint8_t, varint_traits<std::size_t>::kMax> vi{};

    static constexpr std::size_t kCodecType = 1;
    auto const vn = writeVarint(vi.data(), kCodecType);
    std::pair<void const*, std::size_t> result;
    switch (kCodecType)
    {
        // case 0 was uncompressed data; we always compress now.
        case 1:  // lz4
        {
            std::uint8_t* p = nullptr;
            auto const lzr = NodeStore::lz4Compress(in, inSize, [&p, &vn, &bf](std::size_t n) {
                p = reinterpret_cast<std::uint8_t*>(bf(vn + n));
                return p + vn;
            });
            std::memcpy(p, vi.data(), vn);
            result.first = p;
            result.second = vn + lzr.second;
            break;
        }
        default:
            Throw<std::logic_error>("nodeobject codec: unknown=" + std::to_string(kCodecType));
    };
    return result;
}

// Modifies an inner node to erase the ledger
// sequence and type information so the codec
// verification can pass.
//
template <class = void>
void
filterInner(void* in, std::size_t inSize)
{
    using namespace nudb::detail;

    // Check for inner node
    if (inSize == 525)
    {
        istream is(in, inSize);
        std::uint32_t index = 0;
        std::uint32_t unused = 0;
        std::uint8_t kind = 0;
        std::uint32_t prefix = 0;
        read<std::uint32_t>(is, index);
        read<std::uint32_t>(is, unused);
        read<std::uint8_t>(is, kind);
        read<std::uint32_t>(is, prefix);
        if (safeCast<HashPrefix>(prefix) == HashPrefix::InnerNode)
        {
            ostream os(in, 9);
            write<std::uint32_t>(os, 0);
            write<std::uint32_t>(os, 0);
            write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
        }
    }
}

}  // namespace xrpl::NodeStore
