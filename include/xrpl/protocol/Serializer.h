/** @file
 *  Defines `Serializer` (write side) and `SerialIter` (read side) — the two
 *  classes that implement the XRPL canonical binary serialization format.
 *
 *  Every transaction, ledger object, and signed message exchanged across the
 *  XRP Ledger network is encoded using this format.  `Serializer` accumulates
 *  typed values in big-endian byte order; `SerialIter` consumes the resulting
 *  byte stream as a forward-only cursor.
 */
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

/** Accumulates bytes for XRPL canonical binary serialization (write side).
 *
 *  Every `add*` method appends data in big-endian byte order and returns the
 *  byte offset at which writing began, allowing callers to locate previously
 *  written slots for later inspection or patching.  The default constructor
 *  pre-reserves 256 bytes to avoid reallocation on typical transaction sizes.
 *
 *  @note The internal `Blob` (`std::vector<unsigned char>`) storage is
 *      deprecated.  New code should prefer zero-copy patterns built on
 *      `Slice` and `Buffer` where possible.
 */
class Serializer
{
private:
    // DEPRECATED
    Blob data_;

public:
    /** Construct a serializer, pre-reserving capacity.
     *
     *  @param n Initial byte capacity to reserve (default 256).
     */
    explicit Serializer(int n = 256)
    {
        data_.reserve(n);
    }

    /** Construct a serializer pre-populated with a copy of an existing buffer.
     *
     *  @param data Pointer to the source bytes.  Must be non-null when
     *      `size != 0`.
     *  @param size Number of bytes to copy.
     */
    Serializer(void const* data, std::size_t size)
    {
        data_.resize(size);

        if (size != 0u)
        {
            XRPL_ASSERT(data, "xrpl::Serializer::Serializer(void const*) : non-null input");
            std::memcpy(data_.data(), data, size);
        }
    }

    /** Return a non-owning view of the accumulated bytes. */
    [[nodiscard]] Slice
    slice() const noexcept
    {
        return Slice(data_.data(), data_.size());
    }

    /** Return the number of bytes accumulated so far. */
    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return data_.size();
    }

    /** Return a const pointer to the first accumulated byte. */
    [[nodiscard]] void const*
    data() const noexcept
    {
        return data_.data();
    }

    // assemble functions

    /** Append a single byte in big-endian order.
     *
     *  @param i Value to append.
     *  @return Byte offset at which the value was written.
     */
    int
    add8(unsigned char i);

    /** Append a 16-bit unsigned integer in big-endian byte order.
     *
     *  @param i Value to append.
     *  @return Byte offset at which the value was written.
     */
    int
    add16(std::uint16_t i);

    /** Append a 32-bit integer in big-endian byte order.
     *
     *  Accepts any type whose unsigned form is exactly `uint32_t` (i.e.
     *  `int32_t` or `uint32_t`), preventing accidental narrowing from wider
     *  types at compile time.
     *
     *  @tparam T An integer type whose unsigned counterpart is `uint32_t`.
     *  @param i Value to append.
     *  @return Byte offset at which the value was written.
     */
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

    /** Append a `HashPrefix` domain-separator as a big-endian 32-bit value.
     *
     *  Hash-domain prefixes (e.g. `TXN`, `STX`, `VAL`) are prepended to
     *  every signable or hashable payload to prevent cross-domain collisions.
     *  A `static_assert` in the implementation guards that `HashPrefix`'s
     *  underlying type remains `uint32_t`, which is an invariant of the wire
     *  format.
     *
     *  @param p The domain-separation prefix to append.
     *  @return Byte offset at which the prefix was written.
     */
    int
    add32(HashPrefix p);

    /** Append a 64-bit integer in big-endian byte order.
     *
     *  Accepts any type whose unsigned form is exactly `uint64_t` (i.e.
     *  `int64_t` or `uint64_t`), preventing accidental narrowing at compile
     *  time.
     *
     *  @tparam T An integer type whose unsigned counterpart is `uint64_t`.
     *  @param i Value to append.
     *  @return Byte offset at which the value was written.
     */
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

    /** Append an integer of any supported width in big-endian byte order.
     *
     *  Dispatches to `add8`, `add16`, `add32`, or `add64` based on `Integer`.
     *  Explicit specializations in the `.cpp` cover `unsigned char`,
     *  `uint16_t`, `uint32_t`, `int32_t`, and `uint64_t`.
     *
     *  @tparam Integer One of the supported integer types listed above.
     *  @param i Value to append.
     *  @return Byte offset at which the value was written.
     */
    template <typename Integer>
    int addInteger(Integer);

    /** Append the raw bytes of a fixed-width integer type without any prefix.
     *
     *  Covers `uint128`, `uint160`, `uint192`, `uint256`, and any other
     *  `BaseUInt` specialization.
     *
     *  @tparam Bits Bit width of the `BaseUInt` type.
     *  @tparam Tag  Distinguishing tag type of the `BaseUInt` specialization.
     *  @param v Value to append.
     *  @return Byte offset at which the value was written.
     */
    template <std::size_t Bits, class Tag>
    int
    addBitString(BaseUInt<Bits, Tag> const& v)
    {
        return addRaw(v.data(), v.size());
    }

    /** Append a raw byte sequence without any length prefix.
     *
     *  @param vector Bytes to append.
     *  @return Byte offset at which the data was written.
     */
    int
    addRaw(Blob const& vector);

    /** Append the bytes referenced by a `Slice` without any length prefix.
     *
     *  @param slice Non-owning view of bytes to append.
     *  @return Byte offset at which the data was written.
     */
    int
    addRaw(Slice slice);

    /** Append a raw memory region without any length prefix.
     *
     *  @param ptr Pointer to the first byte to append.
     *  @param len Number of bytes to copy from `ptr`.
     *  @return Byte offset at which the data was written.
     */
    int
    addRaw(void const* ptr, int len);

    /** Append all bytes accumulated in another `Serializer` without a length prefix.
     *
     *  @param s Source serializer whose buffer is appended in full.
     *  @return Byte offset at which the data was written.
     */
    int
    addRaw(Serializer const& s);

    /** Append a variable-length-prefixed blob using XRPL's three-tier VL encoding.
     *
     *  Writes a compact 1–3 byte length header followed by the raw bytes:
     *  0–192 bytes use a 1-byte header; 193–12,480 use 2 bytes; 12,481–918,744
     *  use 3 bytes.
     *
     *  @param vector Data to append.
     *  @return Byte offset at which the length header was written.
     *  @throws std::overflow_error if the data exceeds 918,744 bytes.
     */
    int
    addVL(Blob const& vector);

    /** Append a variable-length-prefixed blob from a `Slice`.
     *
     *  Writes a compact length header then the referenced bytes.  An empty
     *  slice writes the header only (length 0).
     *
     *  @param slice Non-owning view of the data to append.
     *  @return Byte offset at which the length header was written.
     *  @throws std::overflow_error if the slice exceeds 918,744 bytes.
     */
    int
    addVL(Slice const& slice);

    /** Append a variable-length-prefixed blob from an iterator range.
     *
     *  Writes the length header for a payload of `len` bytes, then iterates
     *  `[begin, end)` calling `addRaw` on each element's `.data()`/`.size()`.
     *  In debug builds an assertion verifies that the total bytes iterated
     *  equals `len`.
     *
     *  @tparam Iter Forward iterator whose value type exposes `.data()` and
     *      `.size()`.
     *  @param begin Start of the range.
     *  @param end   Past-the-end of the range.
     *  @param len   Total byte count of all elements in the range.
     *  @return Byte offset at which the length header was written.
     *  @throws std::overflow_error if `len` exceeds 918,744.
     */
    template <class Iter>
    int
    addVL(Iter begin, Iter end, int len);

    /** Append a variable-length-prefixed blob from a raw pointer.
     *
     *  Writes a compact length header then `len` bytes from `ptr`.  When
     *  `len == 0` only the header is written; `ptr` is not dereferenced.
     *
     *  @param ptr Pointer to the data to append.  May be null when `len == 0`.
     *  @param len Number of bytes to copy.
     *  @return Byte offset at which the length header was written.
     *  @throws std::overflow_error if `len` exceeds 918,744.
     */
    int
    addVL(void const* ptr, int len);

    // disassemble functions

    /** Read a single byte at a given offset without consuming it.
     *
     *  @param[out] i Output parameter set to the byte value on success.
     *  @param offset Zero-based byte offset into the internal buffer.
     *  @return `true` if `offset` is within bounds; `false` otherwise.
     */
    bool
    get8(int& i, int offset) const;

    /** Read an integer of any supported width from the given byte offset.
     *
     *  Assembles the value from big-endian bytes without consuming them.
     *
     *  @tparam Integer Target integer type; must fit within the buffer from
     *      `offset`.
     *  @param[out] number Set to the decoded value on success.
     *  @param offset Zero-based byte offset at which to start reading.
     *  @return `true` if `[offset, offset + sizeof(Integer))` is within
     *      bounds; `false` otherwise (and `number` is unmodified).
     */
    template <typename Integer>
    bool
    getInteger(Integer& number, int offset)
    {
        static auto const kBYTES = sizeof(Integer);
        if ((offset + kBYTES) > data_.size())
            return false;
        number = 0;

        auto ptr = &data_[offset];
        for (auto i = 0; i < kBYTES; ++i)
        {
            if (i)
                number <<= 8;
            number |= *ptr++;
        }
        return true;
    }

    /** Copy a fixed-width integer type out of the buffer at the given offset.
     *
     *  Uses `memcpy` directly into the `BaseUInt` storage; no byte-order
     *  conversion is performed, so the buffer must already contain the value
     *  in the expected byte order.
     *
     *  @tparam Bits Bit width of the `BaseUInt` type.
     *  @tparam Tag  Distinguishing tag type of the `BaseUInt` specialization.
     *  @param[out] data Destination for the extracted value.
     *  @param offset Zero-based byte offset at which to start reading.
     *  @return `true` if `[offset, offset + Bits/8)` is within bounds;
     *      `false` otherwise (and `data` is unmodified).
     */
    template <std::size_t Bits, typename Tag = void>
    bool
    getBitString(BaseUInt<Bits, Tag>& data, int offset) const
    {
        auto success = (offset + (Bits / 8)) <= data_.size();
        if (success)
            memcpy(data.begin(), &(data_.front()) + offset, (Bits / 8));
        return success;
    }

    /** Append a compact TLV field tag used by `STObject` serialization.
     *
     *  Encodes the (type, name) pair into 1, 2, or 3 bytes:
     *  - Both < 16: one byte `(type << 4) | name`.
     *  - Type < 16, name ≥ 16: two bytes — `(type << 4)` then `name`.
     *  - Type ≥ 16, name < 16: two bytes — `name` then `type`.
     *  - Both ≥ 16: three bytes — `0x00` sentinel, then `type`, then `name`.
     *
     *  @param type Serialized-type family code (1–255).
     *  @param name Per-type field index (1–255).
     *  @return Byte offset at which the tag was written.
     *  @note Both `type` and `name` must be in [1, 255]; an assertion fires in
     *      debug builds if either is out of range.
     */
    int
    addFieldID(int type, int name);

    /** Append a field tag using the `SerializedTypeID` enum as the type code.
     *
     *  Convenience overload that casts `type` to `int` before delegating to
     *  `addFieldID(int, int)`.
     *
     *  @param type Serialized-type family.
     *  @param name Per-type field index (1–255).
     *  @return Byte offset at which the tag was written.
     */
    int
    addFieldID(SerializedTypeID type, int name)
    {
        return addFieldID(safeCast<int>(type), name);
    }

    /** @deprecated Use `sha512Half(s.slice())` directly instead.
     *
     *  Compute the XRPL "SHA-512 half" hash over the accumulated buffer.
     *
     *  @return The first 256 bits of SHA-512 applied to the accumulated bytes.
     */
    // DEPRECATED
    [[nodiscard]] uint256
    getSHA512Half() const;

    // totality functions

    /** Return a const reference to the underlying byte vector.
     *
     *  @note The `Blob` type is deprecated; prefer `slice()` for new code.
     */
    [[nodiscard]] Blob const&
    peekData() const
    {
        return data_;
    }

    /** Return a copy of the accumulated byte vector.
     *
     *  @note Allocates; prefer `slice()` to avoid the copy.
     */
    [[nodiscard]] Blob
    getData() const
    {
        return data_;
    }

    /** Return a mutable reference to the underlying byte vector.
     *
     *  Intended for legacy callers that need to splice or overwrite bytes
     *  in place.  New code should not use this.
     */
    Blob&
    modData()
    {
        return data_;
    }

    /** Return the number of accumulated bytes.
     *
     *  @note Prefer `size()` for new code.
     */
    [[nodiscard]] int
    getDataLength() const
    {
        return data_.size();
    }

    /** Return a const pointer to the first accumulated byte. */
    [[nodiscard]] void const*
    getDataPtr() const
    {
        return data_.data();
    }

    /** Return a mutable pointer to the first accumulated byte. */
    void*
    getDataPtr()
    {
        return data_.data();
    }

    /** Return the number of accumulated bytes.
     *
     *  @note Alias for `getDataLength()`; prefer `size()` for new code.
     */
    [[nodiscard]] int
    getLength() const
    {
        return data_.size();
    }

    /** Return the accumulated bytes as a `std::string`. */
    [[nodiscard]] std::string
    getString() const
    {
        return std::string(static_cast<char const*>(getDataPtr()), size());
    }

    /** Clear all accumulated bytes, leaving the buffer empty. */
    void
    erase()
    {
        data_.clear();
    }

    /** Remove bytes from the end of the buffer.
     *
     *  @param num Number of bytes to remove.
     *  @return `true` on success; `false` if `num` exceeds the current size,
     *      leaving the buffer unchanged.
     */
    bool
    chop(int num);

    // vector-like functions

    /** Return a mutable iterator to the first byte. */
    Blob::iterator
    begin()
    {
        return data_.begin();
    }

    /** Return a mutable past-the-end iterator. */
    Blob::iterator
    end()
    {
        return data_.end();
    }

    /** Return a const iterator to the first byte. */
    [[nodiscard]] Blob::const_iterator
    begin() const
    {
        return data_.begin();
    }

    /** Return a const past-the-end iterator. */
    [[nodiscard]] Blob::const_iterator
    end() const
    {
        return data_.end();
    }

    /** Reserve capacity for at least `n` bytes without changing the size.
     *
     *  @param n Minimum byte capacity to reserve.
     */
    void
    reserve(size_t n)
    {
        data_.reserve(n);
    }

    /** Resize the buffer to exactly `n` bytes.
     *
     *  New bytes are zero-initialized; existing bytes beyond `n` are dropped.
     *
     *  @param n Target size in bytes.
     */
    void
    resize(size_t n)
    {
        data_.resize(n);
    }

    /** Return the number of bytes that can be held without reallocation. */
    [[nodiscard]] size_t
    capacity() const
    {
        return data_.capacity();
    }

    /** Compare the accumulated bytes against a raw `Blob` for equality. */
    bool
    operator==(Blob const& v) const
    {
        return v == data_;
    }

    /** Compare the accumulated bytes against a raw `Blob` for inequality. */
    bool
    operator!=(Blob const& v) const
    {
        return v != data_;
    }

    /** Compare two `Serializer` instances for byte-for-byte equality. */
    bool
    operator==(Serializer const& v) const
    {
        return v.data_ == data_;
    }

    /** Compare two `Serializer` instances for byte-for-byte inequality. */
    bool
    operator!=(Serializer const& v) const
    {
        return v.data_ != data_;
    }

    /** Return the number of header bytes used to encode a VL prefix.
     *
     *  Dispatches on the first header byte: ≤192 → 1 byte; 193–240 → 2
     *  bytes; 241–254 → 3 bytes.
     *
     *  @param b1 First byte of the VL header (0–254).
     *  @return 1, 2, or 3.
     *  @throws std::overflow_error if `b1` is negative or equals 255.
     */
    static int
    decodeLengthLength(int b1);

    /** Decode a one-byte VL length (0–192 range).
     *
     *  @param b1 The sole header byte.
     *  @return The decoded payload length.
     *  @throws std::overflow_error if `b1` is negative or > 254.
     */
    static int
    decodeVLLength(int b1);

    /** Decode a two-byte VL length (193–12,480 range).
     *
     *  Formula: `193 + (b1 - 193) * 256 + b2`.
     *
     *  @param b1 First header byte (193–240).
     *  @param b2 Second header byte.
     *  @return The decoded payload length.
     *  @throws std::overflow_error if `b1` is outside [193, 240].
     */
    static int
    decodeVLLength(int b1, int b2);

    /** Decode a three-byte VL length (12,481–918,744 range).
     *
     *  Formula: `12481 + (b1 - 241) * 65536 + b2 * 256 + b3`.
     *
     *  @param b1 First header byte (241–254).
     *  @param b2 Second header byte.
     *  @param b3 Third header byte.
     *  @return The decoded payload length.
     *  @throws std::overflow_error if `b1` is outside [241, 254].
     */
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

/** Forward-only cursor over an external byte buffer for XRPL deserialization
 *  (read side).
 *
 *  Stores a pointer into the caller-owned buffer together with `remain_` (bytes
 *  not yet consumed) and `used_` (bytes consumed).  All `get*` methods advance
 *  the cursor and throw `std::runtime_error` on underflow — error codes are not
 *  returned; malformed input is treated as an exceptional condition.
 *
 *  The buffer must outlive the iterator; no ownership is taken.  `reset()`
 *  rewinds to the original position in O(1) using `used_` as the rewind delta.
 *
 *  @note This class is deprecated as a direct dependency.  New code should
 *      prefer zero-copy patterns built on `Slice` and `Buffer`.  In
 *      particular, `getSlice()` is preferred over the copying `getRaw()`.
 */
// DEPRECATED
class SerialIter
{
private:
    std::uint8_t const* p_;
    std::size_t remain_;
    std::size_t used_ = 0;

public:
    /** Construct a cursor over an existing byte buffer.
     *
     *  The iterator does not take ownership; the caller must ensure that
     *  `data` remains valid for the iterator's lifetime.
     *
     *  @param data Pointer to the first byte of the buffer.
     *  @param size Total number of bytes available.
     */
    SerialIter(void const* data, std::size_t size) noexcept;

    /** Construct a cursor from a `Slice`.
     *
     *  @param slice Non-owning view of the buffer to iterate.
     */
    SerialIter(Slice const& slice) : SerialIter(slice.data(), slice.size())
    {
    }

    /** Construct a cursor from a fixed-size byte array.
     *
     *  The array size is inferred at compile time.
     *
     *  @tparam N Size of the array (must be > 0).
     *  @param data Reference to the byte array.
     */
    template <int N>
    explicit SerialIter(std::uint8_t const (&data)[N]) : SerialIter(&data[0], N)
    {
        static_assert(N > 0, "");
    }

    /** Return `true` if all bytes have been consumed. */
    [[nodiscard]] bool
    empty() const noexcept
    {
        return remain_ == 0;
    }

    /** Rewind the cursor to the beginning of the buffer.
     *
     *  O(1): uses `used_` as the rewind delta rather than storing a separate
     *  copy of the original pointer.
     */
    void
    reset() noexcept;

    /** Return the number of bytes not yet consumed. */
    [[nodiscard]] int
    getBytesLeft() const noexcept
    {
        return static_cast<int>(remain_);
    }

    // get functions throw on error

    /** Consume and return the next byte.
     *
     *  @return The byte at the current cursor position.
     *  @throws std::runtime_error if the buffer is exhausted.
     */
    unsigned char
    get8();

    /** Consume and decode the next 2 bytes as a big-endian unsigned 16-bit integer.
     *
     *  @return Decoded value.
     *  @throws std::runtime_error if fewer than 2 bytes remain.
     */
    std::uint16_t
    get16();

    /** Consume and decode the next 4 bytes as a big-endian unsigned 32-bit integer.
     *
     *  Use `geti32()` for signed values.
     *
     *  @return Decoded value.
     *  @throws std::runtime_error if fewer than 4 bytes remain.
     */
    std::uint32_t
    get32();

    /** Consume and decode the next 4 bytes as a big-endian signed 32-bit integer.
     *
     *  Uses `boost::endian::load_big_s32` to ensure correct two's-complement
     *  sign extension.
     *
     *  @return Decoded value.
     *  @throws std::runtime_error if fewer than 4 bytes remain.
     */
    std::int32_t
    geti32();

    /** Consume and decode the next 8 bytes as a big-endian unsigned 64-bit integer.
     *
     *  Use `geti64()` for signed values.
     *
     *  @return Decoded value.
     *  @throws std::runtime_error if fewer than 8 bytes remain.
     */
    std::uint64_t
    get64();

    /** Consume and decode the next 8 bytes as a big-endian signed 64-bit integer.
     *
     *  Uses `boost::endian::load_big_s64` to ensure correct two's-complement
     *  sign extension.
     *
     *  @return Decoded value.
     *  @throws std::runtime_error if fewer than 8 bytes remain.
     */
    std::int64_t
    geti64();

    /** Consume and return the next `Bits/8` bytes as a `BaseUInt<Bits, Tag>`.
     *
     *  Constructs the result via `BaseUInt::fromVoid`, providing zero-copy
     *  extraction of fixed-width types such as `uint128`, `uint160`, `uint192`,
     *  and `uint256`.
     *
     *  @tparam Bits Bit width of the target type (must be a multiple of 8).
     *  @tparam Tag  Distinguishing tag type of the `BaseUInt` specialization.
     *  @return The decoded value.
     *  @throws std::runtime_error if fewer than `Bits/8` bytes remain.
     */
    template <std::size_t Bits, class Tag = void>
    BaseUInt<Bits, Tag>
    getBitString();

    /** Consume and return the next 16 bytes as a `uint128`. */
    uint128
    get128()
    {
        return getBitString<128>();
    }

    /** Consume and return the next 20 bytes as a `uint160`. */
    uint160
    get160()
    {
        return getBitString<160>();
    }

    /** Consume and return the next 24 bytes as a `uint192`. */
    uint192
    get192()
    {
        return getBitString<192>();
    }

    /** Consume and return the next 32 bytes as a `uint256`. */
    uint256
    get256()
    {
        return getBitString<256>();
    }

    /** Decode and consume the next field-ID tag, inverse of `Serializer::addFieldID`.
     *
     *  Reads 1–3 bytes depending on the packing scheme.
     *
     *  @param[out] type Decoded type family code (≥ 1).
     *  @param[out] name Decoded per-type field index (≥ 1).
     *  @throws std::runtime_error if the buffer is exhausted or a decoded
     *      uncommon code is < 16 (which would be ambiguous with the common
     *      single-byte encoding).
     */
    void
    getFieldID(int& type, int& name);

    /** Decode and consume the variable-length header, returning the payload size.
     *
     *  Reads 1–3 header bytes and advances the cursor to the first byte of
     *  the payload.
     *
     *  @return Decoded payload length in bytes.
     *  @throws std::runtime_error if the buffer is exhausted mid-header.
     *  @throws std::overflow_error if the first byte is outside the valid range.
     */
    int
    getVLDataLength();

    /** Return a zero-copy view of the next `bytes` bytes and advance the cursor.
     *
     *  The returned `Slice` points directly into the underlying buffer and is
     *  valid only while that buffer is alive.  Prefer this over `getRaw()`
     *  when an allocation can be avoided.
     *
     *  @param bytes Number of bytes to expose.
     *  @return A `Slice` referencing the requested region.
     *  @throws std::runtime_error if `bytes` exceeds the remaining bytes.
     */
    Slice
    getSlice(std::size_t bytes);

    /** @deprecated Prefer `getSlice()` to avoid allocation.
     *
     *  Copy `size` bytes from the current position into a new `Blob` and
     *  advance the cursor.
     *
     *  @param size Number of bytes to copy.
     *  @return A `Blob` containing the copied bytes.
     *  @throws std::runtime_error if `size` exceeds the remaining bytes.
     */
    Blob
    getRaw(int size);

    /** @deprecated Prefer `getVLBuffer()` or `getVLDataLength()` + `getSlice()`.
     *
     *  Decode the VL header and return a copy of the payload as a `Blob`.
     *
     *  @return A `Blob` containing the VL payload.
     *  @throws std::runtime_error if the buffer is exhausted.
     */
    Blob
    getVL();

    /** Advance the cursor by `num` bytes without reading the data.
     *
     *  @param num Number of bytes to skip.
     *  @throws std::runtime_error if `num` exceeds the remaining bytes.
     */
    void
    skip(int num);

    /** Decode the VL header and return the payload as a move-only `Buffer`.
     *
     *  Equivalent to `getVL()` but avoids the SSO overhead of `std::vector`.
     *  Prefer this over `getVL()` for new callers.
     *
     *  @return A `Buffer` containing the VL payload.
     *  @throws std::runtime_error if the buffer is exhausted.
     */
    Buffer
    getVLBuffer();

    /** Copy `size` bytes from the current position into a new container of
     *  type `T` and advance the cursor.
     *
     *  `T` must be either `Blob` or `Buffer`.  The `size == 0` guard skips
     *  `memcpy` because passing a null pointer — which an empty `Buffer` may
     *  have — to `memcpy` with a zero count is undefined behavior in C++.
     *
     *  @tparam T Either `Blob` or `Buffer`.
     *  @param size Number of bytes to copy.
     *  @return A freshly allocated container holding the copied bytes.
     *  @throws std::runtime_error if `size` exceeds the remaining bytes.
     */
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
