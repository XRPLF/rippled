#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace xrpl {

class Serializer
{
private:
    // DEPRECATED
    Blob data_;

public:
    explicit Serializer(int n = 256)
    {
        data_.reserve(n);
    }

    Serializer(void const* data, std::size_t size)
    {
        data_.resize(size);

        if (size != 0u)
        {
            XRPL_ASSERT(data, "xrpl::Serializer::Serializer(void const*) : non-null input");
            std::memcpy(data_.data(), data, size);
        }
    }

    [[nodiscard]] Slice
    slice() const noexcept
    {
        return Slice(data_.data(), data_.size());
    }

    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return data_.size();
    }

    [[nodiscard]] void const*
    data() const noexcept
    {
        return data_.data();
    }

    // assemble functions
    int
    add8(unsigned char i);
    int
    add16(std::uint16_t i);

    template <typename T>
        requires(std::is_same_v<std::make_unsigned_t<std::remove_cv_t<T>>, std::uint32_t>)
    int
    add32(T i)
    {
        int const ret = data_.size();
        data_.push_back(static_cast<unsigned char>((i >> 24) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 16) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 8) & 0xff));
        data_.push_back(static_cast<unsigned char>(i & 0xff));
        return ret;
    }

    int
    add32(HashPrefix p);

    template <typename T>
        requires(std::is_same_v<std::make_unsigned_t<std::remove_cv_t<T>>, std::uint64_t>)
    int
    add64(T i)
    {
        int const ret = data_.size();
        data_.push_back(static_cast<unsigned char>((i >> 56) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 48) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 40) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 32) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 24) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 16) & 0xff));
        data_.push_back(static_cast<unsigned char>((i >> 8) & 0xff));
        data_.push_back(static_cast<unsigned char>(i & 0xff));
        return ret;
    }

    template <typename Integer>
    int addInteger(Integer);

    template <std::size_t Bits, class Tag>
    int
    addBitString(BaseUInt<Bits, Tag> const& v)
    {
        return addRaw(v.data(), v.size());
    }

    int
    addRaw(Blob const& vector);
    int
    addRaw(Slice slice);
    int
    addRaw(void const* ptr, int len);
    int
    addRaw(Serializer const& s);

    int
    addVL(Blob const& vector);
    int
    addVL(Slice const& slice);
    template <class Iter>
    int
    addVL(Iter begin, Iter end, int len);
    int
    addVL(void const* ptr, int len);

    // disassemble functions
    bool
    get8(int&, int offset) const;

    template <typename Integer>
    bool
    getInteger(Integer& number, int offset)
    {
        static auto const kBytes = sizeof(Integer);
        if ((offset + kBytes) > data_.size())
            return false;
        number = 0;

        auto ptr = &data_[offset];
        for (auto i = 0; i < kBytes; ++i)
        {
            if (i)
                number <<= 8;
            number |= *ptr++;
        }
        return true;
    }

    template <std::size_t Bits, typename Tag = void>
    bool
    getBitString(BaseUInt<Bits, Tag>& data, int offset) const
    {
        auto success = (offset + (Bits / 8)) <= data_.size();
        if (success)
            memcpy(data.begin(), &(data_.front()) + offset, (Bits / 8));
        return success;
    }

    int
    addFieldID(int type, int name);
    int
    addFieldID(SerializedTypeID type, int name)
    {
        return addFieldID(safeCast<int>(type), name);
    }

    // DEPRECATED
    [[nodiscard]] uint256
    getSHA512Half() const;

    // totality functions
    [[nodiscard]] Blob const&
    peekData() const
    {
        return data_;
    }
    [[nodiscard]] Blob
    getData() const
    {
        return data_;
    }
    Blob&
    modData()
    {
        return data_;
    }

    [[nodiscard]] int
    getDataLength() const
    {
        return data_.size();
    }
    [[nodiscard]] void const*
    getDataPtr() const
    {
        return data_.data();
    }
    void*
    getDataPtr()
    {
        return data_.data();
    }
    [[nodiscard]] int
    getLength() const
    {
        return data_.size();
    }
    [[nodiscard]] std::string
    getString() const
    {
        return std::string(static_cast<char const*>(getDataPtr()), size());
    }
    void
    erase()
    {
        data_.clear();
    }
    bool
    chop(int num);

    // vector-like functions
    Blob ::iterator
    begin()
    {
        return data_.begin();
    }
    Blob ::iterator
    end()
    {
        return data_.end();
    }
    [[nodiscard]] Blob ::const_iterator
    begin() const
    {
        return data_.begin();
    }
    [[nodiscard]] Blob ::const_iterator
    end() const
    {
        return data_.end();
    }
    void
    reserve(size_t n)
    {
        data_.reserve(n);
    }
    void
    resize(size_t n)
    {
        data_.resize(n);
    }
    [[nodiscard]] size_t
    capacity() const
    {
        return data_.capacity();
    }

    bool
    operator==(Blob const& v) const
    {
        return v == data_;
    }
    bool
    operator!=(Blob const& v) const
    {
        return v != data_;
    }
    bool
    operator==(Serializer const& v) const
    {
        return v.data_ == data_;
    }
    bool
    operator!=(Serializer const& v) const
    {
        return v.data_ != data_;
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
    int const ret = addEncoded(len);
    for (; begin != end; ++begin)
    {
        addRaw(begin->data(), begin->size());
#ifndef NDEBUG
        len -= begin->size();
#endif
    }
    XRPL_ASSERT(len == 0, "xrpl::Serializer::addVL : length matches distance");
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

    [[nodiscard]] bool
    empty() const noexcept
    {
        return remain_ == 0;
    }

    void
    reset() noexcept;

    [[nodiscard]] int
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
    std::int32_t
    geti32();

    std::uint64_t
    get64();
    std::int64_t
    geti64();

    template <std::size_t Bits, class Tag = void>
    BaseUInt<Bits, Tag>
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

    uint192
    get192()
    {
        return getBitString<192>();
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
BaseUInt<Bits, Tag>
SerialIter::getBitString()
{
    auto const n = Bits / 8;

    if (remain_ < n)
        Throw<std::runtime_error>("invalid SerialIter getBitString");

    auto const x = p_;

    p_ += n;
    used_ += n;
    remain_ -= n;

    return BaseUInt<Bits, Tag>::fromVoid(x);
}

}  // namespace xrpl
