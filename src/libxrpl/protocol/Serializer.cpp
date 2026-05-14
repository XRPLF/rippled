/** @file
 *  Implements the binary serialization backbone of the XRP Ledger protocol.
 *
 *  `Serializer` (write side) appends typed values to a growing byte buffer;
 *  every `add*` method returns the byte offset at which writing began so
 *  callers can patch previously written slots.  `SerialIter` (read side) is a
 *  non-owning forward cursor that throws `std::runtime_error` on underrun.
 *  Together they encode and decode the canonical wire format used by every
 *  transaction, ledger object, and cryptographic hash in the system.
 */
#include <xrpl/protocol/Serializer.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>

#include <boost/endian/conversion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace xrpl {

/** Append a 16-bit unsigned integer in big-endian byte order.
 *
 *  @param i Value to append.
 *  @return Byte offset at which the value was written.
 */
int
Serializer::add16(std::uint16_t i)
{
    int const ret = data_.size();
    data_.push_back(static_cast<unsigned char>(i >> 8));
    data_.push_back(static_cast<unsigned char>(i & 0xff));
    return ret;
}

/** Append a `HashPrefix` domain-separator as a big-endian 32-bit value.
 *
 *  `HashPrefix` is an `enum class : std::uint32_t` whose values are
 *  prepended to data before signing or hashing to prevent cross-domain hash
 *  collisions (e.g., `TXN` for transaction IDs, `STX` for signing payloads).
 *  A `static_assert` guards against a future accidental change to the enum's
 *  underlying type, which would silently corrupt the wire format.
 *
 *  @param p The domain-separation prefix to append.
 *  @return Byte offset at which the prefix was written.
 */
int
Serializer::add32(HashPrefix p)
{
    // This should never trigger; the size & type of a hash prefix are
    // integral parts of the protocol and unlikely to ever change.
    static_assert(std::is_same_v<std::uint32_t, std::underlying_type_t<decltype(p)>>);

    return add32(safeCast<std::uint32_t>(p));
}

template <>
int
Serializer::addInteger(unsigned char i)
{
    return add8(i);
}
template <>
int
Serializer::addInteger(std::uint16_t i)
{
    return add16(i);
}
template <>
int
Serializer::addInteger(std::uint32_t i)
{
    return add32(i);
}
template <>
int
Serializer::addInteger(std::uint64_t i)
{
    return add64(i);
}
template <>
int
Serializer::addInteger(std::int32_t i)
{
    return add32(i);
}

/** Append a raw byte sequence without any length prefix.
 *
 *  @param vector Bytes to append.
 *  @return Byte offset at which the data was written.
 */
int
Serializer::addRaw(Blob const& vector)
{
    int const ret = data_.size();
    data_.insert(data_.end(), vector.begin(), vector.end());
    return ret;
}

/** Append the bytes referenced by a `Slice` without any length prefix.
 *
 *  @param slice Non-owning view of bytes to append.
 *  @return Byte offset at which the data was written.
 */
int
Serializer::addRaw(Slice slice)
{
    int const ret = data_.size();
    data_.insert(data_.end(), slice.begin(), slice.end());
    return ret;
}

/** Append all bytes accumulated in another `Serializer` without a length prefix.
 *
 *  @param s Source serializer whose buffer is appended in full.
 *  @return Byte offset at which the data was written.
 */
int
Serializer::addRaw(Serializer const& s)
{
    int const ret = data_.size();
    data_.insert(data_.end(), s.begin(), s.end());
    return ret;
}

/** Append a raw memory region without any length prefix.
 *
 *  @param ptr Pointer to the first byte to append.
 *  @param len Number of bytes to copy from `ptr`.
 *  @return Byte offset at which the data was written.
 */
int
Serializer::addRaw(void const* ptr, int len)
{
    int const ret = data_.size();
    data_.insert(data_.end(), (char const*)ptr, ((char const*)ptr) + len);
    return ret;
}

/** Append a compact TLV field tag used by `STObject` serialization.
 *
 *  Encodes the (type, name) pair into 1, 2, or 3 bytes according to the
 *  XRPL field-ID packing scheme:
 *
 *  - Both < 16 (common/common): one byte `(type << 4) | name`.
 *  - Type < 16, name ≥ 16 (common type, uncommon name): `type << 4` then
 *      `name`.
 *  - Type ≥ 16, name < 16 (uncommon type, common name): `name` then `type`
 *      — note the reversed order.
 *  - Both ≥ 16 (uncommon/uncommon): leading `0x00` sentinel, then `type`,
 *      then `name`.
 *
 *  The leading zero in the three-byte form signals the decoder that the
 *  subsequent two bytes are a full type+name pair.
 *
 *  @param type Serialized-type family code (1–255).
 *  @param name Per-type field index (1–255).
 *  @return Byte offset at which the tag was written.
 *  @note Both `type` and `name` must be in [1, 255]; the assertion fires in
 *      debug builds if either is out of range.
 */
int
Serializer::addFieldID(int type, int name)
{
    int const ret = data_.size();
    XRPL_ASSERT(
        (type > 0) && (type < 256) && (name > 0) && (name < 256),
        "xrpl::Serializer::addFieldID : inputs inside range");

    if (type < 16)
    {
        if (name < 16)
        {
            // common type, common name
            data_.push_back(static_cast<unsigned char>((type << 4) | name));
        }
        else
        {
            // common type, uncommon name
            data_.push_back(static_cast<unsigned char>(type << 4));
            data_.push_back(static_cast<unsigned char>(name));
        }
    }
    else if (name < 16)
    {
        // uncommon type, common name
        data_.push_back(static_cast<unsigned char>(name));
        data_.push_back(static_cast<unsigned char>(type));
    }
    else
    {
        // uncommon type, uncommon name
        data_.push_back(static_cast<unsigned char>(0));
        data_.push_back(static_cast<unsigned char>(type));
        data_.push_back(static_cast<unsigned char>(name));
    }

    return ret;
}

/** Append a single byte.
 *
 *  @param byte Value to append.
 *  @return Byte offset at which the value was written.
 */
int
Serializer::add8(unsigned char byte)
{
    int const ret = data_.size();
    data_.push_back(byte);
    return ret;
}

/** Read a single byte at the given byte offset without consuming it.
 *
 *  @param byte Output parameter; set to the byte value on success.
 *  @param offset Zero-based byte offset into the internal buffer.
 *  @return `true` if `offset` is within bounds; `false` otherwise.
 */
bool
Serializer::get8(int& byte, int offset) const
{
    if (offset >= data_.size())
        return false;

    byte = data_[offset];
    return true;
}

/** Remove bytes from the end of the buffer.
 *
 *  @param bytes Number of bytes to remove.
 *  @return `true` on success; `false` if `bytes` exceeds the current size,
 *      leaving the buffer unchanged.
 */
bool
Serializer::chop(int bytes)
{
    if (bytes > data_.size())
        return false;

    data_.resize(data_.size() - bytes);
    return true;
}

/** Compute SHA-512 over the buffer and return the first 256 bits.
 *
 *  This is XRPL's standard hashing primitive (`sha512Half`), used for
 *  transaction IDs, SHAMap inner-node hashes, and signing digests.
 *
 *  @return The first 256 bits of SHA-512 applied to the accumulated bytes.
 */
uint256
Serializer::getSHA512Half() const
{
    return sha512Half(makeSlice(data_));
}

/** Append a variable-length-prefixed blob using XRPL's three-tier encoding.
 *
 *  Writes a compact length header (1–3 bytes) followed by the raw bytes.
 *  An assertion verifies the buffer grew by exactly `data_size + header_size`.
 *
 *  @param vector Data to append.
 *  @return Byte offset at which the length header was written.
 *  @throws std::overflow_error if the data exceeds 918,744 bytes.
 *  @see addEncoded
 */
int
Serializer::addVL(Blob const& vector)
{
    int const ret = addEncoded(vector.size());
    addRaw(vector);
    XRPL_ASSERT(
        data_.size() == (ret + vector.size() + encodeLengthLength(vector.size())),
        "xrpl::Serializer::addVL : size matches expected");
    return ret;
}

/** Append a variable-length-prefixed blob from a `Slice`.
 *
 *  Writes a compact length header then the referenced bytes.  An empty slice
 *  writes the header only (length 0).
 *
 *  @param slice Non-owning view of the data to append.
 *  @return Byte offset at which the length header was written.
 *  @throws std::overflow_error if the slice exceeds 918,744 bytes.
 */
int
Serializer::addVL(Slice const& slice)
{
    int const ret = addEncoded(slice.size());
    if (!slice.empty())
        addRaw(slice.data(), slice.size());
    return ret;
}

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
Serializer::addVL(void const* ptr, int len)
{
    int const ret = addEncoded(len);

    if (len != 0)
        addRaw(ptr, len);

    return ret;
}

/** Write the XRPL variable-length header for a payload of the given size.
 *
 *  Encoding tiers (identical to `decodeLengthLength` / `decodeVLLength`):
 *  - 0–192      → 1 byte (direct value).
 *  - 193–12,480 → 2 bytes, bias-193 big-endian.
 *  - 12,481–918,744 → 3 bytes, bias-12,481 big-endian.
 *
 *  @param length Number of data bytes that will follow.
 *  @return Byte offset at which the header was written.
 *  @throws std::overflow_error if `length` exceeds 918,744.
 */
int
Serializer::addEncoded(int length)
{
    std::array<std::uint8_t, 4> bytes{};
    int numBytes = 0;

    if (length <= 192)
    {
        bytes[0] = static_cast<unsigned char>(length);
        numBytes = 1;
    }
    else if (length <= 12480)
    {
        length -= 193;
        bytes[0] = 193 + static_cast<unsigned char>(length >> 8);
        bytes[1] = static_cast<unsigned char>(length & 0xff);
        numBytes = 2;
    }
    else if (length <= 918744)
    {
        length -= 12481;
        bytes[0] = 241 + static_cast<unsigned char>(length >> 16);
        bytes[1] = static_cast<unsigned char>((length >> 8) & 0xff);
        bytes[2] = static_cast<unsigned char>(length & 0xff);
        numBytes = 3;
    }
    else
    {
        Throw<std::overflow_error>("lenlen");
    }

    return addRaw(&bytes[0], numBytes);
}

/** Return the number of header bytes required to encode a VL prefix of the
 *  given data length.
 *
 *  Used by `addVL` to assert that the buffer grew by the correct amount.
 *
 *  @param length Data payload size in bytes.
 *  @return 1, 2, or 3.
 *  @throws std::overflow_error if `length` is negative or exceeds 918,744.
 */
int
Serializer::encodeLengthLength(int length)
{
    if (length < 0)
        Throw<std::overflow_error>("len<0");

    if (length <= 192)
        return 1;

    if (length <= 12480)
        return 2;

    if (length <= 918744)
        return 3;

    Throw<std::overflow_error>("len>918744");
    return 0;  // Silence compiler warning.
}

/** Return the total number of length-header bytes given the first byte.
 *
 *  Dispatches by first-byte range: ≤192 → 1 byte; 193–240 → 2 bytes;
 *  241–254 → 3 bytes.  Used by `SerialIter::getVLDataLength` to know how
 *  many additional bytes to consume before assembling the final length.
 *
 *  @param b1 First byte of the VL header (0–254).
 *  @return 1, 2, or 3.
 *  @throws std::overflow_error if `b1` is negative or equals 255 (reserved).
 */
int
Serializer::decodeLengthLength(int b1)
{
    if (b1 < 0)
        Throw<std::overflow_error>("b1<0");

    if (b1 <= 192)
        return 1;

    if (b1 <= 240)
        return 2;

    if (b1 <= 254)
        return 3;

    Throw<std::overflow_error>("b1>254");
    return 0;  // Silence compiler warning.
}

/** Decode a one-byte VL length (0–192 range).
 *
 *  @param b1 The sole header byte (must be 0–254).
 *  @return The decoded data length.
 *  @throws std::overflow_error if `b1` is negative or > 254.
 */
int
Serializer::decodeVLLength(int b1)
{
    if (b1 < 0)
        Throw<std::overflow_error>("b1<0");

    if (b1 > 254)
        Throw<std::overflow_error>("b1>254");

    return b1;
}

/** Decode a two-byte VL length (193–12,480 range).
 *
 *  Formula: `193 + (b1 - 193) * 256 + b2`.
 *
 *  @param b1 First header byte (193–240).
 *  @param b2 Second header byte.
 *  @return The decoded data length.
 *  @throws std::overflow_error if `b1` is outside [193, 240].
 */
int
Serializer::decodeVLLength(int b1, int b2)
{
    if (b1 < 193)
        Throw<std::overflow_error>("b1<193");

    if (b1 > 240)
        Throw<std::overflow_error>("b1>240");

    return 193 + ((b1 - 193) * 256) + b2;
}

/** Decode a three-byte VL length (12,481–918,744 range).
 *
 *  Formula: `12481 + (b1 - 241) * 65536 + b2 * 256 + b3`.
 *
 *  @param b1 First header byte (241–254).
 *  @param b2 Second header byte.
 *  @param b3 Third header byte.
 *  @return The decoded data length.
 *  @throws std::overflow_error if `b1` is outside [241, 254].
 */
int
Serializer::decodeVLLength(int b1, int b2, int b3)
{
    if (b1 < 241)
        Throw<std::overflow_error>("b1<241");

    if (b1 > 254)
        Throw<std::overflow_error>("b1>254");

    return 12481 + ((b1 - 241) * 65536) + (b2 * 256) + b3;
}

//------------------------------------------------------------------------------

/** Construct a cursor over an existing byte buffer.
 *
 *  The iterator does not take ownership of the buffer; the caller must ensure
 *  that `data` remains valid for the lifetime of this object.
 *
 *  @param data Pointer to the first byte of the buffer.
 *  @param size Total number of bytes available.
 */
SerialIter::SerialIter(void const* data, std::size_t size) noexcept
    : p_(reinterpret_cast<std::uint8_t const*>(data)), remain_(size)
{
}

/** Rewind the cursor to the beginning of the buffer.
 *
 *  Restores `p_`, `remain_`, and `used_` to their construction-time values.
 *  Cheap O(1) operation: uses `used_` as the rewind offset rather than
 *  storing a separate copy of the original pointer.
 */
void
SerialIter::reset() noexcept
{
    p_ -= used_;
    remain_ += used_;
    used_ = 0;
}

/** Advance the cursor by `length` bytes without reading the data.
 *
 *  @param length Number of bytes to skip.
 *  @throws std::runtime_error if `length` exceeds the remaining bytes.
 */
void
SerialIter::skip(int length)
{
    if (remain_ < length)
        Throw<std::runtime_error>("invalid SerialIter skip");
    p_ += length;
    used_ += length;
    remain_ -= length;
}

/** Consume and return the next byte.
 *
 *  @return The byte at the current cursor position.
 *  @throws std::runtime_error if the buffer is exhausted.
 */
unsigned char
SerialIter::get8()
{
    if (remain_ < 1)
        Throw<std::runtime_error>("invalid SerialIter get8");
    unsigned char const t = *p_;
    ++p_;
    ++used_;
    --remain_;
    return t;
}

/** Consume and decode the next 2 bytes as a big-endian unsigned 16-bit integer.
 *
 *  @return Decoded value.
 *  @throws std::runtime_error if fewer than 2 bytes remain.
 */
std::uint16_t
SerialIter::get16()
{
    if (remain_ < 2)
        Throw<std::runtime_error>("invalid SerialIter get16");
    auto t = p_;
    p_ += 2;
    used_ += 2;
    remain_ -= 2;
    return (std::uint64_t(t[0]) << 8) + std::uint64_t(t[1]);
}

/** Consume and decode the next 4 bytes as a big-endian unsigned 32-bit integer.
 *
 *  Uses explicit bit-shift assembly; does not perform sign extension.
 *  Use `geti32` for signed values.
 *
 *  @return Decoded value.
 *  @throws std::runtime_error if fewer than 4 bytes remain.
 */
std::uint32_t
SerialIter::get32()
{
    if (remain_ < 4)
        Throw<std::runtime_error>("invalid SerialIter get32");
    auto t = p_;
    p_ += 4;
    used_ += 4;
    remain_ -= 4;
    return (std::uint64_t(t[0]) << 24) + (std::uint64_t(t[1]) << 16) + (std::uint64_t(t[2]) << 8) +
        std::uint64_t(t[3]);
}

/** Consume and decode the next 8 bytes as a big-endian unsigned 64-bit integer.
 *
 *  Uses explicit bit-shift assembly; does not perform sign extension.
 *  Use `geti64` for signed values.
 *
 *  @return Decoded value.
 *  @throws std::runtime_error if fewer than 8 bytes remain.
 */
std::uint64_t
SerialIter::get64()
{
    if (remain_ < 8)
        Throw<std::runtime_error>("invalid SerialIter get64");
    auto t = p_;
    p_ += 8;
    used_ += 8;
    remain_ -= 8;
    return (std::uint64_t(t[0]) << 56) + (std::uint64_t(t[1]) << 48) + (std::uint64_t(t[2]) << 40) +
        (std::uint64_t(t[3]) << 32) + (std::uint64_t(t[4]) << 24) + (std::uint64_t(t[5]) << 16) +
        (std::uint64_t(t[6]) << 8) + std::uint64_t(t[7]);
}

/** Consume and decode the next 4 bytes as a big-endian signed 32-bit integer.
 *
 *  Uses `boost::endian::load_big_s32` rather than manual shifts to ensure
 *  correct two's-complement sign extension for values with the high bit set.
 *
 *  @return Decoded value.
 *  @throws std::runtime_error if fewer than 4 bytes remain.
 */
std::int32_t
SerialIter::geti32()
{
    if (remain_ < 4)
        Throw<std::runtime_error>("invalid SerialIter geti32");
    auto t = p_;
    p_ += 4;
    used_ += 4;
    remain_ -= 4;
    return boost::endian::load_big_s32(t);
}

/** Consume and decode the next 8 bytes as a big-endian signed 64-bit integer.
 *
 *  Uses `boost::endian::load_big_s64` rather than manual shifts to ensure
 *  correct two's-complement sign extension for values with the high bit set.
 *
 *  @return Decoded value.
 *  @throws std::runtime_error if fewer than 8 bytes remain.
 */
std::int64_t
SerialIter::geti64()
{
    if (remain_ < 8)
        Throw<std::runtime_error>("invalid SerialIter geti64");
    auto t = p_;
    p_ += 8;
    used_ += 8;
    remain_ -= 8;
    return boost::endian::load_big_s64(t);
}

/** Decode and consume the next field-ID tag, inverse of `Serializer::addFieldID`.
 *
 *  Reads 1–3 bytes depending on the packing scheme: a high nibble of zero in
 *  the first byte signals an uncommon type and causes an additional byte to be
 *  read; a low nibble of zero signals an uncommon name and likewise.
 *
 *  @param type Output: decoded type family code (≥ 1).
 *  @param name Output: decoded per-type field index (≥ 1).
 *  @throws std::runtime_error if the buffer is exhausted or an uncommon
 *      code is decoded as < 16 (which would be ambiguous with the common
 *      encoding).
 */
void
SerialIter::getFieldID(int& type, int& name)
{
    type = get8();
    name = type & 15;
    type >>= 4;

    if (type == 0)
    {
        // uncommon type
        type = get8();
        if (type < 16)
            Throw<std::runtime_error>("gFID: uncommon type out of range " + std::to_string(type));
    }

    if (name == 0)
    {
        // uncommon name
        name = get8();
        if (name < 16)
            Throw<std::runtime_error>("gFID: uncommon name out of range " + std::to_string(name));
    }
}

/** Copy `size` bytes from the current cursor position into a new `Blob` or
 *  `Buffer`.
 *
 *  The `size == 0` guard skips `memcpy` entirely: an empty `Blob`/`Buffer`
 *  may have a null `data()` pointer, and passing null to `memcpy` even with
 *  a zero count is undefined behavior in C++ (C99 §7.21.1/2 notwithstanding).
 *
 *  @tparam T Either `Blob` or `Buffer`.
 *  @param size Number of bytes to copy.
 *  @return A freshly allocated container holding the copied bytes.
 *  @throws std::runtime_error if `size` exceeds the remaining bytes.
 */
template <class T>
T
SerialIter::getRawHelper(int size)
{
    static_assert(std::is_same_v<T, Blob> || std::is_same_v<T, Buffer>, "");
    if (remain_ < size)
        Throw<std::runtime_error>("invalid SerialIter getRaw");
    T result(size);
    if (size != 0)
    {
        // It's normally safe to call memcpy with size set to 0 (see the
        // C99 standard 7.21.1/2). However, here this could mean that
        // result.data would be null, which would trigger undefined behavior.
        std::memcpy(result.data(), p_, size);
        p_ += size;
        used_ += size;
        remain_ -= size;
    }
    return result;
}

/** @deprecated Prefer `getSlice` to avoid allocation.
 *
 *  Copy `size` bytes from the current position and advance the cursor.
 *
 *  @param size Number of bytes to copy.
 *  @return A `Blob` containing the copied bytes.
 *  @throws std::runtime_error if `size` exceeds the remaining bytes.
 */
Blob
SerialIter::getRaw(int size)
{
    return getRawHelper<Blob>(size);
}

/** Decode and consume the variable-length header, returning the payload size.
 *
 *  Reads 1–3 bytes by first calling `get8()` on the leading byte, then
 *  using `Serializer::decodeLengthLength` to determine whether 1 or 2
 *  additional bytes are needed, and finally calling the matching
 *  `Serializer::decodeVLLength` overload to assemble the final value.
 *  After this call the cursor sits at the first byte of the payload.
 *
 *  @return Decoded payload length in bytes.
 *  @throws std::runtime_error if the buffer is exhausted mid-header.
 *  @throws std::overflow_error if the first byte is out of the valid range.
 */
int
SerialIter::getVLDataLength()
{
    int const b1 = get8();
    int datLen = 0;
    int const lenLen = Serializer::decodeLengthLength(b1);
    if (lenLen == 1)
    {
        datLen = Serializer::decodeVLLength(b1);
    }
    else if (lenLen == 2)
    {
        int const b2 = get8();
        datLen = Serializer::decodeVLLength(b1, b2);
    }
    else
    {
        XRPL_ASSERT(lenLen == 3, "xrpl::SerialIter::getVLDataLength : lenLen is 3");
        int const b2 = get8();
        int const b3 = get8();
        datLen = Serializer::decodeVLLength(b1, b2, b3);
    }
    return datLen;
}

/** Return a zero-copy view of the next `bytes` bytes and advance the cursor.
 *
 *  The returned `Slice` points directly into the underlying buffer; it is
 *  valid only as long as the buffer outlives the slice.  Preferred over
 *  `getRaw` when an allocation can be avoided.
 *
 *  @param bytes Number of bytes to expose.
 *  @return A `Slice` referencing the requested region.
 *  @throws std::runtime_error if `bytes` exceeds the remaining bytes.
 */
Slice
SerialIter::getSlice(std::size_t bytes)
{
    if (bytes > remain_)
        Throw<std::runtime_error>("invalid SerialIter getSlice");
    Slice const s(p_, bytes);
    p_ += bytes;
    used_ += bytes;
    remain_ -= bytes;
    return s;
}

/** @deprecated Prefer `getVLBuffer` or read the length with `getVLDataLength`
 *  then call `getSlice` to avoid allocation.
 *
 *  Decode the VL header and return a copy of the payload as a `Blob`.
 *
 *  @return A `Blob` containing the VL payload.
 *  @throws std::runtime_error if the buffer is exhausted.
 */
Blob
SerialIter::getVL()
{
    return getRaw(getVLDataLength());
}

/** Decode the VL header and return the payload as a `Buffer`.
 *
 *  Equivalent to `getVL` but returns a `Buffer` (move-only, no SSO overhead)
 *  instead of a `Blob`.  Prefer this over `getVL` for new callers that do not
 *  need `Blob` compatibility.
 *
 *  @return A `Buffer` containing the VL payload.
 *  @throws std::runtime_error if the buffer is exhausted.
 */
Buffer
SerialIter::getVLBuffer()
{
    return getRawHelper<Buffer>(getVLDataLength());
}

}  // namespace xrpl
