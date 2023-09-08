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

#ifndef RIPPLE_PROTOCOL_DESERIALIZER_H_INCLUDED
#define RIPPLE_PROTOCOL_DESERIALIZER_H_INCLUDED

#include <ripple/basics/Slice.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/serdes.h>
#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>

namespace ripple {

/** A class to work with serialized buffers.

    This class takes a non-owning pointer to a serialized buffer and
    provides interfaces to extract components from the buffer.

    The buffer must remain valid for the lifetime of the object and
    of any variable-length blobs (in a Slice) extracted from this
    object.
 */
class SerialIter
{
private:
    std::uint8_t const* p_;
    std::size_t remain_;
    std::size_t used_ = 0;

public:
    /** @{ */
    SerialIter(void const* data, std::size_t size) noexcept
        : p_(reinterpret_cast<std::uint8_t const*>(data)), remain_(size)
    {
    }

    SerialIter(Slice const& slice) : SerialIter(slice.data(), slice.size())
    {
    }

    // Infer the size of the data based on the size of the passed array.
    template <
        class T,
        std::size_t N,
        class = std::enable_if_t<
            std::is_same_v<T, char> || std::is_same_v<T, unsigned char>>>
    explicit SerialIter(T const (&data)[N]) : SerialIter(&data[0], N)
    {
        static_assert(
            N != 0,
            "SerialIter does not support construction from zero-size arrays.");
    }
    /** @} */

    [[nodiscard]] bool
    empty() const noexcept
    {
        return remain_ == 0;
    }

    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return remain_;
    }

    /** Functions to extract 8, 16, 32 and 64-bit unsigned integers.

        @return The unsigned integer on success.
        @throws Exception if the buffer does not contain enough bytes.
     */
    /** @{ */
    [[nodiscard]] unsigned char
    get8()
    {
        if (remain_ < 1)
            Throw<std::runtime_error>("SerialIter: invalid get8");
        auto ret = *p_++;
        ++used_;
        --remain_;
        return ret;
    }

    [[nodiscard]] std::uint16_t
    get16()
    {
        constexpr std::size_t const bytes = sizeof(std::uint16_t);

        if (remain_ < bytes)
            Throw<std::runtime_error>("SerialIter: invalid get16");

        auto const p = p_;

        p_ += bytes;
        used_ += bytes;
        remain_ -= bytes;

        return boost::endian::
            endian_load<std::uint16_t, bytes, boost::endian::order::big>(p);
    }

    [[nodiscard]] std::uint32_t
    get32()
    {
        constexpr std::size_t const bytes = sizeof(std::uint32_t);

        if (remain_ < bytes)
            Throw<std::runtime_error>("SerialIter: invalid get32");

        auto const p = p_;

        p_ += bytes;
        used_ += bytes;
        remain_ -= bytes;

        return boost::endian::
            endian_load<std::uint32_t, bytes, boost::endian::order::big>(p);
    }

    [[nodiscard]] std::uint64_t
    get64()
    {
        constexpr std::size_t const bytes = sizeof(std::uint64_t);

        if (remain_ < bytes)
            Throw<std::runtime_error>("SerialIter: invalid get64");

        auto const p = p_;

        p_ += bytes;
        used_ += bytes;
        remain_ -= bytes;

        return boost::endian::
            endian_load<std::uint64_t, bytes, boost::endian::order::big>(p);
    }
    /** @} */

    /** Functions to extract extra-long unsigned integers (base_uint).

        @return The unsigned integer on success.
        @throws Exception if the buffer does not contain enough bytes.
     */
    /** @{ */
    template <std::size_t Bits, class Tag = void>
    [[nodiscard]] base_uint<Bits, Tag>
    getBitString()
    {
        auto const n = Bits / 8;

        if (remain_ < n)
            Throw<std::runtime_error>("SerialIter: invalid getBitString");

        auto const x = p_;

        p_ += n;
        used_ += n;
        remain_ -= n;

        return base_uint<Bits, Tag>::fromVoid(x);
    }

    [[nodiscard]] uint128
    get128()
    {
        return getBitString<128>();
    }

    [[nodiscard]] uint160
    get160()
    {
        return getBitString<160>();
    }

    [[nodiscard]] uint256
    get256()
    {
        return getBitString<256>();
    }
    /** @} */

    /** Extracts a variable-length blob. */
    [[nodiscard]] Slice
    getVL()
    {
        auto const vll = [this]() {
            auto const b1 = static_cast<std::size_t>(get8());

            if (b1 < serdes::offset2)
                return b1;

            auto const b2 = static_cast<std::size_t>(get8());

            if (b1 < serdes::offset3)
                return serdes::maxSize1 + (b1 - serdes::offset2) * 256 + b2;

            auto const b3 = static_cast<std::size_t>(get8());

            if (b1 != std::numeric_limits<std::uint8_t>::max())
                return (serdes::maxSize2 + (b1 - serdes::offset3) * 65536) +
                    (b2 * 256) + b3;

            Throw<std::overflow_error>("Invalid VL encoded length");
        }();

        assert(vll < serdes::maxSize3);

        if (vll <= remain_)
        {
            Slice s(p_, vll);

            p_ += vll;
            used_ += vll;
            remain_ -= vll;

            return s;
        }

        Throw<std::runtime_error>("SerialIter: invalid getVL");
    }

    /** Extracts a Field ID. */
    [[nodiscard]] std::pair<std::uint8_t, std::uint8_t>
    getFieldID()
    {
        auto ret = [x = get8()]() {
            return std::make_pair(x >> 4, x & 0x0F);
        }();

        if (ret.first == 0)
        {  // The field type is uncommon
            ret.first = static_cast<std::uint8_t>(get8());

            if (ret.first < 16)
                Throw<std::runtime_error>(
                    "Uncommon field type out of range " +
                    std::to_string(ret.first));
        }

        if (ret.second == 0)
        {  // The field name is uncommon
            ret.second = static_cast<std::uint8_t>(get8());

            if (ret.second < 16)
                Throw<std::runtime_error>(
                    "Uncommon field name out of range " +
                    std::to_string(ret.second));
        }

        return ret;
    }

    /** Returns the remaining deserialized buffer, if any, as a Slice */
    [[nodiscard]] Slice
    slice() const
    {
        return {p_, remain_};
    }

    /** Consumes up to the given number of bytes from the beginning. */
    void
    skip(std::size_t size)
    {
        if (remain_ < size)
            Throw<std::runtime_error>("SerialIter: invalid skip");
        p_ += size;
        used_ += size;
        remain_ -= size;
    }
};

}  // namespace ripple

#endif
