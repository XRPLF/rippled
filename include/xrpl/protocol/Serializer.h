#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SField.h>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

namespace detail {

/**
 * @name XRPL variable-length (VL) encoding
 *
 *  The length of a variable-length field is encoded in one to three
 *  bytes; the value of the first ("lead") byte determines how many
 *  additional bytes follow, and the encoded length is:
 *
 *      lead in [  0, 192]: 1 byte;  length = lead
 *      lead in [193, 240]: 2 bytes; length = 193 +
 *                                     ((lead - 193) << 8) + b2
 *      lead in [241, 254]: 3 bytes; length = 12481 +
 *                                     ((lead - 241) << 16) +
 *                                     (b2 << 8) + b3
 *      lead == 255:        reserved
 *
 *  @note A 3-byte header can represent lengths up to 929,984 bytes, but
 *        the encoder has never written anything longer than 918,744.
 *        bytes. The decoder applies the same limit, so every VL field
 *        that can be read can also be written back byte-for-byte.
 *        The representable maximum is derived from the header format and
 *        kept distinct from the chosen limit.
 */
/** @{ */

/** The smallest lead byte of a 2-byte header. */
inline constexpr std::size_t kVlMinLead2 = 193;

/** The smallest lead byte of a 3-byte header. */
inline constexpr std::size_t kVlMinLead3 = 241;

/** The one lead byte that starts no header. */
inline constexpr std::size_t kVlReservedLead = 255;

/** The longest field length a 1-byte header encodes. */
inline constexpr std::size_t kVlMaxLength1 = kVlMinLead2 - 1;

/** The shortest field length a 2-byte header encodes. */
inline constexpr std::size_t kVlMinLength2 = kVlMaxLength1 + 1;

/** The longest field length a 2-byte header encodes. */
inline constexpr std::size_t kVlMaxLength2 = kVlMaxLength1 + ((kVlMinLead3 - kVlMinLead2) << 8);

/** The shortest field length a 3-byte header encodes. */
inline constexpr std::size_t kVlMinLength3 = kVlMaxLength2 + 1;

/** The longest field length a 3-byte header can represent. */
inline constexpr std::size_t kVlMaxRepresentableLength =
    kVlMaxLength2 + ((kVlReservedLead - kVlMinLead3) << 16);

/**
 * The largest field length the encoder writes and the decoder accepts.
 *
 *  This is a chosen limit, smaller than kVlMaxRepresentableLength; see
 *  the note in the group description.
 */
inline constexpr std::size_t kVlMaxLength = 918744;

/** @} */

static_assert(kVlMaxLength >= kVlMinLength3 && kVlMaxLength <= kVlMaxRepresentableLength);

/**
 * An integer type whose serialized representation is unambiguous.
 *
 *  This intentionally excludes bool and every character type other
 *  than unsigned char (and, by extension, std::uint8_t).
 *  A single byte is serialized only as an unsigned char.
 */
template <class T>
concept SerializableIntegerCandidate = std::integral<T> && !std::same_as<T, bool> &&
    !std::same_as<T, char> && !std::same_as<T, signed char> && !std::same_as<T, wchar_t> &&
    !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

/**
 * A type that holds exactly one byte and has no sign to misinterpret.
 */
template <class T>
concept SerializableByteCandidate =
    std::same_as<T, std::uint8_t> || std::same_as<T, unsigned char> || std::same_as<T, std::byte>;

}  // namespace detail

static_assert(
    std::endian::native == std::endian::little || std::endian::native == std::endian::big);

/**
 * An append-only buffer for building data in the canonical XRPL binary encoding.
 *
 *  The serializer owns its buffer, which grows as needed. The data
 *  buffer is append-only and existing data cannot be modified.
 *
 *  The integer add functions do not convert their arguments: the
 *  type of an argument determines how many bytes are written, so
 *  passing any other type fails to compile.
 *
 *  Views into the buffer are invalidated by any operation that
 *  modifies the serializer.
 *
 *  @par Error handling
 *  An add function that throws leaves the serializer unchanged and
 *  usable.
 */
class Serializer
{
    Blob data_;

    /**
     * The spare capacity reserved whenever the buffer is allocated or grown.
     */
    static constexpr std::size_t kReservation = 256;

    /**
     * Append one or more runs of bytes to the buffer in a single step.
     *
     *  The parts are appended in argument order, with nothing between
     *  them. Capacity is reserved, if necessary, before any data gets
     *  appended.
     *
     *  @tparam Parts Each must be exactly Slice; no conversion is applied.
     *  @param parts The bytes to append. Individual parts may be empty.
     *
     *  @pre No part may refer to the serializer's own storage, including
     *       its spare capacity. This is asserted here, but callers which
     *       take user-provided data should verify this before calling.
     *
     *  @throws std::bad_alloc or std::length_error if the buffer cannot
     *          grow. The serializer is left unchanged.
     *
     *  @note The buffer grows by the combined size of the parts; any
     *        slices referencing the buffer may be invalidated.
     */
    template <std::same_as<Slice>... Parts>
        requires(sizeof...(Parts) > 0)
    void
    write(Parts... parts)
    {
        XRPL_ASSERT(
            (!overlap(parts, Slice{data_.data(), data_.capacity()}) && ...),
            "xrpl::Serializer::write : parts do not alias the buffer");

        if (auto const need = data_.size() + (parts.size() + ...); need > data_.capacity())
            data_.reserve(std::max(need + kReservation, 2 * data_.capacity()));

        (data_.insert(data_.end(), parts.begin(), parts.end()), ...);
    }

    /**
     * Append an integer to the buffer in network byte order.
     *
     *  Exactly sizeof(T) bytes are written, most significant byte first.
     *  Signed values are written in their two's complement representation.
     */
    template <detail::SerializableIntegerCandidate T>
    void
    addInteger(T v)
    {
        if (std::endian::native != std::endian::big)
            v = std::byteswap(v);

        write(makeSlice(std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(v)));
    }

public:
    explicit Serializer(std::size_t n = kReservation)
    {
        data_.reserve(n);
    }

    /**
     * Returns an immutable view of the bytes written to the serializer.
     *
     *  @note The returned slice is valid only until the serializer is next
     *        modified or destroyed.
     */
    [[nodiscard]] Slice
    slice() const noexcept
    {
        return Slice{data_.data(), data_.size()};
    }

    /**
     * The number of bytes written to the serializer.
     */
    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return data_.size();
    }

    /**
     * A pointer to the first byte written to the serializer.
     *
     *  This exists for interfaces that take a pointer and a size, such as
     *  the setters for protobuf `bytes` fields. Prefer slice() elsewhere.
     *
     *  @note The pointer has the same lifetime as the result of slice(),
     *        and may be null if the serializer is empty.
     */
    [[nodiscard]] void const*
    data() const noexcept
    {
        return data_.data();
    }

    /**
     * Transfers ownership of the bytes written so far to the caller.
     *
     *  The serializer is left empty and can be reused, but it no longer
     *  holds any reserved capacity, so writing to it again allocates.
     *
     *  Use this in preference to copying out of slice() when the
     *  serializer is not needed afterwards.
     *
     *  @return The buffer holding every byte written since construction,
     *          or since the last call to erase() or takeData().
     *
     *  @note Any slices referencing the serializer buffer should be
     *        considered invalid.
     */
    [[nodiscard]] Blob
    takeData() noexcept
    {
        return std::exchange(data_, Blob{});
    }

    /**
     * Discards all the bytes written to the serializer.
     *
     *  The serializer is left empty and can be reused. The storage it has
     *  already allocated is retained, so reusing it for output of a similar
     *  size does not allocate.
     *
     *  @note Any slices referencing the serializer buffer are invalidated.
     */
    void
    erase() noexcept
    {
        data_.clear();
    }

    /**
     * Append a single byte to the buffer.
     *
     *  The argument must be an `unsigned char`, a `std::uint8_t` or a
     *  `std::byte`. No conversion is applied: an argument of any other
     *  type fails to compile, including `char`, `signed char`, `bool`,
     *  enumerations, and any untyped integer literal.
     *
     *  @note The buffer grows by one byte; any previously acquired slices
     *        referencing the serializer's buffer may be invalidated.
     */
    template <detail::SerializableByteCandidate T>
    void
    add8(T i)
    {
        data_.push_back(static_cast<std::uint8_t>(i));
    }

    /** @{ */
    /**
     * Append explicitly-sized integers to the buffer.
     *
     *  The argument must be exactly as wide as the function's name states.
     *  Narrower and wider arguments are both rejected, and no conversion is
     *  applied. Signed and unsigned arguments of that width are accepted.
     *
     *  @note The buffer grows by sizeof(T) bytes; any slices referencing
     *        the buffer may be invalidated.
     */
    template <detail::SerializableIntegerCandidate T>
        requires(sizeof(T) == 2)
    void
    add16(T i)
    {
        addInteger<T>(i);
    }

    template <detail::SerializableIntegerCandidate T>
        requires(sizeof(T) == 4)
    void
    add32(T i)
    {
        addInteger<T>(i);
    }

    void
    add32(HashPrefix p)
    {
        addInteger<std::uint32_t>(safeCast<std::uint32_t>(p));
    }

    template <detail::SerializableIntegerCandidate T>
        requires(sizeof(T) == 8)
    void
    add64(T i)
    {
        addInteger<T>(i);
    }
    /** @} */

    template <BaseUIntType T>
    void
    addBitString(T const& v)
    {
        addRaw(Slice{v.data(), v.size()});
    }

    /**
     * Append raw bytes to the buffer.
     *
     *  The bytes are copied verbatim, with no length prefix and no other
     *  framing.
     *
     *  @param slice The bytes to append. May be empty, in which case the
     *               buffer is left unchanged.
     *
     *  @throws std::runtime_error if slice refers to the serializer's own
     *          storage, including its spare capacity. The serializer is
     *          left unchanged.
     *
     *  @note The buffer grows by slice.size() bytes; any slices referencing
     *        the buffer may be invalidated.
     */
    void
    addRaw(Slice slice)
    {
        if (overlap(slice, Slice{data_.data(), data_.capacity()})) [[unlikely]]
            Throw<std::runtime_error>("input slice points inside our storage");

        write(slice);
    }

    /**
     * Append a variable-length (VL) field to the buffer.
     *
     *  Writes a length header of one to three bytes, followed by the bytes
     *  of the field. The header always uses the shortest form that can
     *  represent the length; see detail::kVlMaxLength for the format.
     *
     *  @param slice The contents of the field. May be empty, in which case
     *               only a one byte header is written.
     *
     *  @throws std::runtime_error if slice refers to the serializer's own
     *          storage, including its spare capacity, or if it is longer
     *          than the maximum allowed length. In both cases the serializer
     *          is left unchanged.
     *
     *  @note The buffer grows by slice.size() bytes plus the size of the
     *        header; any slices referencing the buffer may be invalidated.
     */
    void
    addVL(Slice slice)
    {
        if (overlap(slice, Slice{data_.data(), data_.capacity()})) [[unlikely]]
            Throw<std::runtime_error>("input slice points inside our storage");

        auto length = slice.size();

        if (length > detail::kVlMaxLength) [[unlikely]]
            Throw<std::runtime_error>("Excessive length for VL field");

        std::array<std::uint8_t, 3> hdr{};
        std::size_t n = 0;

        if (length <= detail::kVlMaxLength1)
        {
            hdr[n++] = static_cast<std::uint8_t>(length);
        }
        else if (length <= detail::kVlMaxLength2)
        {
            length -= detail::kVlMinLength2;
            hdr[n++] = static_cast<std::uint8_t>(detail::kVlMinLead2 + (length >> 8));
            hdr[n++] = static_cast<std::uint8_t>(length & 0xff);
        }
        else
        {
            length -= detail::kVlMinLength3;
            hdr[n++] = static_cast<std::uint8_t>(detail::kVlMinLead3 + (length >> 16));
            hdr[n++] = static_cast<std::uint8_t>((length >> 8) & 0xff);
            hdr[n++] = static_cast<std::uint8_t>(length & 0xff);
        }

        write(Slice{hdr.data(), n}, slice);
    }

    void
    addFieldID(int type, int name)
    {
        XRPL_ASSERT(
            (type > 0) && (type < 256) && (name > 0) && (name < 256),
            "xrpl::Serializer::addFieldID : inputs inside range");

        std::array<std::uint8_t, 3> id{};
        std::size_t n = 0;

        if (type < 16)
        {
            if (name < 16)
            {
                // common type, common name
                id[n++] = static_cast<std::uint8_t>((type << 4) | name);
            }
            else
            {
                // common type, uncommon name
                id[n++] = static_cast<std::uint8_t>(type << 4);
                id[n++] = static_cast<std::uint8_t>(name);
            }
        }
        else if (name < 16)
        {
            // uncommon type, common name
            id[n++] = static_cast<std::uint8_t>(name);
            id[n++] = static_cast<std::uint8_t>(type);
        }
        else
        {
            // uncommon type, uncommon name
            id[n++] = static_cast<std::uint8_t>(0);
            id[n++] = static_cast<std::uint8_t>(type);
            id[n++] = static_cast<std::uint8_t>(name);
        }

        write(Slice{id.data(), n});
    }

    void
    addFieldID(SerializedTypeID type, int name)
    {
        addFieldID(safeCast<int>(type), name);
    }

    /**
     * Hidden friend to hash the buffer contents.
     *
     *  This is byte-for-byte equivalent to `hash_append(h, s.slice());`
     *  so `sha512Half(s) == sha512Half(s.slice())`.
     */
    template <class Hasher>
    friend void
    hash_append(Hasher& h, Serializer const& s) noexcept
    {
        h(s.data_.data(), s.data_.size());
    }
};

/**
 * An integer type that a Serializer can append with add8, add16, add32 or add64.
 */
template <class T>
concept SerializableInteger = std::integral<T> &&
    (requires(Serializer& s, T v) { s.add8(v); } || requires(Serializer& s, T v) { s.add16(v); } ||
     requires(Serializer& s, T v) { s.add32(v); } || requires(Serializer& s, T v) { s.add64(v); });

/**
 * A forward-only reader over a buffer in the canonical XRPL binary encoding.
 *
 *  A reader may be constructed over an empty slice, but if the slice
 *  is non-empty, its data pointer must point to accessible memory.
 *
 *  The reader does not own the buffer it reads from. The buffer must
 *  outlive both the reader and every slice the reader returns, since
 *  they refer to the buffer directly and are not copies.
 *
 *  Each read consumes bytes from the front of the unread part of the
 *  buffer, advancing the read pointer past them. No mechanism allows
 *  the read pointer to be moved back. To retain a position, copy the
 *  reader: a copy is cheap and is independent of the original.
 *
 *  @par Error handling
 *  Read operations will throw std::runtime_error if the buffer holds
 *  fewer bytes than the read requires, or if the bytes do not form a
 *  valid encoding for the value being read. Callers should not use a
 *  reader after an exception is thrown.
 */
class SerialIter
{
    std::uint8_t const* p_;
    std::size_t remain_;

    /**
     * Consume the next bytes of the buffer.
     *
     *  Checks that at least n bytes remain, then advances the read position
     *  past them. Every read goes through this function, so it is the only
     *  place where the bounds check and the position update happen.
     *
     *  @param n The number of bytes to consume, which may be zero.
     *  @return A pointer to the first of the consumed bytes. It refers to
     *          the underlying buffer and is valid only as long as that
     *          buffer is.
     *
     *  @throws std::runtime_error if fewer than n bytes remain. The read
     *          position is left unchanged.
     */
    [[nodiscard]] std::uint8_t const*
    consume(std::size_t n)
    {
        if (n > remain_) [[unlikely]]
            Throw<std::runtime_error>("short buffer");

        auto const ret = p_;
        p_ += n;
        remain_ -= n;

        return ret;
    }

    /**
     * Consume a fixed number of bytes.
     *
     *  @tparam N The number of bytes to consume.
     *  @return A span over exactly N bytes of the underlying buffer.
     *
     *  @throws std::runtime_error if fewer than N bytes remain.
     */
    template <std::size_t N>
        requires(N > 0)
    [[nodiscard]] std::span<std::uint8_t const, N>
    consume()
    {
        return std::span<std::uint8_t const, N>{consume(N), N};
    }

    /**
     * Read an integer from the buffer, decoded from big-endian.
     *
     *  The integer is deserialized by reading exactly sizeof(T) bytes from
     *  the wire and interpreting them in network byte order, regardless of
     *  the endianness of the host platform.
     *
     *  @tparam T An integral type.
     *  @return The decoded value.
     *
     *  @throws std::runtime_error if fewer than sizeof(T) bytes remain.
     */
    template <detail::SerializableIntegerCandidate T>
        requires(sizeof(T) <= 8)
    T
    readInteger()
    {
        std::array<std::uint8_t, sizeof(T)> raw{};
        std::ranges::copy(consume<raw.size()>(), raw.begin());

        auto v = std::bit_cast<T>(raw);

        if (std::endian::native != std::endian::big)
            v = std::byteswap(v);

        return v;
    }

public:
    explicit SerialIter(Slice slice) : p_(slice.data()), remain_(slice.size())
    {
        if (p_ == nullptr && remain_ != 0) [[unlikely]]
            Throw<std::runtime_error>("null buffer with non-zero size");
    }

    /**
     * The number of bytes that have not yet been deserialized.
     */
    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return remain_;
    }

    /**
     * Whether every byte of the buffer has been deserialized.
     */
    [[nodiscard]] bool
    empty() const noexcept
    {
        return remain_ == 0;
    }

    /**
     * The bytes not yet consumed.
     */
    [[nodiscard]] Slice
    slice() const noexcept
    {
        return {p_, remain_};
    }

    [[nodiscard]] std::uint8_t
    get8()
    {
        return *consume(1);
    }

    [[nodiscard]] std::int16_t
    geti16()
    {
        return readInteger<std::int16_t>();
    }

    [[nodiscard]] std::uint16_t
    get16()
    {
        return readInteger<std::uint16_t>();
    }

    [[nodiscard]] std::uint32_t
    get32()
    {
        return readInteger<std::uint32_t>();
    }

    [[nodiscard]] std::int32_t
    geti32()
    {
        return readInteger<std::int32_t>();
    }

    [[nodiscard]] std::uint64_t
    get64()
    {
        return readInteger<std::uint64_t>();
    }

    [[nodiscard]] std::int64_t
    geti64()
    {
        return readInteger<std::int64_t>();
    }

    /**
     * Read a fixed-width BaseUInt encoded integer.
     *
     *  @tparam T The BaseUInt instantiation to read, e.g. uint256 or
     *            AccountID. Exactly T::kBytes bytes are consumed.
     *
     *  @throws std::runtime_error if fewer than T::kBytes bytes remain.
     */
    template <BaseUIntType T>
    [[nodiscard]] T
    getBitString()
    {
        return T{consume<T::kBytes>()};
    }

    /** @{ */
    /**
     * Shorthands for the untagged BaseUInt widths.
     */
    [[nodiscard]] UInt128
    get128()
    {
        return getBitString<UInt128>();
    }

    [[nodiscard]] UInt160
    get160()
    {
        return getBitString<UInt160>();
    }

    [[nodiscard]] UInt192
    get192()
    {
        return getBitString<UInt192>();
    }

    [[nodiscard]] UInt256
    get256()
    {
        return getBitString<UInt256>();
    }
    /** @} */

    void
    getFieldID(int& type, int& name)
    {
        int const lead = get8();

        int t = lead >> 4;
        int n = lead & 15;

        bool const uncommonType = (t == 0);
        bool const uncommonName = (n == 0);

        if (uncommonType)
            t = get8();

        if (uncommonName)
            n = get8();

        if (uncommonType && t < 16)
            Throw<std::runtime_error>("gFID: uncommon type out of range " + std::to_string(t));

        if (uncommonName && n < 16)
            Throw<std::runtime_error>("gFID: uncommon name out of range " + std::to_string(n));

        type = t;
        name = n;
    }

    /**
     * Read a VL-encoded field.
     *
     *  @return A slice over the field data, which refers to the underlying
     *          buffer and is valid only as long as that buffer is.
     */
    [[nodiscard]] Slice
    getVL()
    {
        auto const bytes = [this]() -> std::size_t {
            std::size_t const b1 = get8();

            if (b1 < detail::kVlMinLead2)
                return b1;

            if (b1 < detail::kVlMinLead3)
            {
                std::size_t const b2 = get8();

                return detail::kVlMinLength2 + ((b1 - detail::kVlMinLead2) << 8) + b2;
            }

            if (b1 < detail::kVlReservedLead)
            {
                std::size_t const b2 = get8();
                std::size_t const b3 = get8();

                auto const len =
                    detail::kVlMinLength3 + ((b1 - detail::kVlMinLead3) << 16) + (b2 << 8) + b3;

                if (len > detail::kVlMaxLength) [[unlikely]]
                    Throw<std::runtime_error>("VL field length exceeds the encoder limit");

                return len;
            }

            Throw<std::runtime_error>("reserved VL lead byte");
        }();

        return Slice{consume(bytes), bytes};
    }
};

}  // namespace xrpl
