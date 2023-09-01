//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef BASICS_FEES_H_INCLUDED
#define BASICS_FEES_H_INCLUDED

#include <ripple/basics/XRPAmount.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <limits>
#include <utility>

#include <cassert>
#include <cmath>
#include <ios>
#include <iosfwd>
#include <sstream>
#include <string>

namespace ripple {

namespace feeunit {

/** "fee units" calculations are a not-really-unitless value that is used
    to express the cost of a given transaction vs. a reference transaction.
    They are primarily used by the Transactor classes. */
struct feeunitTag;
/** "fee levels" are used by the transaction queue to compare the relative
    cost of transactions that require different levels of effort to process.
    See also: src/ripple/app/misc/FeeEscalation.md#fee-level */
struct feelevelTag;
/** unitless values are plain scalars wrapped in a TaggedFee. They are
    used for calculations in this header. */
struct unitlessTag;

template <class T>
using enable_if_unit_t = typename std::enable_if_t<
    std::is_class_v<T> && std::is_object_v<typename T::unit_type> &&
    std::is_object_v<typename T::value_type>>;

/** `is_usable_unit_v` is checked to ensure that only values with
    known valid type tags can be used (sometimes transparently) in
    non-fee contexts. At the time of implementation, this includes
    all known tags, but more may be added in the future, and they
    should not be added automatically unless determined to be
    appropriate.
*/
template <class T, class = enable_if_unit_t<T>>
constexpr bool is_usable_unit_v =
    std::is_same_v<typename T::unit_type, feeunitTag> ||
    std::is_same_v<typename T::unit_type, feelevelTag> ||
    std::is_same_v<typename T::unit_type, unitlessTag> ||
    std::is_same_v<typename T::unit_type, dropTag>;

template <class UnitTag, class T>
class TaggedFee : private boost::totally_ordered<TaggedFee<UnitTag, T>>,
                  private boost::additive<TaggedFee<UnitTag, T>>,
                  private boost::equality_comparable<TaggedFee<UnitTag, T>, T>,
                  private boost::dividable<TaggedFee<UnitTag, T>, T>,
                  private boost::modable<TaggedFee<UnitTag, T>, T>,
                  private boost::unit_steppable<TaggedFee<UnitTag, T>>
{
public:
    using unit_type = UnitTag;
    using value_type = T;

private:
    value_type fee_;

protected:
    template <class Other>
    static constexpr bool is_compatible_v =
        std::is_arithmetic_v<Other>&& std::is_arithmetic_v<value_type>&&
            std::is_convertible_v<Other, value_type>;

    template <class OtherFee, class = enable_if_unit_t<OtherFee>>
    static constexpr bool is_compatiblefee_v =
        is_compatible_v<typename OtherFee::value_type>&&
            std::is_same_v<UnitTag, typename OtherFee::unit_type>;

    template <class Other>
    using enable_if_compatible_t =
        typename std::enable_if_t<is_compatible_v<Other>>;

    template <class OtherFee>
    using enable_if_compatiblefee_t =
        typename std::enable_if_t<is_compatiblefee_v<OtherFee>>;

public:
    TaggedFee() = default;
    constexpr TaggedFee(TaggedFee const& other) = default;
    constexpr TaggedFee&
    operator=(TaggedFee const& other) = default;

    constexpr explicit TaggedFee(beast::Zero) : fee_(0)
    {
    }

    constexpr TaggedFee& operator=(beast::Zero)
    {
        fee_ = 0;
        return *this;
    }

    constexpr explicit TaggedFee(value_type fee) : fee_(fee)
    {
    }

    TaggedFee&
    operator=(value_type fee)
    {
        fee_ = fee;
        return *this;
    }

    /** Instances with the same unit, and a type that is
        "safe" to convert to this one can be converted
        implicitly */
    template <
        class Other,
        class = std::enable_if_t<
            is_compatible_v<Other> &&
            is_safetocasttovalue_v<value_type, Other>>>
    constexpr TaggedFee(TaggedFee<unit_type, Other> const& fee)
        : TaggedFee(safe_cast<value_type>(fee.fee()))
    {
    }

    constexpr TaggedFee
    operator*(value_type const& rhs) const
    {
        return TaggedFee{fee_ * rhs};
    }

    friend constexpr TaggedFee
    operator*(value_type lhs, TaggedFee const& rhs)
    {
        // multiplication is commutative
        return rhs * lhs;
    }

    constexpr value_type
    operator/(TaggedFee const& rhs) const
    {
        return fee_ / rhs.fee_;
    }

    TaggedFee&
    operator+=(TaggedFee const& other)
    {
        fee_ += other.fee();
        return *this;
    }

    TaggedFee&
    operator-=(TaggedFee const& other)
    {
        fee_ -= other.fee();
        return *this;
    }

    TaggedFee&
    operator++()
    {
        ++fee_;
        return *this;
    }

    TaggedFee&
    operator--()
    {
        --fee_;
        return *this;
    }

    TaggedFee&
    operator*=(value_type const& rhs)
    {
        fee_ *= rhs;
        return *this;
    }

    TaggedFee&
    operator/=(value_type const& rhs)
    {
        fee_ /= rhs;
        return *this;
    }

    template <class transparent = value_type>
    std::enable_if_t<std::is_integral_v<transparent>, TaggedFee&>
    operator%=(value_type const& rhs)
    {
        fee_ %= rhs;
        return *this;
    }

    TaggedFee
    operator-() const
    {
        static_assert(
            std::is_signed_v<T>, "- operator illegal on unsigned fee types");
        return TaggedFee{-fee_};
    }

    bool
    operator==(TaggedFee const& other) const
    {
        return fee_ == other.fee_;
    }

    template <class Other, class = enable_if_compatible_t<Other>>
    bool
    operator==(TaggedFee<unit_type, Other> const& other) const
    {
        return fee_ == other.fee();
    }

    bool
    operator==(value_type other) const
    {
        return fee_ == other;
    }

    template <class Other, class = enable_if_compatible_t<Other>>
    bool
    operator!=(TaggedFee<unit_type, Other> const& other) const
    {
        return !operator==(other);
    }

    bool
    operator<(TaggedFee const& other) const
    {
        return fee_ < other.fee_;
    }

    /** Returns true if the amount is not zero */
    explicit constexpr operator bool() const noexcept
    {
        return fee_ != 0;
    }

    /** Return the sign of the amount */
    constexpr int
    signum() const noexcept
    {
        return (fee_ < 0) ? -1 : (fee_ ? 1 : 0);
    }

    /** Returns the number of drops */
    constexpr value_type
    fee() const
    {
        return fee_;
    }

    template <class Other>
    constexpr double
    decimalFromReference(TaggedFee<unit_type, Other> reference) const
    {
        return static_cast<double>(fee_) / reference.fee();
    }

    // `is_usable_unit_v` is checked to ensure that only values with
    // known valid type tags can be converted to JSON. At the time
    // of implementation, that includes all known tags, but more may
    // be added in the future.
    std::enable_if_t<is_usable_unit_v<TaggedFee>, Json::Value>
    jsonClipped() const
    {
        if constexpr (std::is_integral_v<value_type>)
        {
            using jsontype = std::conditional_t<
                std::is_signed_v<value_type>,
                Json::Int,
                Json::UInt>;

            constexpr auto min = std::numeric_limits<jsontype>::min();
            constexpr auto max = std::numeric_limits<jsontype>::max();

            if (fee_ < min)
                return min;
            if (fee_ > max)
                return max;
            return static_cast<jsontype>(fee_);
        }
        else
        {
            return fee_;
        }
    }

    /** Returns the underlying value. Code SHOULD NOT call this
        function unless the type has been abstracted away,
        e.g. in a templated function.
    */
    constexpr value_type
    value() const
    {
        return fee_;
    }

    friend std::istream&
    operator>>(std::istream& s, TaggedFee& val)
    {
        s >> val.fee_;
        return s;
    }
};

// Output Fees as just their numeric value.
template <class Char, class Traits, class UnitTag, class T>
std::basic_ostream<Char, Traits>&
operator<<(std::basic_ostream<Char, Traits>& os, const TaggedFee<UnitTag, T>& q)
{
    return os << q.value();
}

template <class UnitTag, class T>
std::string
to_string(TaggedFee<UnitTag, T> const& amount)
{
    return std::to_string(amount.fee());
}

template <class Source, class = enable_if_unit_t<Source>>
constexpr bool can_muldiv_source_v =
    std::is_convertible_v<typename Source::value_type, std::uint64_t>;

template <class Dest, class = enable_if_unit_t<Dest>>
constexpr bool can_muldiv_dest_v =
    can_muldiv_source_v<Dest>&&  // Dest is also a source
        std::is_convertible_v<std::uint64_t, typename Dest::value_type> &&
    sizeof(typename Dest::value_type) >= sizeof(std::uint64_t);

template <
    class Source1,
    class Source2,
    class = enable_if_unit_t<Source1>,
    class = enable_if_unit_t<Source2>>
constexpr bool can_muldiv_sources_v =
    can_muldiv_source_v<Source1>&& can_muldiv_source_v<Source2>&& std::
        is_same_v<typename Source1::unit_type, typename Source2::unit_type>;

template <
    class Source1,
    class Source2,
    class Dest,
    class = enable_if_unit_t<Source1>,
    class = enable_if_unit_t<Source2>,
    class = enable_if_unit_t<Dest>>
constexpr bool can_muldiv_v =
    can_muldiv_sources_v<Source1, Source2>&& can_muldiv_dest_v<Dest>;
// Source and Dest can be the same by default

template <
    class Source1,
    class Source2,
    class Dest,
    class = enable_if_unit_t<Source1>,
    class = enable_if_unit_t<Source2>,
    class = enable_if_unit_t<Dest>>
constexpr bool can_muldiv_commute_v = can_muldiv_v<Source1, Source2, Dest> &&
    !std::is_same_v<typename Source1::unit_type, typename Dest::unit_type>;

template <class T>
using enable_muldiv_source_t =
    typename std::enable_if_t<can_muldiv_source_v<T>>;

template <class T>
using enable_muldiv_dest_t = typename std::enable_if_t<can_muldiv_dest_v<T>>;

template <class Source1, class Source2>
using enable_muldiv_sources_t =
    typename std::enable_if_t<can_muldiv_sources_v<Source1, Source2>>;

template <class Source1, class Source2, class Dest>
using enable_muldiv_t =
    typename std::enable_if_t<can_muldiv_v<Source1, Source2, Dest>>;

template <class Source1, class Source2, class Dest>
using enable_muldiv_commute_t =
    typename std::enable_if_t<can_muldiv_commute_v<Source1, Source2, Dest>>;

template <class T>
TaggedFee<unitlessTag, T>
scalar(T value)
{
    return TaggedFee<unitlessTag, T>{value};
}

template <
    class Source1,
    class Source2,
    class Dest,
    class = enable_muldiv_t<Source1, Source2, Dest>>
std::optional<Dest>
mulDivU(Source1 value, Dest mul, Source2 div)
{
    // Fees can never be negative in any context.
    if (value.value() < 0 || mul.value() < 0 || div.value() < 0)
    {
        // split the asserts so if one hits, the user can tell which
        // without a debugger.
        assert(value.value() >= 0);
        assert(mul.value() >= 0);
        assert(div.value() >= 0);
        return std::nullopt;
    }

    using desttype = typename Dest::value_type;
    constexpr auto max = std::numeric_limits<desttype>::max();

    // Shortcuts, since these happen a lot in the real world
    if (value == div)
        return mul;
    if (mul.value() == div.value())
    {
        if (value.value() > max)
            return std::nullopt;
        return Dest{static_cast<desttype>(value.value())};
    }

    using namespace boost::multiprecision;

    uint128_t product;
    product = multiply(
        product,
        static_cast<std::uint64_t>(value.value()),
        static_cast<std::uint64_t>(mul.value()));

    auto quotient = product / div.value();

    if (quotient > max)
        return std::nullopt;

    return Dest{static_cast<desttype>(quotient)};
}

}  // namespace feeunit

template <class T>
using FeeLevel = feeunit::TaggedFee<feeunit::feelevelTag, T>;
using FeeLevel64 = FeeLevel<std::uint64_t>;
using FeeLevelDouble = FeeLevel<double>;

template <
    class Source1,
    class Source2,
    class Dest,
    class = feeunit::enable_muldiv_t<Source1, Source2, Dest>>
std::optional<Dest>
mulDiv(Source1 value, Dest mul, Source2 div)
{
    return feeunit::mulDivU(value, mul, div);
}

template <
    class Source1,
    class Source2,
    class Dest,
    class = feeunit::enable_muldiv_commute_t<Source1, Source2, Dest>>
std::optional<Dest>
mulDiv(Dest value, Source1 mul, Source2 div)
{
    // Multiplication is commutative
    return feeunit::mulDivU(mul, value, div);
}

template <class Dest, class = feeunit::enable_muldiv_dest_t<Dest>>
std::optional<Dest>
mulDiv(std::uint64_t value, Dest mul, std::uint64_t div)
{
    // Give the scalars a non-tag so the
    // unit-handling version gets called.
    return feeunit::mulDivU(feeunit::scalar(value), mul, feeunit::scalar(div));
}

template <class Dest, class = feeunit::enable_muldiv_dest_t<Dest>>
std::optional<Dest>
mulDiv(Dest value, std::uint64_t mul, std::uint64_t div)
{
    // Multiplication is commutative
    return mulDiv(mul, value, div);
}

template <
    class Source1,
    class Source2,
    class = feeunit::enable_muldiv_sources_t<Source1, Source2>>
std::optional<std::uint64_t>
mulDiv(Source1 value, std::uint64_t mul, Source2 div)
{
    // Give the scalars a dimensionless unit so the
    // unit-handling version gets called.
    auto unitresult = feeunit::mulDivU(value, feeunit::scalar(mul), div);

    if (!unitresult)
        return std::nullopt;

    return unitresult->value();
}

template <
    class Source1,
    class Source2,
    class = feeunit::enable_muldiv_sources_t<Source1, Source2>>
std::optional<std::uint64_t>
mulDiv(std::uint64_t value, Source1 mul, Source2 div)
{
    // Multiplication is commutative
    return mulDiv(mul, value, div);
}

template <class Dest, class Src>
constexpr std::enable_if_t<
    std::is_same_v<typename Dest::unit_type, typename Src::unit_type> &&
        std::is_integral_v<typename Dest::value_type> &&
        std::is_integral_v<typename Src::value_type>,
    Dest>
safe_cast(Src s) noexcept
{
    // Dest may not have an explicit value constructor
    return Dest{safe_cast<typename Dest::value_type>(s.value())};
}

template <class Dest, class Src>
constexpr std::enable_if_t<
    std::is_same_v<typename Dest::unit_type, typename Src::unit_type> &&
        std::is_integral_v<typename Dest::value_type> &&
        std::is_integral_v<typename Src::value_type>,
    Dest>
unsafe_cast(Src s) noexcept
{
    // Dest may not have an explicit value constructor
    return Dest{unsafe_cast<typename Dest::value_type>(s.value())};
}

}  // namespace ripple

#endif  // BASICS_FEES_H_INCLUDED
