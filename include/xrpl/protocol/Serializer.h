#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SField.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace xrpl {

class Serializer
{
private:
    // DEPRECATED
    Blob data_;

public:
    /**
     * A header is never longer than this. The encoder fills a buffer of this
     * size and writes only the bytes it used.
     */
    static constexpr int kMaxNumberOfBytesInHeader = 3;

    // A field whose size varies is stored as a header holding its length, then
    // the field data. The header is 1, 2 or 3 bytes long. Nothing outside it says
    // which, so the decoder reads the first byte and its value says how long the
    // header is:
    //
    //     0 ... 192   kMin/kMaxValueOfFirstByteFor1ByteHeader
    //   193 ... 240   kMin/kMaxValueOfFirstByteFor2ByteHeader
    //   241 ... 254   kMin/kMaxValueOfFirstByteFor3ByteHeader
    //         255     belongs to no header
    //
    // Each range starts one past the end of the range before it.

    static constexpr int kMinValueOfFirstByteFor1ByteHeader = 0;
    static constexpr int kMaxValueOfFirstByteFor1ByteHeader = 192;

    static constexpr int kMinValueOfFirstByteFor2ByteHeader =
        kMaxValueOfFirstByteFor1ByteHeader + 1;
    static constexpr int kMaxValueOfFirstByteFor2ByteHeader = 240;

    static constexpr int kMinValueOfFirstByteFor3ByteHeader =
        kMaxValueOfFirstByteFor2ByteHeader + 1;

    static constexpr int kMaxValueOfFirstByteFor3ByteHeader = 254;

    // A length x too big for one byte is split across the header. For 2 bytes:
    //
    //     first byte  = 193 + (x - 193) / 256
    //     second byte = (x - 193) % 256
    //
    // so 300 is stored as 193, 107. For 3 bytes it is the same, from 241, with
    // the remainder split across two bytes: 20,000 is stored as 241, 29, 95.

    static constexpr int kNumberOfValuesInOneByte = 256;
    static constexpr int kNumberOfValuesInTwoBytes =
        kNumberOfValuesInOneByte * kNumberOfValuesInOneByte;

    // Each header length therefore covers a range of field lengths:
    //
    //        0 ...     192   kMin/kMaxValueOfLengthFor1ByteHeader
    //      193 ...  12,480   kMin/kMaxValueOfLengthFor2ByteHeader
    //   12,481 ... 918,744   kMin/kMaxValueOfLengthFor3ByteHeader
    //
    // The encoder always uses the shortest header that fits.

    /**
     * A 1 byte header holds the length in the byte itself, so both ends of
     * this range are the same numbers as the first byte's own range.
     */
    static constexpr int kMinValueOfLengthFor1ByteHeader = kMinValueOfFirstByteFor1ByteHeader;
    static constexpr int kMaxValueOfLengthFor1ByteHeader = kMaxValueOfFirstByteFor1ByteHeader;

    static constexpr int kMinValueOfLengthFor2ByteHeader = kMaxValueOfLengthFor1ByteHeader + 1;

    /**
     * 48 values of the first byte mean a 2 byte header, and each of them covers
     * 256 lengths. The 48 is worked out from the two range ends above, so it
     * stays right if either of them changes.
     */
    static constexpr int kMaxValueOfLengthFor2ByteHeader = kMinValueOfLengthFor2ByteHeader +
        ((kMaxValueOfFirstByteFor2ByteHeader - kMaxValueOfFirstByteFor1ByteHeader) *
         kNumberOfValuesInOneByte) -
        1;

    static constexpr int kMinValueOfLengthFor3ByteHeader = kMaxValueOfLengthFor2ByteHeader + 1;

    /**
     * 14 values of the first byte mean a 3 byte header, and each of them covers
     * 65,536 lengths. Counted the same way, that gives the largest length any
     * header can state.
     *
     * Nothing is accepted or rejected against this. The assertion below uses it
     * to check that every length the encoder writes is one a header can state.
     */
    static constexpr int kMaxRepresentableLength = kMinValueOfLengthFor3ByteHeader +
        ((kMaxValueOfFirstByteFor3ByteHeader - kMaxValueOfFirstByteFor2ByteHeader) *
         kNumberOfValuesInTwoBytes) -
        1;

    /**
     * The largest length the encoder will write. This is the one number here
     * that is picked rather than worked out. The decoder accepts nothing above
     * it, so both sides agree on the same set of lengths.
     */
    static constexpr int kMaxValueOfLengthFor3ByteHeader = 918744;

    static_assert(
        kMaxValueOfLengthFor3ByteHeader <= kMaxRepresentableLength,
        "a length the encoder writes must be one a header can state");

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
    add8(unsigned char byteValue);
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
    operator==(Serializer const& v) const
    {
        return v.data_ == data_;
    }

    /**
     * Works out how long a header is, from its first byte.
     *
     * Each overload of decodeVLLength below reads one header length, so call
     * this first to learn which of them to call.
     *
     * @param firstByte First byte of the header, as read from the stream.
     * @return How many bytes the whole header takes, counting firstByte: 1, 2
     * or 3.
     * @throws std::overflow_error if firstByte is the one value that starts no
     * header.
     */
    static int
    decodeLengthLength(std::byte firstByte);

    /**
     * Reads the field length out of a 1 byte header.
     *
     * @param firstByte The single header byte, which is the length itself.
     * @return Field length in bytes, from kMinValueOfLengthFor1ByteHeader to
     * kMaxValueOfLengthFor1ByteHeader.
     * @throws std::overflow_error if firstByte is big enough to mean a longer
     * header, in which case it is not a length by itself.
     */
    static int
    decodeVLLength(std::byte firstByte);

    /**
     * Reads the field length out of a 2 byte header.
     *
     * @param firstByte First header byte. Its value means a 2 byte header, and
     * how far it sits into that range gives the top part of the length.
     * @param secondByte Second header byte, holding the rest of the length.
     * @return Field length in bytes, from kMinValueOfLengthFor2ByteHeader to
     * kMaxValueOfLengthFor2ByteHeader.
     * @throws std::overflow_error if firstByte is outside the range that means
     * a 2 byte header.
     */
    static int
    decodeVLLength(std::byte firstByte, std::byte secondByte);

    /**
     * Reads the field length out of a 3 byte header.
     *
     * @param firstByte First header byte. Its value means a 3 byte header, and
     * how far it sits into that range gives the top part of the length.
     * @param secondByte Second header byte, holding the middle part of the
     * length.
     * @param thirdByte Third header byte, holding the low part.
     * @return Field length in bytes, from kMinValueOfLengthFor3ByteHeader to
     * kMaxValueOfLengthFor3ByteHeader.
     * @throws std::overflow_error if firstByte is outside the range that means
     * a 3 byte header, or if the three bytes together state a length above
     * kMaxValueOfLengthFor3ByteHeader, which the encoder would not write back.
     */
    static int
    decodeVLLength(std::byte firstByte, std::byte secondByte, std::byte thirdByte);

private:
    /**
     * Works out how many bytes the header needs for the given length.
     *
     * This deliberately repeats the width choice addEncoded makes, so that
     * addVL's assertion can compare the two. It has no other caller; do not
     * reach for it as a utility.
     *
     * @param length Field length in bytes.
     * @return How many header bytes it needs: 1, 2 or 3.
     * @throws std::overflow_error if length is negative, or above
     * kMaxValueOfLengthFor3ByteHeader.
     */
    static int
    encodeLengthLength(int length);

    /**
     * Appends the length header for a field of the given length.
     *
     * The field's own data is not written; the caller appends it next.
     *
     * @param length Field length in bytes.
     * @return Offset within this Serializer at which the header was written.
     * @throws std::overflow_error if length is negative, or above
     * kMaxValueOfLengthFor3ByteHeader.
     */
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
        static_assert(N > 0);
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

    /**
     * Reads the length header at the read position and steps past it.
     *
     * @return Field length in bytes. The iterator is left on the first byte of
     * the field data.
     * @throws std::overflow_error if the header states a length the encoder could
     * not have written.
     * @throws std::runtime_error if the data runs out before the header does.
     */
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
