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

#ifndef RIPPLE_PROTOCOL_SERIALIZER_H_INCLUDED
#define RIPPLE_PROTOCOL_SERIALIZER_H_INCLUDED

#include <ripple/basics/Blob.h>
#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/basics/strHex.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/serdes.h>
#include <boost/container/small_vector.hpp>
#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>

namespace ripple {

/** The Serializer class is used to serialize data. */
class SerializerBase
{
protected:
    /** Constructor

        By making the constructor protected we enforce the requirement that
        this class not be constructible as a standalone object, but only as
        as a base class.

        @param buffer The buffer into which data will be serialized.

     */
    SerializerBase() = default;

    virtual void
    append(std::uint8_t c) = 0;

    virtual void
    append(std::uint8_t const* ptr, std::size_t len) = 0;

public:
    virtual ~SerializerBase() = default;

    // The class is not meant to be copy- or move-constructible.
    SerializerBase(SerializerBase const& s) = delete;
    SerializerBase&
    operator=(SerializerBase const& s) = delete;

    SerializerBase(SerializerBase&& s) = delete;
    SerializerBase&
    operator=(SerializerBase&& s) = delete;

    /** Empties the underlying buffer, in effect resetting the serializer. */
    virtual void
    clear() = 0;

    /** Returns true if the serializer buffer contains no data. */
    [[nodiscard]] virtual bool
    empty() const = 0;

    /** Serialize a blob as a sequence of raw bytes. */
    /** @{ */
    void
    addRaw(const void* ptr, std::size_t len)
    {
        assert(ptr != nullptr);

        if (auto data = reinterpret_cast<std::uint8_t const*>(ptr); len != 0)
            append(data, len);
    }

    void
    addRaw(Blob const& vector)
    {
        addRaw(vector.data(), vector.size());
    }

    void
    addRaw(Slice slice)
    {
        addRaw(slice.data(), slice.size());
    }
    /** @} */

    /** Serialize a blob as a length-prefixed sequence of raw bytes. */
    /** @{ */
    void
    addVL(void const* ptr, std::size_t len)
    {
        using namespace detail;

        if (len >= serdes::maxSize3)
            Throw<std::overflow_error>(
                "Attempt to encode overlong VL field: " + std::to_string(len));

        // First we need to encode the length of this blob as a variable-length
        // integer without a length prefix, making the packing a little weird.
        if (len < serdes::maxSize1)
            append(static_cast<std::uint8_t>(len));
        else if (len < serdes::maxSize2)
        {
            auto const rem = len - serdes::maxSize1;
            append(serdes::offset2 + static_cast<std::uint8_t>(rem >> 8));
            append(static_cast<std::uint8_t>(rem & 0xff));
        }
        else
        {
            auto const rem = len - serdes::maxSize2;
            append(serdes::offset3 + static_cast<std::uint8_t>(rem >> 16));
            append(static_cast<std::uint8_t>((rem >> 8) & 0xff));
            append(static_cast<std::uint8_t>(rem & 0xff));
        }

        // Now, finally, we can encode the data:
        if (len)
            addRaw(ptr, len);
    }

    void
    addVL(Blob const& vector)
    {
        addVL(vector.data(), vector.size());
    }

    void
    addVL(Slice const& slice)
    {
        addVL(slice.data(), slice.size());
    }
    /** @} */

    inline void
    add8(unsigned char byte)
    {
        append(byte);
    }

    inline void
    add16(std::uint16_t i)
    {
        i = boost::endian::native_to_big(i);
        addRaw(reinterpret_cast<std::uint8_t const*>(&i), sizeof(i));
    }

    inline void
    add32(std::uint32_t i)
    {
        i = boost::endian::native_to_big(i);
        addRaw(reinterpret_cast<std::uint8_t const*>(&i), sizeof(i));
    }

    inline void
    add32(HashPrefix p)
    {
        // This should never trigger; the size & type of HashPrefix are
        // integral parts of the protocol and unlikely to ever change.
        static_assert(
            std::is_same_v<std::uint32_t, std::underlying_type_t<decltype(p)>>);

        add32(safe_cast<std::uint32_t>(p));
    }

    inline void
    add64(std::uint64_t i)
    {
        i = boost::endian::native_to_big(i);
        addRaw(reinterpret_cast<std::uint8_t const*>(&i), sizeof(i));
    }

    template <std::size_t Bits, class Tag>
    void
    addBitString(base_uint<Bits, Tag> const& v)
    {
        return addRaw(v.data(), v.size());
    }

    void
    addFieldID(int type, int name)
    {
        assert((type > 0) && (type < 256) && (name > 0) && (name < 256));

        if (type < 16 && name < 16)
        {
            // common type, common name
            append(static_cast<std::uint8_t>((type << 4) | name));
        }
        else if (type < 16)
        {
            // common type, uncommon name
            append(static_cast<std::uint8_t>(type << 4));
            append(static_cast<std::uint8_t>(name));
        }
        else if (name < 16)
        {
            // uncommon type, common name
            append(static_cast<std::uint8_t>(name));
            append(static_cast<std::uint8_t>(type));
        }
        else
        {
            // uncommon type, uncommon name
            append(static_cast<std::uint8_t>(0));
            append(static_cast<std::uint8_t>(type));
            append(static_cast<std::uint8_t>(name));
        }
    }

    void
    addFieldID(SerializedTypeID type, int name)
    {
        return addFieldID(safe_cast<int>(type), name);
    }
};

/** A serializer class with a large built-in buffer.

    @note The size of this buffer may seen extravagant but it helps to
          reduce the need to perform memory allocations, and the extra
          space is "free" in terms of runtime costs because this class
          is intended to be placed on the stack, and not the heap.
 */
class Serializer : public SerializerBase
{
private:
    boost::container::small_vector<std::uint8_t, 16384> buffer_;

public:
    Serializer() = default;
    Serializer(Serializer const& s) : SerializerBase(), buffer_(s.buffer_)
    {
    }

    Serializer(Serializer&& other)
        : SerializerBase()
        , buffer_(other.buffer_)  // notice: invokes *copy* constructor
    {
    }

    [[nodiscard]] Slice
    slice() const noexcept
    {
        return Slice(buffer_.data(), buffer_.size());
    }

    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return buffer_.size();
    }

    [[nodiscard]] unsigned char const*
    data() const noexcept
    {
        return buffer_.data();
    }

    void
    clear() override
    {
        buffer_.clear();
    }

    [[nodiscard]] bool
    empty() const override
    {
        return buffer_.empty();
    }

    void
    append(std::uint8_t c) override
    {
        buffer_.push_back(c);
    }

    void
    append(std::uint8_t const* ptr, std::size_t len) override
    {
        assert(ptr != nullptr);

        if (len != 0)
            buffer_.insert(buffer_.end(), ptr, ptr + len);
    }
};

/** A serializer that uses an externally provided buffer.

    @note The buffer provided must remain valid for the lifetime of the
          serializer object.
 */
template <class Buffer>
class SerializerInto : public SerializerBase
{
    static_assert(
        std::is_same_v<Buffer, std::vector<std::uint8_t>> ||
        std::is_same_v<Buffer, std::vector<unsigned char>> ||
        std::is_same_v<Buffer, std::string>);

    Buffer* buffer_;

public:
    SerializerInto(Buffer& buffer, std::size_t reserve = 0) : buffer_(&buffer)
    {
        if (reserve)
            buffer_->reserve(reserve);
    }

    SerializerInto(SerializerInto const& other) = delete;

    SerializerInto(SerializerInto&& other) : buffer_(other.buffer_)
    {
        other.buffer_ = nullptr;
    }

    void
    clear() override
    {
        assert(buffer_ != nullptr);
        buffer_->clear();
    }

    [[nodiscard]] bool
    empty() const override
    {
        assert(buffer_ != nullptr);
        return buffer_->empty();
    }

    void
    append(std::uint8_t c) override
    {
        assert(buffer_ != nullptr);
        buffer_->push_back(c);
    }

    void
    append(std::uint8_t const* ptr, std::size_t len) override
    {
        assert(ptr != nullptr);
        if (len != 0)
            buffer_->insert(buffer_->end(), ptr, ptr + len);
    }
};

}  // namespace ripple

#endif
