//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2014, Nikolaos D. Bougalis <nikb@bougalis.net>

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

#ifndef BEAST_UTILITY_TAGGED_INTEGER_H_INCLUDED
#define BEAST_UTILITY_TAGGED_INTEGER_H_INCLUDED

#include <ripple/beast/hash/hash_append.h>

#include <functional>
#include <iostream>
#include <type_traits>
#include <utility>

namespace ripple {

/** A type-safe wrap around standard unsigned integral types

    The tag is used to implement type safety, catching mismatched types at
    compile time. Multiple instantiations wrapping the same underlying integral
    type are distinct types (distinguished by tag) and will not interoperate.

    A tagged_integer supports all the comparison operators that are available
    for the underlying integral type. It only supports a subset of arithmetic
    operators, restricting mutation to only safe and meaningful types.
*/
template <class Int, class Tag>
class tagged_integer
{
private:
    static_assert (std::is_unsigned <Int>::value,
        "The specified Int type must be unsigned");

    Int m_value;

public:
    using value_type = Int;
    using tag_type = Tag;

    tagged_integer() = default;

    template <
        class OtherInt,
        class = typename std::enable_if <
            std::is_integral <OtherInt>::value &&
            sizeof (OtherInt) <= sizeof (Int)
        >::type
    >
    explicit
    /* constexpr */
    tagged_integer (OtherInt value) noexcept
        : m_value (value)
    {
    }

    // Arithmetic operators
    tagged_integer&
    operator++ () noexcept
    {
        ++m_value;
        return *this;
    }

    tagged_integer
    operator++ (int) noexcept
    {
        tagged_integer orig (*this);
        ++(*this);
        return orig;
    }

    tagged_integer&
    operator-- () noexcept
    {
        --m_value;
        return *this;
    }

    tagged_integer
    operator-- (int) noexcept
    {
        tagged_integer orig (*this);
        --(*this);
        return orig;
    }

    template <class OtherInt>
    typename std::enable_if <
        std::is_integral <OtherInt>::value &&
            sizeof (OtherInt) <= sizeof (Int),
    tagged_integer <Int, Tag>>::type&
    operator+= (OtherInt rhs) noexcept
    {
        m_value += rhs;
        return *this;
    }

    template <class OtherInt>
    typename std::enable_if <
        std::is_integral <OtherInt>::value &&
            sizeof (OtherInt) <= sizeof (Int),
    tagged_integer <Int, Tag>>::type&
    operator-= (OtherInt rhs) noexcept
    {
        m_value -= rhs;
        return *this;
    }

    template <class OtherInt>
    friend
    typename std::enable_if <
        std::is_integral <OtherInt>::value &&
            sizeof (OtherInt) <= sizeof (Int),
    tagged_integer <Int, Tag>>::type
    operator+ (tagged_integer const& lhs,
               OtherInt rhs) noexcept
    {
        return tagged_integer (lhs.m_value + rhs);
    }

    template <class OtherInt>
    friend
    typename std::enable_if <
        std::is_integral <OtherInt>::value &&
            sizeof (OtherInt) <= sizeof (Int),
    tagged_integer <Int, Tag>>::type
    operator+ (OtherInt lhs,
               tagged_integer const& rhs) noexcept
    {
        return tagged_integer (lhs + rhs.m_value);
    }

    template <class OtherInt>
    friend
    typename std::enable_if <
        std::is_integral <OtherInt>::value &&
            sizeof (OtherInt) <= sizeof (Int),
    tagged_integer <Int, Tag>>::type
    operator- (tagged_integer const& lhs,
               OtherInt rhs) noexcept
    {
        return tagged_integer (lhs.m_value - rhs);
    }

    friend
    Int
    operator- (tagged_integer const& lhs,
               tagged_integer const& rhs) noexcept
    {
        return lhs.m_value - rhs.m_value;
    }

    // Comparison operators
    friend
    bool
    operator== (tagged_integer const& lhs,
                tagged_integer const& rhs) noexcept
    {
        return lhs.m_value == rhs.m_value;
    }

    friend
    bool
    operator!= (tagged_integer const& lhs,
                tagged_integer const& rhs) noexcept
    {
        return lhs.m_value != rhs.m_value;
    }

    friend
    bool
    operator< (tagged_integer const& lhs,
               tagged_integer const& rhs) noexcept
    {
        return lhs.m_value < rhs.m_value;
    }

    friend
    bool
    operator<= (tagged_integer const& lhs,
                tagged_integer const& rhs) noexcept
    {
        return lhs.m_value <= rhs.m_value;
    }

    friend
    bool
    operator> (tagged_integer const& lhs,
               tagged_integer const& rhs) noexcept
    {
        return lhs.m_value > rhs.m_value;
    }

    friend
    bool
    operator>= (tagged_integer const& lhs,
                tagged_integer const& rhs) noexcept
    {
        return lhs.m_value >= rhs.m_value;
    }

    friend
    std::ostream&
    operator<< (std::ostream& s, tagged_integer const& t)
    {
        s << t.m_value;
        return s;
    }

    friend
    std::istream&
    operator>> (std::istream& s, tagged_integer& t)
    {
        s >> t.m_value;
        return s;
    }
};

} // ripple

namespace beast {
template <class Int, class Tag, class HashAlgorithm>
struct is_contiguously_hashable<ripple::tagged_integer<Int, Tag>, HashAlgorithm>
    : public is_contiguously_hashable<Int, HashAlgorithm>
{
};

} // beast
#endif

