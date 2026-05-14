// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/basics/partitioned_unordered_map.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/endian/conversion.hpp>
#include <boost/functional/hash.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

namespace xrpl {

namespace detail {

template <class Container, class = std::void_t<>>
struct IsContiguousContainer : std::false_type
{
};

template <class Container>
struct IsContiguousContainer<
    Container,
    std::void_t<
        decltype(std::declval<Container const>().size()),
        decltype(std::declval<Container const>().data()),
        typename Container::value_type>> : std::true_type
{
};

template <>
struct IsContiguousContainer<Slice> : std::true_type
{
};

template <typename...>
struct AlwaysFalseT : std::bool_constant<false>
{
};

}  // namespace detail

/** Integers of any length that is a multiple of 32-bits

    @note This class stores its values internally in big-endian
          form and that internal representation is part of the
          binary protocol of the XRP Ledger and cannot be changed
          arbitrarily without causing breakage.

          @tparam Bits The number of bits this integer should have; must
                       be at least 64 and a multiple of 32.
          @tparam Tag An arbitrary type that functions as a tag and allows
                      the instantiation of "distinct" types that the same
                      number of bits.
 */
template <std::size_t Bits, class Tag = void>
class BaseUInt
{
    static_assert((Bits % 32) == 0, "The length of a base_uint in bits must be a multiple of 32.");

    static_assert(Bits >= 64, "The length of a base_uint in bits must be at least 64.");

    static constexpr std::size_t kWIDTH = Bits / 32;

    // This is really big-endian in byte order.
    // We sometimes use std::uint32_t for speed.

    std::array<std::uint32_t, kWIDTH> data_;

public:
    //--------------------------------------------------------------------------
    //
    // STL Container Interface
    //

    static std::size_t constexpr kBYTES = Bits / 8;
    static_assert(sizeof(data_) == kBYTES, "");

    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = unsigned char;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = value_type const*;
    using const_reference = value_type const&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using tag_type = Tag;

    pointer
    data()
    {
        return reinterpret_cast<pointer>(data_.data());
    }
    [[nodiscard]] const_pointer
    data() const
    {
        return reinterpret_cast<const_pointer>(data_.data());
    }

    iterator
    begin()
    {
        return data();
    }
    iterator
    end()
    {
        return data() + kBYTES;
    }
    [[nodiscard]] const_iterator
    begin() const
    {
        return data();
    }
    [[nodiscard]] const_iterator
    end() const
    {
        return data() + kBYTES;
    }
    [[nodiscard]] const_iterator
    cbegin() const
    {
        return data();
    }
    [[nodiscard]] const_iterator
    cend() const
    {
        return data() + kBYTES;
    }

    /** Value hashing function.
        The seed prevents crafted inputs from causing degenerate parent
       containers.
    */
    using hasher = HardenedHash<>;

    //--------------------------------------------------------------------------

private:
    /** Construct from a raw pointer.
        The buffer pointed to by `data` must be at least Bits/8 bytes.

        @note the structure is used to disambiguate this from the std::uint64_t
              constructor: something like base_uint(0) is ambiguous.
    */
    // NIKB TODO Remove the need for this constructor.
    struct VoidHelper
    {
        explicit VoidHelper() = default;
    };

    explicit BaseUInt(void const* data, VoidHelper)
    {
        memcpy(data_.data(), data, kBYTES);
    }

    // Helper function to initialize a base_uint from a std::string_view.
    enum class ParseResult {
        Okay,
        BadLength,
        BadChar,
    };

    constexpr Expected<decltype(data_), ParseResult>
    parseFromStringView(std::string_view sv) noexcept
    {
        // Local lambda that converts a single hex char to four bits and
        // ORs those bits into a uint32_t.
        auto hexCharToUInt = [](char c, std::uint32_t shift, std::uint32_t& accum) -> ParseResult {
            std::uint32_t nibble = 0xFFu;
            if (c < '0' || c > 'f')
                return ParseResult::BadChar;

            if (c >= 'a')
            {
                nibble = static_cast<std::uint32_t>(c - 'a' + 0xA);
            }
            else if (c >= 'A')
            {
                nibble = static_cast<std::uint32_t>(c - 'A' + 0xA);
            }
            else if (c <= '9')
            {
                nibble = static_cast<std::uint32_t>(c - '0');
            }

            if (nibble > 0xFu)
                return ParseResult::BadChar;

            accum |= (nibble << shift);

            return ParseResult::Okay;
        };

        decltype(data_) ret{};

        if (sv == "0")
        {
            return ret;
        }

        if (sv.size() != size() * 2)
            return Unexpected(ParseResult::BadLength);

        std::size_t i = 0u;
        auto in = sv.begin();
        while (in != sv.end())
        {
            std::uint32_t accum = {};
            for (std::uint32_t const shift : {4u, 0u, 12u, 8u, 20u, 16u, 28u, 24u})
            {
                if (auto const result = hexCharToUInt(*in++, shift, accum);
                    result != ParseResult::Okay)
                    return Unexpected(result);
            }
            ret[i++] = accum;
        }
        return ret;
    }

    constexpr decltype(data_)
    parseFromStringViewThrows(std::string_view sv) noexcept(false)
    {
        auto const result = parseFromStringView(sv);
        if (!result)
        {
            if (result.error() == ParseResult::BadLength)
                Throw<std::invalid_argument>("invalid length for hex string");

            Throw<std::range_error>("invalid hex character");
        }
        return *result;
    }

public:
    constexpr BaseUInt() : data_{}
    {
    }

    constexpr BaseUInt(beast::Zero) : data_{}
    {
    }

    explicit BaseUInt(std::uint64_t b)
    {
        *this = b;
    }

    // This constructor is intended to be used at compile time since it might
    // throw at runtime.  Consider declaring this constructor consteval once
    // we get to C++23.
    explicit constexpr BaseUInt(std::string_view sv) noexcept(false)
        : data_(parseFromStringViewThrows(sv))
    {
    }

    template <
        class Container,
        class = std::enable_if_t<
            detail::IsContiguousContainer<Container>::value &&
            std::is_trivially_copyable_v<typename Container::value_type>>>
    explicit BaseUInt(Container const& c)
    {
        // Use AlwaysFalseT so the static_assert condition is dependent
        // and only triggers when this constructor template is instantiated.
        static_assert(
            detail::AlwaysFalseT<Container>::value,
            "This constructor is not intended to be used and will be soon removed. "
            "Use base_uint::fromRaw instead.");
    }

    template <
        class Container,
        class = std::enable_if_t<
            detail::IsContiguousContainer<Container>::value &&
            std::is_trivially_copyable_v<typename Container::value_type>>>
    static BaseUInt
    fromRaw(Container const& c)
    {
        BaseUInt result;
        XRPL_ASSERT(
            c.size() * sizeof(typename Container::value_type) == size(),
            "xrpl::BaseUInt::fromRaw(Container auto) : input size match");
        std::memcpy(result.data_.data(), c.data(), size());
        return result;
    }

    template <class Container>
    std::enable_if_t<
        detail::IsContiguousContainer<Container>::value &&
            std::is_trivially_copyable_v<typename Container::value_type>,
        BaseUInt&>
    operator=(Container const& c)
    {
        XRPL_ASSERT(
            c.size() * sizeof(typename Container::value_type) == size(),
            "xrpl::BaseUInt::operator=(Container auto) : input size match");
        std::memcpy(data_.data(), c.data(), size());
        return *this;
    }

    /* Construct from a raw pointer.
        The buffer pointed to by `data` must be at least Bits/8 bytes.
    */
    static BaseUInt
    fromVoid(void const* data)
    {
        return BaseUInt(data, VoidHelper());
    }

    template <class T>
    static std::optional<BaseUInt>
    fromVoidChecked(T const& from)
    {
        if (from.size() != size())
            return {};
        return fromVoid(from.data());
    }

    [[nodiscard]] constexpr int
    signum() const
    {
        for (int i = 0; i < kWIDTH; i++)
        {
            if (data_[i] != 0)
                return 1;
        }

        return 0;
    }

    bool
    operator!() const
    {
        return *this == beast::kZERO;
    }

    constexpr BaseUInt
    operator~() const
    {
        BaseUInt ret;

        for (int i = 0; i < kWIDTH; i++)
            ret.data_[i] = ~data_[i];

        return ret;
    }

    BaseUInt&
    operator=(std::uint64_t uHost)
    {
        *this = beast::kZERO;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
        union
        {
            unsigned u[2];
            std::uint64_t ul;
        };
        // NOLINTEND(cppcoreguidelines-pro-type-member-init)
        // Put in least significant bits.
        ul = boost::endian::native_to_big(uHost);
        data_[kWIDTH - 2] = u[0];
        data_[kWIDTH - 1] = u[1];
        return *this;
    }

    BaseUInt&
    operator^=(BaseUInt const& b)
    {
        for (int i = 0; i < kWIDTH; i++)
            data_[i] ^= b.data_[i];

        return *this;
    }

    BaseUInt&
    operator&=(BaseUInt const& b)
    {
        for (int i = 0; i < kWIDTH; i++)
            data_[i] &= b.data_[i];

        return *this;
    }

    BaseUInt&
    operator|=(BaseUInt const& b)
    {
        for (int i = 0; i < kWIDTH; i++)
            data_[i] |= b.data_[i];

        return *this;
    }

    BaseUInt&
    operator++()
    {
        // prefix operator
        for (int i = kWIDTH - 1; i >= 0; --i)
        {
            data_[i] = boost::endian::native_to_big(boost::endian::big_to_native(data_[i]) + 1);
            if (data_[i] != 0)
                break;
        }

        return *this;
    }

    BaseUInt
    operator++(int)
    {
        // postfix operator
        BaseUInt const ret = *this;
        ++(*this);

        return ret;
    }

    BaseUInt&
    operator--()
    {
        for (int i = kWIDTH - 1; i >= 0; --i)
        {
            auto prev = data_[i];
            data_[i] = boost::endian::native_to_big(boost::endian::big_to_native(data_[i]) - 1);

            if (prev != 0)
                break;
        }

        return *this;
    }

    BaseUInt
    operator--(int)
    {
        // postfix operator
        BaseUInt const ret = *this;
        --(*this);

        return ret;
    }

    [[nodiscard]] BaseUInt
    next() const
    {
        auto ret = *this;
        return ++ret;
    }

    [[nodiscard]] BaseUInt
    prev() const
    {
        auto ret = *this;
        return --ret;
    }

    BaseUInt&
    operator+=(BaseUInt const& b)
    {
        std::uint64_t carry = 0;

        for (int i = kWIDTH - 1; i >= 0; i--)
        {
            std::uint64_t const n = carry + boost::endian::big_to_native(data_[i]) +
                boost::endian::big_to_native(b.data_[i]);

            data_[i] = boost::endian::native_to_big(static_cast<std::uint32_t>(n));
            carry = n >> 32;
        }

        return *this;
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, BaseUInt const& a) noexcept
    {
        // Do not allow any endian transformations on this memory
        h(a.data_.data(), sizeof(a.data_));
    }

    /** Parse a hex string into a base_uint

        The input must be precisely `2 * bytes` hexadecimal characters
        long, with one exception: the value '0'.

        @param sv A null-terminated string of hexadecimal characters
        @return true if the input was parsed properly; false otherwise.
     */
    [[nodiscard]] constexpr bool
    parseHex(std::string_view sv)
    {
        auto const result = parseFromStringView(sv);
        if (!result)
            return false;

        data_ = *result;
        return true;
    }

    [[nodiscard]] constexpr bool
    parseHex(char const* str)
    {
        return parseHex(std::string_view{str});
    }

    [[nodiscard]] bool
    parseHex(std::string const& str)
    {
        return parseHex(std::string_view{str});
    }

    constexpr static std::size_t
    size()
    {
        return kBYTES;
    }

    BaseUInt<Bits, Tag>&
    operator=(beast::Zero)
    {
        data_.fill(0);
        return *this;
    }

    // Deprecated.
    [[nodiscard]] bool
    isZero() const
    {
        return *this == beast::kZERO;
    }
    [[nodiscard]] bool
    isNonZero() const
    {
        return *this != beast::kZERO;
    }
    void
    zero()
    {
        *this = beast::kZERO;
    }
};

using uint128 = BaseUInt<128>;
using uint160 = BaseUInt<160>;
using uint256 = BaseUInt<256>;
using uint192 = BaseUInt<192>;

template <std::size_t Bits, class Tag>
[[nodiscard]] constexpr std::strong_ordering
operator<=>(BaseUInt<Bits, Tag> const& lhs, BaseUInt<Bits, Tag> const& rhs)
{
    // This comparison might seem wrong on a casual inspection because it
    // compares data internally stored as std::uint32_t byte-by-byte. But
    // note that the underlying data is stored in big endian, even if the
    // platform is little endian. This makes the comparison correct.
    //
    // FIXME: use std::lexicographical_compare_three_way once support is
    //        added to MacOS.

    auto const ret = std::mismatch(lhs.cbegin(), lhs.cend(), rhs.cbegin());

    // a == b
    if (ret.first == lhs.cend())
        return std::strong_ordering::equivalent;

    return (*ret.first > *ret.second) ? std::strong_ordering::greater : std::strong_ordering::less;
}

template <std::size_t Bits, typename Tag>
[[nodiscard]] constexpr bool
operator==(BaseUInt<Bits, Tag> const& lhs, BaseUInt<Bits, Tag> const& rhs)
{
    return (lhs <=> rhs) == 0;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
constexpr bool
operator==(BaseUInt<Bits, Tag> const& a, std::uint64_t b)
{
    return a == BaseUInt<Bits, Tag>(b);
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
constexpr BaseUInt<Bits, Tag>
operator^(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b)
{
    return BaseUInt<Bits, Tag>(a) ^= b;
}

template <std::size_t Bits, class Tag>
constexpr BaseUInt<Bits, Tag>
operator&(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b)
{
    return BaseUInt<Bits, Tag>(a) &= b;
}

template <std::size_t Bits, class Tag>
constexpr BaseUInt<Bits, Tag>
operator|(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b)
{
    return BaseUInt<Bits, Tag>(a) |= b;
}

template <std::size_t Bits, class Tag>
constexpr BaseUInt<Bits, Tag>
operator+(BaseUInt<Bits, Tag> const& a, BaseUInt<Bits, Tag> const& b)
{
    return BaseUInt<Bits, Tag>(a) += b;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline std::string
to_string(BaseUInt<Bits, Tag> const& a)
{
    return strHex(a.cbegin(), a.cend());
}

template <std::size_t Bits, class Tag>
inline std::string
toShortString(BaseUInt<Bits, Tag> const& a)
{
    static_assert(BaseUInt<Bits, Tag>::kBYTES > 4, "For 4 bytes or less, use a native type");
    return strHex(a.cbegin(), a.cbegin() + 4) + "...";
}

template <std::size_t Bits, class Tag>
inline std::ostream&
operator<<(std::ostream& out, BaseUInt<Bits, Tag> const& u)
{
    return out << to_string(u);
}

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

#ifndef __INTELLISENSE__
static_assert(sizeof(uint128) == 128 / 8, "There should be no padding bytes");
static_assert(sizeof(uint160) == 160 / 8, "There should be no padding bytes");
static_assert(sizeof(uint192) == 192 / 8, "There should be no padding bytes");
static_assert(sizeof(uint256) == 256 / 8, "There should be no padding bytes");
#endif

}  // namespace xrpl

namespace beast {

template <std::size_t Bits, class Tag>
struct IsUniquelyRepresented<xrpl::BaseUInt<Bits, Tag>> : public std::true_type
{
    explicit IsUniquelyRepresented() = default;
};

}  // namespace beast
