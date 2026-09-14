// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <xrpl/basics/hardened_hash.h>
#include <xrpl/basics/partitioned_unordered_map.h> // IWUY: needed for extract<>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/functional/hash.hpp>
#include <boost/predef.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#if BOOST_COMP_MSVC
#include <intrin.h>
#endif

static_assert(
    std::endian::native == std::endian::little || std::endian::native == std::endian::big,
    "mixed-endian targets are not supported");

namespace xrpl {

/** Required forward reference so that we define template-based concepts. */
template <std::size_t Bits, class Tag = void>
    requires(Bits % 32 == 0) && (Bits >= 64)
class BaseUInt;

namespace detail {

template <class T>
inline constexpr bool isBaseUInt = false;

template <std::size_t Bits, class Tag>
inline constexpr bool isBaseUInt<BaseUInt<Bits, Tag>> = true;

/**
 * A contiguous, sized range of one-byte elements that can be copied into a
 * BaseUInt (e.g. `std::string`, `Blob`, `Slice`, `std::span` of bytes,
 * `std::array` or a C array of bytes).
 *
 * Wider element types are deliberately excluded: copying their object
 * representation would make the resulting value depend on the host's byte
 * order, whereas a byte sequence has a single, unambiguous interpretation
 * as the big-endian representation a BaseUInt stores.
 *
 * BaseUInt itself is excluded: the only conversions between BaseUInt
 * specialisations are the explicit tag-attaching and tag-erasing
 * constructors.
 */
template <class R>
concept ByteCopySource =
    std::ranges::contiguous_range<R const> && std::ranges::sized_range<R const> &&
    (sizeof(std::ranges::range_value_t<R const>) == 1) && !isBaseUInt<std::remove_cvref_t<R>>;

/**
 * A ByteCopySource whose length is a compile-time constant equal to N,
 * with an element type that is unambiguously a byte.
 */
template <class R, std::size_t N>
concept FixedByteRange =
    ByteCopySource<R> && (decltype(std::span(std::declval<R const&>()))::extent == N) &&
    (std::same_as<std::ranges::range_value_t<R const>, unsigned char> ||
     std::same_as<std::ranges::range_value_t<R const>, std::byte>);

template <typename T>
concept Limb = std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>;

/**
 * Convert a value between native and big-endian representations.
 *
 * Byte reversal is an involution, so this function performs both the
 * native-to-big and big-to-native conversions.
 */
template <Limb T>
[[nodiscard]] constexpr T
toBigEndian(T v) noexcept
{
    if constexpr (std::endian::native == std::endian::little)
        v = std::byteswap(v);

    return v;
}

/**
 * Parse a hexadecimal string into a big-endian limb array.
 *
 * The input is consumed left-to-right, most significant digit first. Each
 * group of `2 * sizeof(T)` characters forms one limb; limbs are stored in
 * big-endian byte order with limb 0 being the most significant.
 *
 * The exact-length requirement has a single exception: the one-character
 * string "0" yields a zero value. No other short form is accepted.
 *
 * Both upper- and lower-case hexadecimal digits are accepted. No prefix
 * ("0x"), sign, or whitespace is permitted.
 *
 * @tparam T The limb type; must be std::uint32_t or std::uint64_t.
 * @tparam N The number of limbs; must be greater than zero.
 *
 * @param sv The string to parse. Must be exactly `N * 2 * sizeof(T)`
 *           characters long, or the string "0".
 *
 * @return The parsed limb array on success. On failure, an unseated
 *         optional.
 */
template <Limb T, std::size_t N>
    requires(N > 0)
[[nodiscard]] constexpr std::optional<std::array<T, N>>
parseHex(std::string_view sv) noexcept
{
    constexpr std::size_t kHexPerLimb = sizeof(T) * 2;

    std::array<T, N> out{};

    if (sv == "0")
        return out;

    if (sv.size() != N * kHexPerLimb)
        return std::nullopt;

    for (std::size_t i = 0; i != N; ++i)
    {
        auto const first = sv.data() + i * kHexPerLimb;
        auto const last = first + kHexPerLimb;

        T value{};

        if (auto const [ptr, ec] = std::from_chars(first, last, value, 16);
            ec != std::errc{} || ptr != last)
            return std::nullopt;

        out[i] = toBigEndian(value);
    }

    return out;
}

// Add with carry. The return value is the carry-out, when doing chain
// addition.
template <Limb T>
[[nodiscard]] constexpr bool
addCarry(T a, T b, bool carry, T& sum) noexcept
{
#if BOOST_COMP_GNUC || BOOST_COMP_CLANG
    // In GCC and Clang, __builtin_add_overflow can be used at compile
    // time, so we don't need a fallback path.
    bool const c1 = __builtin_add_overflow(a, b, &sum);
    bool const c2 = __builtin_add_overflow(sum, carry, &sum);
    return c1 || c2;
#else
#if BOOST_COMP_MSVC && BOOST_ARCH_X86_64
    if !consteval
    {
        if constexpr (std::same_as<T, std::uint64_t>)
            return _addcarry_u64(carry, a, b, &sum) != 0;
        else
            return _addcarry_u32(carry, a, b, &sum) != 0;
    }
#endif

    // Portable implementation, used under constant evaluation with
    // MSVC/x86-64 and as the fallback path on platforms that we do
    // not have explicit support for.
    T const s = static_cast<T>(a + b);
    bool const c1 = s < a;
    sum = static_cast<T>(s + carry);
    bool const c2 = sum < s;
    return c1 | c2;
#endif
}

template <Limb T, std::size_t N>
constexpr void
add(std::array<T, N>& lhs, std::array<T, N> const& rhs) noexcept
{
    bool carry = false;
    for (std::size_t i = N; i-- > 0;)
    {
        T sum;
        carry = addCarry(toBigEndian(lhs[i]), toBigEndian(rhs[i]), carry, sum);
        lhs[i] = toBigEndian(sum);
    }
}

template <Limb T, std::size_t N>
constexpr void
increment(std::array<T, N>& data) noexcept
{
    for (std::size_t i = N; i-- > 0;)
    {
        T native = toBigEndian(data[i]);
        data[i] = toBigEndian(++native);
        if (native != 0)
            return;
    }
}

template <Limb T, std::size_t N>
constexpr void
decrement(std::array<T, N>& data) noexcept
{
    for (std::size_t i = N; i-- > 0;)
    {
        T const native = toBigEndian(data[i]);
        data[i] = toBigEndian(native - 1);
        if (native != 0)
            return;
    }
}

template <Limb T, std::size_t N>
[[nodiscard]] constexpr std::strong_ordering
compareLimbs(std::array<T, N> const& lhs, std::array<T, N> const& rhs) noexcept
{
    for (std::size_t i = 0; i != N; ++i)
    {
        if (auto const c = toBigEndian(lhs[i]) <=> toBigEndian(rhs[i]); c != 0)
            return c;
    }
    return std::strong_ordering::equal;
}

}  // namespace detail

/**
 * Arbitrarily long unsigned integers.
 *
 * @note This class stores its values internally in big-endian
 *       form and that internal representation is part of the
 *       binary protocol of the XRP Ledger and cannot be changed
 *       arbitrarily without causing breakage.
 *
 * @tparam Bits The number of bits this integer should have; must
 *              be at least 64 and a multiple of 32.
 * @tparam Tag An arbitrary type that functions as a tag and allows
 *             the instantiation of "distinct" types that the same
 *             number of bits.
 */
template <std::size_t Bits, class Tag>
    requires(Bits % 32 == 0) && (Bits >= 64)
class BaseUInt
{
    /** The limb type used to store a BaseUInt of the given width. */
    using LimbType = std::conditional_t<(Bits % 64 == 0), std::uint64_t, std::uint32_t>;

    /** The number of limbs used to store a BaseUInt of the given width. */
    static constexpr std::size_t kLimbCount = Bits / (sizeof(LimbType) * 8);

    // Internal storage: big-endian limbs. data_[0] is the most significant
    // limb. Within each limb, bytes are stored in big-endian order (via
    // toBigEndian at write time).
    std::array<LimbType, kLimbCount> data_;

public:
    /**
     * Value hashing function.
     *  The seed prevents crafted inputs from causing degenerate parent
     * containers.
     */
    using hasher = HardenedHash<>;

    //
    // STL Container Interface
    //

    static constexpr std::size_t kBytes = Bits / 8;
    static_assert(sizeof(data_) == kBytes);

    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = unsigned char;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = value_type const*;
    using const_reference = value_type const&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using tag_type = Tag;

    [[nodiscard]] pointer
    data() noexcept
    {
        return reinterpret_cast<pointer>(data_.data());
    }
    [[nodiscard]] const_pointer
    data() const noexcept
    {
        return reinterpret_cast<const_pointer>(data_.data());
    }

    [[nodiscard]] iterator
    begin() noexcept
    {
        return data();
    }
    [[nodiscard]] iterator
    end() noexcept
    {
        return data() + kBytes;
    }
    [[nodiscard]] const_iterator
    begin() const noexcept
    {
        return data();
    }
    [[nodiscard]] const_iterator
    end() const noexcept
    {
        return data() + kBytes;
    }
    [[nodiscard]] const_iterator
    cbegin() const noexcept
    {
        return data();
    }
    [[nodiscard]] const_iterator
    cend() const noexcept
    {
        return data() + kBytes;
    }

    /**
     * Zero-initialized value.
     */
    constexpr BaseUInt(beast::Zero) noexcept : data_{}
    {
    }

    /**
     * Default construction: the value is zero-initialized.
     *
     * @note In an ideal world, the default constructor would leave the data
     *       uninitialized, but code may depend on the current semantics, so
     *       we cannot change this at this time without extensive auditing.
     */
    constexpr BaseUInt() noexcept : BaseUInt(beast::kZero)
    {
        static_assert(sizeof(BaseUInt) == kBytes);
        static_assert(std::has_unique_object_representations_v<BaseUInt>);
    }

    /**
     * Construct from a byte sequence whose length is fixed at compile time.
     *
     * Accepts any contiguous range of `unsigned char` or `std::byte` whose
     * extent is a compile-time constant equal to `size()`. Byte i of the
     * source becomes byte i of the big-endian representation, so the first
     * byte is the most significant.
     *
     * A source with any other static extent does not compile. A source whose
     * length is only known at run time (`std::string`, `Blob`, `Slice`, a
     * dynamic-extent span) is not accepted here and goes through `fromRaw`,
     * which checks the length and reports failure. Converting such a source to
     * a fixed-extent span at the call site is the caller's assertion that the
     * length is correct; that conversion is not checked.
     *
     * @tparam R A type satisfying `detail::FixedByteRange<R, kBytes>`.
     * @param bytes The bytes to copy.
     */
    template <detail::FixedByteRange<kBytes> R>
    constexpr explicit BaseUInt(R const& bytes) noexcept
    {
        std::array<unsigned char, kBytes> tmp;
        std::ranges::transform(
            bytes, tmp.begin(), [](auto b) { return static_cast<unsigned char>(b); });
        data_ = std::bit_cast<decltype(data_)>(tmp);
    }

    /**
     * Convert to or from the untagged type of the same width.
     *
     * The conversion is a bit-for-bit copy; the value is unchanged. This
     * operation simply attaches or discards a tag.
     *
     * The serialisation layer is the primary user of the erasing direction.
     */
    template <class OtherTag>
        requires(std::is_void_v<OtherTag> != std::is_void_v<Tag>)
    constexpr explicit BaseUInt(BaseUInt<Bits, OtherTag> const& other) noexcept
        : BaseUInt(std::bit_cast<BaseUInt>(other))
    {
    }

    /**
     * Compile-time construction from a hexadecimal string literal.
     *
     *  @param hex Either the string literal "0" or precisely 2 * kBytes
     *             hexadecimal characters.
     */
    explicit consteval BaseUInt(std::string_view hex)
        : data_{[&] {
            if (auto const r = detail::parseHex<LimbType, kLimbCount>(hex))
                return *r;
            throw "invalid hexadecimal literal string";
        }()}
    {
    }

    /**
     * Compile-time construction from a non-negative integer value.
     */
    template <std::integral U>
        requires(sizeof(U) <= sizeof(std::uint64_t) && !std::same_as<U, bool>)
    explicit consteval BaseUInt(U value)
        : data_{[&] {
            if constexpr (std::signed_integral<U>)
            {
                if (value < 0)
                    throw "negative value";
            }

            auto v = static_cast<std::uint64_t>(value);
            std::array<unsigned char, kBytes> bytes{};
            for (std::size_t i = 0; i != sizeof(v); ++i)
            {
                bytes[kBytes - 1 - i] = static_cast<unsigned char>(v);
                v >>= 8;
            }
            return std::bit_cast<decltype(data_)>(bytes);
        }()}
    {
    }

    constexpr BaseUInt(BaseUInt const& b) noexcept = default;
    constexpr BaseUInt&
    operator=(BaseUInt const& b) noexcept = default;

    /**
     * Construct from a byte sequence whose length is only known at run time.
     *
     * @return The value, or an unseated optional if the source is not exactly
     *         `size()` bytes long.
     */
    template <detail::ByteCopySource Container>
    [[nodiscard]] static constexpr std::optional<BaseUInt>
    fromRaw(Container const& c) noexcept
    {
        if (std::ranges::size(c) != size())
            return std::nullopt;

        std::array<unsigned char, kBytes> bytes;
        std::ranges::transform(
            c, bytes.begin(), [](auto b) { return static_cast<unsigned char>(b); });
        return BaseUInt{bytes};
    }

    [[nodiscard]] constexpr int
    signum() const noexcept
    {
        return std::ranges::any_of(data_, [](auto v) { return v != 0; }) ? 1 : 0;
    }

    constexpr bool
    operator!() const noexcept
    {
        return signum() == 0;
    }

    [[nodiscard]] constexpr BaseUInt
    operator~() const noexcept
    {
        BaseUInt ret;
        std::ranges::transform(data_, ret.data_.begin(), std::bit_not{});
        return ret;
    }

    constexpr BaseUInt&
    operator=(beast::Zero) noexcept
    {
        data_.fill(0);
        return *this;
    }

    constexpr BaseUInt&
    operator^=(BaseUInt const& b) noexcept
    {
        std::ranges::transform(data_, b.data_, data_.begin(), std::bit_xor{});
        return *this;
    }

    constexpr BaseUInt&
    operator&=(BaseUInt const& b) noexcept
    {
        std::ranges::transform(data_, b.data_, data_.begin(), std::bit_and{});
        return *this;
    }

    constexpr BaseUInt&
    operator|=(BaseUInt const& b) noexcept
    {
        std::ranges::transform(data_, b.data_, data_.begin(), std::bit_or{});
        return *this;
    }

    constexpr BaseUInt&
    operator+=(BaseUInt const& b) noexcept
    {
        detail::add(data_, b.data_);
        return *this;
    }

    constexpr BaseUInt&
    operator++() noexcept
    {
        detail::increment(data_);
        return *this;
    }

    constexpr BaseUInt
    operator++(int) noexcept
    {
        BaseUInt ret = *this;
        ++(*this);

        return ret;
    }

    constexpr BaseUInt&
    operator--() noexcept
    {
        detail::decrement(data_);
        return *this;
    }

    constexpr BaseUInt
    operator--(int) noexcept
    {
        BaseUInt ret = *this;
        --(*this);

        return ret;
    }

    [[nodiscard]] constexpr BaseUInt
    next() const noexcept
    {
        auto ret = *this;
        return ++ret;
    }

    [[nodiscard]] constexpr BaseUInt
    prev() const noexcept
    {
        auto ret = *this;
        return --ret;
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, BaseUInt const& a) noexcept
    {
        // Do not allow any endian transformations on this memory
        h(a.data_.data(), sizeof(a.data_));
    }

    /**
     * Parse a hex string into a base_uint
     *
     * The input must be precisely `2 * bytes` hexadecimal characters
     * long, with one exception: the value '0'.
     *
     * @param sv The hexadecimal characters composing the string
     * @return true if the input was parsed properly; false otherwise.
     */
    [[nodiscard]] constexpr bool
    parseHex(std::string_view sv) noexcept
    {
        auto const r = detail::parseHex<LimbType, kLimbCount>(sv);

        if (r)
            data_ = *r;

        return r.has_value();
    }

    [[nodiscard]] static constexpr std::size_t
    size() noexcept
    {
        return kBytes;
    }

    // Deprecated.
    [[nodiscard]] constexpr bool
    isZero() const noexcept
    {
        return *this == beast::kZero;
    }

    [[nodiscard]] constexpr bool
    isNonZero() const noexcept
    {
        return !isZero();
    }

    constexpr void
    zero() noexcept
    {
        *this = beast::kZero;
    }

    // Comparison via hidden friends
    friend constexpr std::strong_ordering
    operator<=>(BaseUInt const& lhs, BaseUInt const& rhs) noexcept
    {
        return detail::compareLimbs(lhs.data_, rhs.data_);
    }

    friend constexpr bool
    operator==(BaseUInt const& lhs, BaseUInt const& rhs) noexcept
    {
        return lhs.data_ == rhs.data_;
    }
};

/**
 * Specific instantiations, commonly used in the codebase.
 */
using uint128 = BaseUInt<128>;
using uint160 = BaseUInt<160>;
using uint192 = BaseUInt<192>;
using uint256 = BaseUInt<256>;
using uint512 = BaseUInt<512>;

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
[[nodiscard]] constexpr BaseUInt<Bits, Tag>
operator^(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b) noexcept
{
    return BaseUInt<Bits, Tag>(a) ^= b;
}

template <std::size_t Bits, class Tag>
[[nodiscard]] constexpr BaseUInt<Bits, Tag>
operator&(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b) noexcept
{
    return BaseUInt<Bits, Tag>(a) &= b;
}

template <std::size_t Bits, class Tag>
[[nodiscard]] constexpr BaseUInt<Bits, Tag>
operator|(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b) noexcept
{
    return BaseUInt<Bits, Tag>(a) |= b;
}

template <std::size_t Bits, class Tag>
[[nodiscard]] constexpr BaseUInt<Bits, Tag>
operator+(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b) noexcept
{
    return BaseUInt<Bits, Tag>(a) += b;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
[[nodiscard]] inline std::string
to_string(BaseUInt<Bits, Tag> const& a)
{
    return strHex(a.cbegin(), a.cend());
}

template <std::size_t Bits, class Tag>
[[nodiscard]] inline std::string
toShortString(BaseUInt<Bits, Tag> const& a)
{
    static_assert(BaseUInt<Bits, Tag>::kBytes > 4, "For 4 bytes or less, use a native type");
    return strHex(a.cbegin(), a.cbegin() + 4) + "...";
}

template <std::size_t Bits, class Tag>
inline std::ostream&
operator<<(std::ostream& out, BaseUInt<Bits, Tag> const& u)
{
    return out << to_string(u);
}

/** Bucket selection for partitioned maps
 *
 * @note this is NOT a cryptographic hash and is only useful if the
 *       values being passed in are uniformly distributed.
 *
 * @param key The key value
 * @return the bucket
 */
template <>
inline std::size_t
extract(uint256 const& key)
{
    std::size_t result = 0;
    // Use memcpy to avoid unaligned UB
    // (will optimize to equivalent code)
    std::memcpy(&result, key.data(), sizeof(std::size_t));
    return result;
}

template <class T>
concept BaseUIntType = detail::isBaseUInt<std::remove_cvref_t<T>>;

}  // namespace xrpl

namespace beast {
template <std::size_t Bits, class Tag>
struct IsUniquelyRepresented<xrpl::BaseUInt<Bits, Tag>> : public std::true_type
{
    explicit IsUniquelyRepresented() = default;
};

}  // namespace beast
