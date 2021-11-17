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
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/SField.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <type_traits>

namespace ripple {

class Serializer
{
private:
    // DEPRECATED
    Blob mData;

public:
    explicit Serializer(int n = 256)
    {
        mData.reserve(n);
    }

    Serializer(void const* data, std::size_t size)
    {
        mData.resize(size);

        if (size)
        {
            assert(data != nullptr);
            std::memcpy(mData.data(), data, size);
        }
    }

    Slice
    slice() const noexcept
    {
        return Slice(mData.data(), mData.size());
    }

    std::size_t
    size() const noexcept
    {
        return mData.size();
    }

    void const*
    data() const noexcept
    {
        return mData.data();
    }

    // assemble functions
    int
    add8(unsigned char i);
    int
    add16(std::uint16_t i);
    int
    add32(std::uint32_t i);  // ledger indexes, account sequence, timestamps
    int
    add32(HashPrefix p);
    int
    add64(std::uint64_t i);  // native currency amounts

    template <typename Integer>
    int addInteger(Integer);

    template <std::size_t Bits, class Tag>
    int
    addBitString(base_uint<Bits, Tag> const& v)
    {
        return addRaw(v.data(), v.size());
    }

    int
    addRaw(Blob const& vector);
    int
    addRaw(Slice slice);
    int
    addRaw(const void* ptr, int len);
    int
    addRaw(const Serializer& s);

    int
    addVL(Blob const& vector);
    int
    addVL(Slice const& slice);
    template <class Iter>
    int
    addVL(Iter begin, Iter end, int len);
    int
    addVL(const void* ptr, int len);

    // disassemble functions
    bool
    get8(int&, int offset) const;

    template <typename Integer>
    bool
    getInteger(Integer& number, int offset)
    {
        static const auto bytes = sizeof(Integer);
        if ((offset + bytes) > mData.size())
            return false;
        number = 0;

        auto ptr = &mData[offset];
        for (auto i = 0; i < bytes; ++i)
        {
            if (i)
                number <<= 8;
            number |= *ptr++;
        }
        return true;
    }

    template <std::size_t Bits, typename Tag = void>
    bool
    getBitString(base_uint<Bits, Tag>& data, int offset) const
    {
        auto success = (offset + (Bits / 8)) <= mData.size();
        if (success)
            memcpy(data.begin(), &(mData.front()) + offset, (Bits / 8));
        return success;
    }

    int
    addFieldID(int type, int name);
    int
    addFieldID(SerializedTypeID type, int name)
    {
        return addFieldID(safe_cast<int>(type), name);
    }

    // DEPRECATED
    uint256
    getSHA512Half() const;

    // totality functions
    Blob const&
    peekData() const
    {
        return mData;
    }
    Blob
    getData() const
    {
        return mData;
    }
    Blob&
    modData()
    {
        return mData;
    }

    int
    getDataLength() const
    {
        return mData.size();
    }
    const void*
    getDataPtr() const
    {
        return mData.data();
    }
    void*
    getDataPtr()
    {
        return mData.data();
    }
    int
    getLength() const
    {
        return mData.size();
    }
    std::string
    getString() const
    {
        return std::string(static_cast<const char*>(getDataPtr()), size());
    }
    void
    erase()
    {
        mData.clear();
    }
    bool
    chop(int num);

    // vector-like functions
    Blob ::iterator
    begin()
    {
        return mData.begin();
    }
    Blob ::iterator
    end()
    {
        return mData.end();
    }
    Blob ::const_iterator
    begin() const
    {
        return mData.begin();
    }
    Blob ::const_iterator
    end() const
    {
        return mData.end();
    }
    void
    reserve(size_t n)
    {
        mData.reserve(n);
    }
    void
    resize(size_t n)
    {
        mData.resize(n);
    }
    size_t
    capacity() const
    {
        return mData.capacity();
    }

    bool
    operator==(Blob const& v) const
    {
        return v == mData;
    }
    bool
    operator!=(Blob const& v) const
    {
        return v != mData;
    }
    bool
    operator==(const Serializer& v) const
    {
        return v.mData == mData;
    }
    bool
    operator!=(const Serializer& v) const
    {
        return v.mData != mData;
    }

    static int
    decodeLengthLength(int b1);
    static int
    decodeVLLength(int b1);
    static int
    decodeVLLength(int b1, int b2);
    static int
    decodeVLLength(int b1, int b2, int b3);

private:
    static int
    encodeLengthLength(int length);  // length to encode length
    int
    addEncoded(int length);
};

template <class Iter>
int
Serializer::addVL(Iter begin, Iter end, int len)
{
    int ret = addEncoded(len);
    for (; begin != end; ++begin)
    {
        addRaw(begin->data(), begin->size());
#ifndef NDEBUG
        len -= begin->size();
#endif
    }
    assert(len == 0);
    return ret;
}

//------------------------------------------------------------------------------

// DEPRECATED
// Transitional adapter to new serialization interfaces
class SerialIter
{
private:
    std::uint8_t const* p_;
    std::size_t remain_;
    std::size_t used_ = 0;

public:
    SerialIter(void const* data, std::size_t size) noexcept;

    SerialIter(Slice const& slice) : SerialIter(slice.data(), slice.size())
    {
    }

    // Infer the size of the data based on the size of the passed array.
    template <int N>
    explicit SerialIter(std::uint8_t const (&data)[N]) : SerialIter(&data[0], N)
    {
        static_assert(N > 0, "");
    }

    std::size_t
    empty() const noexcept
    {
        return remain_ == 0;
    }

    void
    reset() noexcept;

    int
    getBytesLeft() const noexcept
    {
        return static_cast<int>(remain_);
    }

    // get functions throw on error
    unsigned char
    get8();

    std::uint16_t
    get16();

    std::uint32_t
    get32();

    std::uint64_t
    get64();

    template <std::size_t Bits, class Tag = void>
    base_uint<Bits, Tag>
    getBitString();

    uint128
    get128()
    {
        return getBitString<128>();
    }

    uint160
    get160()
    {
        return getBitString<160>();
    }

    uint256
    get256()
    {
        return getBitString<256>();
    }

    void
    getFieldID(int& type, int& name);

    // Returns the size of the VL if the
    // next object is a VL. Advances the iterator
    // to the beginning of the VL.
    int
    getVLDataLength();

    Slice
    getSlice(std::size_t bytes);

    // VFALCO DEPRECATED Returns a copy
    Blob
    getRaw(int size);

    // VFALCO DEPRECATED Returns a copy
    Blob
    getVL();

    void
    skip(int num);

    Buffer
    getVLBuffer();

    template <class T>
    T
    getRawHelper(int size);
};

template <std::size_t Bits, class Tag>
base_uint<Bits, Tag>
SerialIter::getBitString()
{
    auto const n = Bits / 8;

    if (remain_ < n)
        Throw<std::runtime_error>("invalid SerialIter getBitString");

    auto const x = p_;

    p_ += n;
    used_ += n;
    remain_ -= n;

    return base_uint<Bits, Tag>::fromVoid(x);
}

}  // namespace ripple

#endif
