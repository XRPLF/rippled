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

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIPPLE_BASICS_BASE_UINT_H_INCLUDED
#define RIPPLE_BASICS_BASE_UINT_H_INCLUDED

#include <ripple/basics/hardened_hash.h>
#include <ripple/basics/strHex.h>
#include <ripple/beast/cxx17/type_traits.h>
#include <ripple/beast/utility/Zero.h>
#include <boost/endian/conversion.hpp>
#include <boost/functional/hash.hpp>
#include <array>
#include <cstring>
#include <functional>
#include <type_traits>

namespace ripple {

namespace detail {

template <class Container, class = std::void_t<>>
struct is_contiguous_container : std::false_type
{
};

template <class Container>
struct is_contiguous_container<
    Container,
    std::void_t<
        decltype(std::declval<Container const>().size()),
        decltype(std::declval<Container const>().data()),
        typename Container::value_type>> : std::true_type
{
};

}  // namespace detail

// This class stores its values internally in big-endian form

template <std::size_t Bits, class Tag = void>
class base_uint
{
    static_assert(
        (Bits % 32) == 0,
        "The length of a base_uint in bits must be a multiple of 32.");

    static_assert(
        Bits >= 64,
        "The length of a base_uint in bits must be at least 64.");

    static constexpr std::size_t WIDTH = Bits / 32;

    // This is really big-endian in byte order.
    // We sometimes use std::uint32_t for speed.

    std::array<std::uint32_t, WIDTH> data_;

public:
    //--------------------------------------------------------------------------
    //
    // STL Container Interface
    //

    static std::size_t constexpr bytes = Bits / 8;
    static_assert(sizeof(data_) == bytes, "");

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
    const_pointer
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
        return data() + bytes;
    }
    const_iterator
    begin() const
    {
        return data();
    }
    const_iterator
    end() const
    {
        return data() + bytes;
    }
    const_iterator
    cbegin() const
    {
        return data();
    }
    const_iterator
    cend() const
    {
        return data() + bytes;
    }

    /** Value hashing function.
        The seed prevents crafted inputs from causing degenerate parent
       containers.
    */
    using hasher = hardened_hash<>;

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

    explicit base_uint(void const* data, VoidHelper)
    {
        memcpy(data_.data(), data, bytes);
    }

public:
    base_uint()
    {
        *this = beast::zero;
    }

    base_uint(beast::Zero)
    {
        *this = beast::zero;
    }

    explicit base_uint(std::uint64_t b)
    {
        *this = b;
    }

    template <
        class Container,
        class = std::enable_if_t<
            detail::is_contiguous_container<Container>::value &&
            std::is_trivially_copyable<typename Container::value_type>::value>>
    explicit base_uint(Container const& c)
    {
        assert(c.size() * sizeof(typename Container::value_type) == size());
        std::memcpy(data_.data(), c.data(), size());
    }

    template <class Container>
    std::enable_if_t<
        detail::is_contiguous_container<Container>::value &&
            std::is_trivially_copyable<typename Container::value_type>::value,
        base_uint&>
    operator=(Container const& c)
    {
        assert(c.size() * sizeof(typename Container::value_type) == size());
        std::memcpy(data_.data(), c.data(), size());
        return *this;
    }

    /* Construct from a raw pointer.
        The buffer pointed to by `data` must be at least Bits/8 bytes.
    */
    static base_uint
    fromVoid(void const* data)
    {
        return base_uint(data, VoidHelper());
    }

    int
    signum() const
    {
        for (int i = 0; i < WIDTH; i++)
            if (data_[i] != 0)
                return 1;

        return 0;
    }

    bool
    operator!() const
    {
        return *this == beast::zero;
    }

    const base_uint
    operator~() const
    {
        base_uint ret;

        for (int i = 0; i < WIDTH; i++)
            ret.data_[i] = ~data_[i];

        return ret;
    }

    base_uint&
    operator=(std::uint64_t uHost)
    {
        *this = beast::zero;
        union
        {
            unsigned u[2];
            std::uint64_t ul;
        };
        // Put in least significant bits.
        ul = boost::endian::native_to_big(uHost);
        data_[WIDTH - 2] = u[0];
        data_[WIDTH - 1] = u[1];
        return *this;
    }

    base_uint&
    operator^=(const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            data_[i] ^= b.data_[i];

        return *this;
    }

    base_uint&
    operator&=(const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            data_[i] &= b.data_[i];

        return *this;
    }

    base_uint&
    operator|=(const base_uint& b)
    {
        for (int i = 0; i < WIDTH; i++)
            data_[i] |= b.data_[i];

        return *this;
    }

    base_uint&
    operator++()
    {
        // prefix operator
        for (int i = WIDTH - 1; i >= 0; --i)
        {
            data_[i] = boost::endian::native_to_big(
                boost::endian::big_to_native(data_[i]) + 1);
            if (data_[i] != 0)
                break;
        }

        return *this;
    }

    const base_uint
    operator++(int)
    {
        // postfix operator
        const base_uint ret = *this;
        ++(*this);

        return ret;
    }

    base_uint&
    operator--()
    {
        for (int i = WIDTH - 1; i >= 0; --i)
        {
            auto prev = data_[i];
            data_[i] = boost::endian::native_to_big(
                boost::endian::big_to_native(data_[i]) - 1);

            if (prev != 0)
                break;
        }

        return *this;
    }

    const base_uint
    operator--(int)
    {
        // postfix operator
        const base_uint ret = *this;
        --(*this);

        return ret;
    }

    base_uint&
    operator+=(const base_uint& b)
    {
        std::uint64_t carry = 0;

        for (int i = WIDTH; i--;)
        {
            std::uint64_t n = carry + boost::endian::big_to_native(data_[i]) +
                boost::endian::big_to_native(b.data_[i]);

            data_[i] =
                boost::endian::native_to_big(static_cast<std::uint32_t>(n));
            carry = n >> 32;
        }

        return *this;
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, base_uint const& a) noexcept
    {
        // Do not allow any endian transformations on this memory
        h(a.data_.data(), sizeof(a.data_));
    }

    /** Parse a hex string into a base_uint
        The string must contain exactly bytes * 2 hex characters and must not
        have any leading or trailing whitespace.
    */
    bool
    SetHexExact(const char* psz)
    {
        unsigned char* pOut = begin();

        for (int i = 0; i < sizeof(data_); ++i)
        {
            auto hi = charUnHex(*psz++);
            if (hi == -1)
                return false;

            auto lo = charUnHex(*psz++);
            if (lo == -1)
                return false;

            *pOut++ = (hi << 4) | lo;
        }

        // We've consumed exactly as many bytes as we needed at this point
        // so we should be at the end of the string.
        return (*psz == 0);
    }

    /** Parse a hex string into a base_uint
        The input can be:
            - shorter than the full hex representation by not including leading
              zeroes.
            - longer than the full hex representation in which case leading
              bytes are discarded.

        When finished parsing, the string must be fully consumed with only a
        null terminator remaining.

        When bStrict is false, the parsing is done in non-strict mode, and, if
        present, leading whitespace and the 0x prefix will be skipped.
    */
    bool
    SetHex(const char* psz, bool bStrict = false)
    {
        // Find beginning.
        auto pBegin = reinterpret_cast<const unsigned char*>(psz);
        // skip leading spaces
        if (!bStrict)
            while (isspace(*pBegin))
                pBegin++;

        // skip 0x
        if (!bStrict && pBegin[0] == '0' && tolower(pBegin[1]) == 'x')
            pBegin += 2;

        // Find end.
        auto pEnd = pBegin;
        while (charUnHex(*pEnd) != -1)
            pEnd++;

        // Take only last digits of over long string.
        if ((unsigned int)(pEnd - pBegin) > 2 * size())
            pBegin = pEnd - 2 * size();

        unsigned char* pOut = end() - ((pEnd - pBegin + 1) / 2);

        *this = beast::zero;

        if ((pEnd - pBegin) & 1)
            *pOut++ = charUnHex(*pBegin++);

        while (pBegin != pEnd)
        {
            auto cHigh = charUnHex(*pBegin++);
            auto cLow = pBegin == pEnd ? 0 : charUnHex(*pBegin++);

            if (cHigh == -1 || cLow == -1)
                return false;

            *pOut++ = (cHigh << 4) | cLow;
        }

        return !*pEnd;
    }

    bool
    SetHex(std::string const& str, bool bStrict = false)
    {
        return SetHex(str.c_str(), bStrict);
    }

    bool
    SetHexExact(std::string const& str)
    {
        return SetHexExact(str.c_str());
    }

    constexpr static std::size_t
    size()
    {
        return bytes;
    }

    base_uint<Bits, Tag>& operator=(beast::Zero)
    {
        data_.fill(0);
        return *this;
    }

    // Deprecated.
    bool
    isZero() const
    {
        return *this == beast::zero;
    }
    bool
    isNonZero() const
    {
        return *this != beast::zero;
    }
    void
    zero()
    {
        *this = beast::zero;
    }
};

using uint128 = base_uint<128>;
using uint160 = base_uint<160>;
using uint256 = base_uint<256>;

template <std::size_t Bits, class Tag>
inline int
compare(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    auto ret = std::mismatch(a.cbegin(), a.cend(), b.cbegin());

    if (ret.first == a.cend())
        return 0;

    // a > b
    if (*ret.first > *ret.second)
        return 1;

    // a < b
    return -1;
}

template <std::size_t Bits, class Tag>
inline bool
operator<(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare(a, b) < 0;
}

template <std::size_t Bits, class Tag>
inline bool
operator<=(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare(a, b) <= 0;
}

template <std::size_t Bits, class Tag>
inline bool
operator>(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare(a, b) > 0;
}

template <std::size_t Bits, class Tag>
inline bool
operator>=(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare(a, b) >= 0;
}

template <std::size_t Bits, class Tag>
inline bool
operator==(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare(a, b) == 0;
}

template <std::size_t Bits, class Tag>
inline bool
operator!=(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return compare(a, b) != 0;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline bool
operator==(base_uint<Bits, Tag> const& a, std::uint64_t b)
{
    return a == base_uint<Bits, Tag>(b);
}

template <std::size_t Bits, class Tag>
inline bool
operator!=(base_uint<Bits, Tag> const& a, std::uint64_t b)
{
    return !(a == b);
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag>
operator^(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag>(a) ^= b;
}

template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag>
operator&(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag>(a) &= b;
}

template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag>
operator|(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag>(a) |= b;
}

template <std::size_t Bits, class Tag>
inline const base_uint<Bits, Tag>
operator+(base_uint<Bits, Tag> const& a, base_uint<Bits, Tag> const& b)
{
    return base_uint<Bits, Tag>(a) += b;
}

//------------------------------------------------------------------------------
template <std::size_t Bits, class Tag>
inline std::string
to_string(base_uint<Bits, Tag> const& a)
{
    return strHex(a.cbegin(), a.cend());
}

// Function templates that return a base_uint given text in hexadecimal.
// Invoke like:
//   auto i = from_hex_text<uint256>("AAAAA");
template <typename T>
auto
from_hex_text(char const* text) -> std::enable_if_t<
    std::is_same<T, base_uint<T::bytes * 8, typename T::tag_type>>::value,
    T>
{
    T ret;
    ret.SetHex(text);
    return ret;
}

template <typename T>
auto
from_hex_text(std::string const& text) -> std::enable_if_t<
    std::is_same<T, base_uint<T::bytes * 8, typename T::tag_type>>::value,
    T>
{
    T ret;
    ret.SetHex(text);
    return ret;
}

template <std::size_t Bits, class Tag>
inline std::ostream&
operator<<(std::ostream& out, base_uint<Bits, Tag> const& u)
{
    return out << to_string(u);
}

#ifndef __INTELLISENSE__
static_assert(sizeof(uint128) == 128 / 8, "There should be no padding bytes");
static_assert(sizeof(uint160) == 160 / 8, "There should be no padding bytes");
static_assert(sizeof(uint256) == 256 / 8, "There should be no padding bytes");
#endif

}  // namespace ripple

namespace beast {

template <std::size_t Bits, class Tag>
struct is_uniquely_represented<ripple::base_uint<Bits, Tag>>
    : public std::true_type
{
    explicit is_uniquely_represented() = default;
};

}  // namespace beast

#endif
