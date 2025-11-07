// Copyright (c) 2014, Nikolaos D. Bougalis <nikb@bougalis.net>

#ifndef BEAST_UTILITY_TAGGED_INTEGER_H_INCLUDED
#define BEAST_UTILITY_TAGGED_INTEGER_H_INCLUDED

#include <xrpl/beast/hash/hash_append.h>

#include <boost/operators.hpp>

#include <iostream>
#include <type_traits>

namespace ripple {

/** A type-safe wrap around standard integral types

    The tag is used to implement type safety, catching mismatched types at
    compile time. Multiple instantiations wrapping the same underlying integral
    type are distinct types (distinguished by tag) and will not interoperate. A
    tagged_integer supports all the usual assignment, arithmetic, comparison and
    shifting operations defined for the underlying type

    The tag is not meant as a unit, which would require restricting the set of
    allowed arithmetic operations.
*/
template <class Int, class Tag>
class tagged_integer
    : boost::totally_ordered<
          tagged_integer<Int, Tag>,
          boost::integer_arithmetic<
              tagged_integer<Int, Tag>,
              boost::bitwise<
                  tagged_integer<Int, Tag>,
                  boost::unit_steppable<
                      tagged_integer<Int, Tag>,
                      boost::shiftable<tagged_integer<Int, Tag>>>>>>
{
private:
    Int m_value;

public:
    using value_type = Int;
    using tag_type = Tag;

    tagged_integer() = default;

    template <
        class OtherInt,
        class = typename std::enable_if<
            std::is_integral<OtherInt>::value &&
            sizeof(OtherInt) <= sizeof(Int)>::type>
    explicit constexpr tagged_integer(OtherInt value) noexcept : m_value(value)
    {
        static_assert(
            sizeof(tagged_integer) == sizeof(Int),
            "tagged_integer is adding padding");
    }

    bool
    operator<(tagged_integer const& rhs) const noexcept
    {
        return m_value < rhs.m_value;
    }

    bool
    operator==(tagged_integer const& rhs) const noexcept
    {
        return m_value == rhs.m_value;
    }

    tagged_integer&
    operator+=(tagged_integer const& rhs) noexcept
    {
        m_value += rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator-=(tagged_integer const& rhs) noexcept
    {
        m_value -= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator*=(tagged_integer const& rhs) noexcept
    {
        m_value *= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator/=(tagged_integer const& rhs) noexcept
    {
        m_value /= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator%=(tagged_integer const& rhs) noexcept
    {
        m_value %= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator|=(tagged_integer const& rhs) noexcept
    {
        m_value |= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator&=(tagged_integer const& rhs) noexcept
    {
        m_value &= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator^=(tagged_integer const& rhs) noexcept
    {
        m_value ^= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator<<=(tagged_integer const& rhs) noexcept
    {
        m_value <<= rhs.m_value;
        return *this;
    }

    tagged_integer&
    operator>>=(tagged_integer const& rhs) noexcept
    {
        m_value >>= rhs.m_value;
        return *this;
    }

    tagged_integer
    operator~() const noexcept
    {
        return tagged_integer{~m_value};
    }

    tagged_integer
    operator+() const noexcept
    {
        return *this;
    }

    tagged_integer
    operator-() const noexcept
    {
        return tagged_integer{-m_value};
    }

    tagged_integer&
    operator++() noexcept
    {
        ++m_value;
        return *this;
    }

    tagged_integer&
    operator--() noexcept
    {
        --m_value;
        return *this;
    }

    explicit
    operator Int() const noexcept
    {
        return m_value;
    }

    friend std::ostream&
    operator<<(std::ostream& s, tagged_integer const& t)
    {
        s << t.m_value;
        return s;
    }

    friend std::istream&
    operator>>(std::istream& s, tagged_integer& t)
    {
        s >> t.m_value;
        return s;
    }

    friend std::string
    to_string(tagged_integer const& t)
    {
        return std::to_string(t.m_value);
    }
};

}  // namespace ripple

namespace beast {
template <class Int, class Tag, class HashAlgorithm>
struct is_contiguously_hashable<ripple::tagged_integer<Int, Tag>, HashAlgorithm>
    : public is_contiguously_hashable<Int, HashAlgorithm>
{
    explicit is_contiguously_hashable() = default;
};

}  // namespace beast
#endif
