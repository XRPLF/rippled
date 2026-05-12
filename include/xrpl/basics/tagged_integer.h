// Copyright (c) 2014, Nikolaos D. Bougalis <nikb@bougalis.net>

#pragma once

#include <xrpl/beast/hash/hash_append.h>

#include <boost/operators.hpp>

#include <iostream>
#include <type_traits>

namespace xrpl {

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
class TaggedInteger : boost::totally_ordered<
                          TaggedInteger<Int, Tag>,
                          boost::integer_arithmetic<
                              TaggedInteger<Int, Tag>,
                              boost::bitwise<
                                  TaggedInteger<Int, Tag>,
                                  boost::unit_steppable<
                                      TaggedInteger<Int, Tag>,
                                      boost::shiftable<TaggedInteger<Int, Tag>>>>>>
{
private:
    Int value_;

public:
    using value_type = Int;
    using tag_type = Tag;

    TaggedInteger() = default;

    template <
        class OtherInt,
        class = std::enable_if_t<std::is_integral_v<OtherInt> && sizeof(OtherInt) <= sizeof(Int)>>
    explicit constexpr TaggedInteger(OtherInt value) noexcept : value_(value)
    {
        static_assert(sizeof(TaggedInteger) == sizeof(Int), "tagged_integer is adding padding");
    }

    bool
    operator<(TaggedInteger const& rhs) const noexcept
    {
        return value_ < rhs.value_;
    }

    bool
    operator==(TaggedInteger const& rhs) const noexcept
    {
        return value_ == rhs.value_;
    }

    TaggedInteger&
    operator+=(TaggedInteger const& rhs) noexcept
    {
        value_ += rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator-=(TaggedInteger const& rhs) noexcept
    {
        value_ -= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator*=(TaggedInteger const& rhs) noexcept
    {
        value_ *= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator/=(TaggedInteger const& rhs) noexcept
    {
        value_ /= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator%=(TaggedInteger const& rhs) noexcept
    {
        value_ %= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator|=(TaggedInteger const& rhs) noexcept
    {
        value_ |= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator&=(TaggedInteger const& rhs) noexcept
    {
        value_ &= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator^=(TaggedInteger const& rhs) noexcept
    {
        value_ ^= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator<<=(TaggedInteger const& rhs) noexcept
    {
        value_ <<= rhs.value_;
        return *this;
    }

    TaggedInteger&
    operator>>=(TaggedInteger const& rhs) noexcept
    {
        value_ >>= rhs.value_;
        return *this;
    }

    TaggedInteger
    operator~() const noexcept
    {
        return TaggedInteger{~value_};
    }

    TaggedInteger
    operator+() const noexcept
    {
        return *this;
    }

    TaggedInteger
    operator-() const noexcept
    {
        return TaggedInteger{-value_};
    }

    TaggedInteger&
    operator++() noexcept
    {
        ++value_;
        return *this;
    }

    TaggedInteger&
    operator--() noexcept
    {
        --value_;
        return *this;
    }

    explicit
    operator Int() const noexcept
    {
        return value_;
    }

    friend std::ostream&
    operator<<(std::ostream& s, TaggedInteger const& t)
    {
        s << t.value_;
        return s;
    }

    friend std::istream&
    operator>>(std::istream& s, TaggedInteger& t)
    {
        s >> t.value_;
        return s;
    }

    friend std::string
    to_string(TaggedInteger const& t)
    {
        return std::to_string(t.value_);
    }
};

}  // namespace xrpl

namespace beast {
template <class Int, class Tag, class HashAlgorithm>
struct IsContiguouslyHashable<xrpl::TaggedInteger<Int, Tag>, HashAlgorithm>
    : public IsContiguouslyHashable<Int, HashAlgorithm>
{
    explicit IsContiguouslyHashable() = default;
};

}  // namespace beast
