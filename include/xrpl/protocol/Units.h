#ifndef PROTOCOL_UNITS_H_INCLUDED
#define PROTOCOL_UNITS_H_INCLUDED

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/operators.hpp>

#include <iosfwd>
#include <limits>
#include <optional>

namespace ripple {

namespace unit {

/** "drops" are the smallest divisible amount of XRP. This is what most
    of the code uses. */
struct dropTag;
/** "fee levels" are used by the transaction queue to compare the relative
    cost of transactions that require different levels of effort to process.
    See also: src/ripple/app/misc/FeeEscalation.md#fee-level */
struct feelevelTag;
/** unitless values are plain scalars wrapped in a ValueUnit. They are
    used for calculations in this header. */
struct unitlessTag;

/** Units to represent basis points (bips) and 1/10 basis points */
class BipsTag;
class TenthBipsTag;

// These names don't have to be too descriptive, because we're in the "unit"
// namespace.

template <class T>
concept Valid = std::is_class_v<T> && std::is_object_v<typename T::unit_type> &&
    std::is_object_v<typename T::value_type>;

/** `Usable` is checked to ensure that only values with
    known valid type tags can be used (sometimes transparently) in
    non-unit contexts. At the time of implementation, this includes
    all known tags, but more may be added in the future, and they
    should not be added automatically unless determined to be
    appropriate.
*/
template <class T>
concept Usable = Valid<T> &&
    (std::is_same_v<typename T::unit_type, feelevelTag> ||
     std::is_same_v<typename T::unit_type, unitlessTag> ||
     std::is_same_v<typename T::unit_type, dropTag> ||
     std::is_same_v<typename T::unit_type, BipsTag> ||
     std::is_same_v<typename T::unit_type, TenthBipsTag>);

template <class Other, class VU>
concept Compatible = Valid<VU> && std::is_arithmetic_v<Other> &&
    std::is_arithmetic_v<typename VU::value_type> &&
    std::is_convertible_v<Other, typename VU::value_type>;

template <class T>
concept Integral = std::is_integral_v<T>;

template <class VU>
concept IntegralValue = Integral<typename VU::value_type>;

template <class VU1, class VU2>
concept CastableValue = IntegralValue<VU1> && IntegralValue<VU2> &&
    std::is_same_v<typename VU1::unit_type, typename VU2::unit_type>;

template <class UnitTag, class T>
class ValueUnit : private boost::totally_ordered<ValueUnit<UnitTag, T>>,
                  private boost::additive<ValueUnit<UnitTag, T>>,
                  private boost::equality_comparable<ValueUnit<UnitTag, T>, T>,
                  private boost::dividable<ValueUnit<UnitTag, T>, T>,
                  private boost::modable<ValueUnit<UnitTag, T>, T>,
                  private boost::unit_steppable<ValueUnit<UnitTag, T>>
{
public:
    using unit_type = UnitTag;
    using value_type = T;

private:
    value_type value_;

public:
    ValueUnit() = default;
    constexpr ValueUnit(ValueUnit const& other) = default;
    constexpr ValueUnit&
    operator=(ValueUnit const& other) = default;

    constexpr explicit ValueUnit(beast::Zero) : value_(0)
    {
    }

    constexpr ValueUnit&
    operator=(beast::Zero)
    {
        value_ = 0;
        return *this;
    }

    constexpr explicit ValueUnit(value_type value) : value_(value)
    {
    }

    constexpr ValueUnit&
    operator=(value_type value)
    {
        value_ = value;
        return *this;
    }

    /** Instances with the same unit, and a type that is
        "safe" to convert to this one can be converted
        implicitly */
    template <Compatible<ValueUnit> Other>
    constexpr ValueUnit(ValueUnit<unit_type, Other> const& value)
        requires SafeToCast<Other, value_type>
        : ValueUnit(safe_cast<value_type>(value.value()))
    {
    }

    constexpr ValueUnit
    operator+(value_type const& rhs) const
    {
        return ValueUnit{value_ + rhs};
    }

    friend constexpr ValueUnit
    operator+(value_type lhs, ValueUnit const& rhs)
    {
        // addition is commutative
        return rhs + lhs;
    }

    constexpr ValueUnit
    operator-(value_type const& rhs) const
    {
        return ValueUnit{value_ - rhs};
    }

    friend constexpr ValueUnit
    operator-(value_type lhs, ValueUnit const& rhs)
    {
        // subtraction is NOT commutative, but (lhs + (-rhs)) is addition, which
        // is
        return -rhs + lhs;
    }

    constexpr ValueUnit
    operator*(value_type const& rhs) const
    {
        return ValueUnit{value_ * rhs};
    }

    friend constexpr ValueUnit
    operator*(value_type lhs, ValueUnit const& rhs)
    {
        // multiplication is commutative
        return rhs * lhs;
    }

    constexpr value_type
    operator/(ValueUnit const& rhs) const
    {
        return value_ / rhs.value_;
    }

    ValueUnit&
    operator+=(ValueUnit const& other)
    {
        value_ += other.value();
        return *this;
    }

    ValueUnit&
    operator-=(ValueUnit const& other)
    {
        value_ -= other.value();
        return *this;
    }

    ValueUnit&
    operator++()
    {
        ++value_;
        return *this;
    }

    ValueUnit&
    operator--()
    {
        --value_;
        return *this;
    }

    ValueUnit&
    operator*=(value_type const& rhs)
    {
        value_ *= rhs;
        return *this;
    }

    ValueUnit&
    operator/=(value_type const& rhs)
    {
        value_ /= rhs;
        return *this;
    }

    template <Integral transparent = value_type>
    ValueUnit&
    operator%=(value_type const& rhs)
    {
        value_ %= rhs;
        return *this;
    }

    ValueUnit
    operator-() const
    {
        static_assert(
            std::is_signed_v<T>, "- operator illegal on unsigned value types");
        return ValueUnit{-value_};
    }

    constexpr bool
    operator==(ValueUnit const& other) const
    {
        return value_ == other.value_;
    }

    template <Compatible<ValueUnit> Other>
    constexpr bool
    operator==(ValueUnit<unit_type, Other> const& other) const
    {
        return value_ == other.value();
    }

    constexpr bool
    operator==(value_type other) const
    {
        return value_ == other;
    }

    template <Compatible<ValueUnit> Other>
    constexpr bool
    operator!=(ValueUnit<unit_type, Other> const& other) const
    {
        return !operator==(other);
    }

    constexpr bool
    operator<(ValueUnit const& other) const
    {
        return value_ < other.value_;
    }

    /** Returns true if the amount is not zero */
    explicit constexpr
    operator bool() const noexcept
    {
        return value_ != 0;
    }

    /** Return the sign of the amount */
    constexpr int
    signum() const noexcept
    {
        return (value_ < 0) ? -1 : (value_ ? 1 : 0);
    }

    /** Returns the number of drops */
    // TODO: Move this to a new class, maybe with the old "TaggedFee" name
    constexpr value_type
    fee() const
    {
        return value_;
    }

    template <class Other>
    constexpr double
    decimalFromReference(ValueUnit<unit_type, Other> reference) const
    {
        return static_cast<double>(value_) / reference.value();
    }

    // `Usable` is checked to ensure that only values with
    // known valid type tags can be converted to JSON. At the time
    // of implementation, that includes all known tags, but more may
    // be added in the future.
    Json::Value
    jsonClipped() const
        requires Usable<ValueUnit>
    {
        if constexpr (std::is_integral_v<value_type>)
        {
            using jsontype = std::conditional_t<
                std::is_signed_v<value_type>,
                Json::Int,
                Json::UInt>;

            constexpr auto min = std::numeric_limits<jsontype>::min();
            constexpr auto max = std::numeric_limits<jsontype>::max();

            if (value_ < min)
                return min;
            if (value_ > max)
                return max;
            return static_cast<jsontype>(value_);
        }
        else
        {
            return value_;
        }
    }

    /** Returns the underlying value. Code SHOULD NOT call this
        function unless the type has been abstracted away,
        e.g. in a templated function.
    */
    constexpr value_type
    value() const
    {
        return value_;
    }

    friend std::istream&
    operator>>(std::istream& s, ValueUnit& val)
    {
        s >> val.value_;
        return s;
    }
};

// Output Values as just their numeric value.
template <class Char, class Traits, class UnitTag, class T>
std::basic_ostream<Char, Traits>&
operator<<(std::basic_ostream<Char, Traits>& os, ValueUnit<UnitTag, T> const& q)
{
    return os << q.value();
}

template <class UnitTag, class T>
std::string
to_string(ValueUnit<UnitTag, T> const& amount)
{
    return std::to_string(amount.value());
}

template <class Source>
concept muldivSource = Valid<Source> &&
    std::is_convertible_v<typename Source::value_type, std::uint64_t>;

template <class Dest>
concept muldivDest = muldivSource<Dest> &&  // Dest is also a source
    std::is_convertible_v<std::uint64_t, typename Dest::value_type> &&
    sizeof(typename Dest::value_type) >= sizeof(std::uint64_t);

template <class Source2, class Source1>
concept muldivSources = muldivSource<Source1> && muldivSource<Source2> &&
    std::is_same_v<typename Source1::unit_type, typename Source2::unit_type>;

template <class Dest, class Source1, class Source2>
concept muldivable = muldivSources<Source1, Source2> && muldivDest<Dest>;
// Source and Dest can be the same by default

template <class Dest, class Source1, class Source2>
concept muldivCommutable = muldivable<Dest, Source1, Source2> &&
    !std::is_same_v<typename Source1::unit_type, typename Dest::unit_type>;

template <class T>
ValueUnit<unitlessTag, T>
scalar(T value)
{
    return ValueUnit<unitlessTag, T>{value};
}

template <class Source1, class Source2, unit::muldivable<Source1, Source2> Dest>
std::optional<Dest>
mulDivU(Source1 value, Dest mul, Source2 div)
{
    // values can never be negative in any context.
    if (value.value() < 0 || mul.value() < 0 || div.value() < 0)
    {
        // split the asserts so if one hits, the user can tell which
        // without a debugger.
        XRPL_ASSERT(
            value.value() >= 0, "ripple::unit::mulDivU : minimum value input");
        XRPL_ASSERT(
            mul.value() >= 0, "ripple::unit::mulDivU : minimum mul input");
        XRPL_ASSERT(
            div.value() > 0, "ripple::unit::mulDivU : minimum div input");
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

}  // namespace unit

// Fee Levels
template <class T>
using FeeLevel = unit::ValueUnit<unit::feelevelTag, T>;
using FeeLevel64 = FeeLevel<std::uint64_t>;
using FeeLevelDouble = FeeLevel<double>;

// Basis points (Bips)
template <class T>
using Bips = unit::ValueUnit<unit::BipsTag, T>;
using Bips16 = Bips<std::uint16_t>;
using Bips32 = Bips<std::uint32_t>;
template <class T>
using TenthBips = unit::ValueUnit<unit::TenthBipsTag, T>;
using TenthBips16 = TenthBips<std::uint16_t>;
using TenthBips32 = TenthBips<std::uint32_t>;

template <class Source1, class Source2, unit::muldivable<Source1, Source2> Dest>
std::optional<Dest>
mulDiv(Source1 value, Dest mul, Source2 div)
{
    return unit::mulDivU(value, mul, div);
}

template <
    class Source1,
    class Source2,
    unit::muldivCommutable<Source1, Source2> Dest>
std::optional<Dest>
mulDiv(Dest value, Source1 mul, Source2 div)
{
    // Multiplication is commutative
    return unit::mulDivU(mul, value, div);
}

template <unit::muldivDest Dest>
std::optional<Dest>
mulDiv(std::uint64_t value, Dest mul, std::uint64_t div)
{
    // Give the scalars a non-tag so the
    // unit-handling version gets called.
    return unit::mulDivU(unit::scalar(value), mul, unit::scalar(div));
}

template <unit::muldivDest Dest>
std::optional<Dest>
mulDiv(Dest value, std::uint64_t mul, std::uint64_t div)
{
    // Multiplication is commutative
    return mulDiv(mul, value, div);
}

template <unit::muldivSource Source1, unit::muldivSources<Source1> Source2>
std::optional<std::uint64_t>
mulDiv(Source1 value, std::uint64_t mul, Source2 div)
{
    // Give the scalars a dimensionless unit so the
    // unit-handling version gets called.
    auto unitresult = unit::mulDivU(value, unit::scalar(mul), div);

    if (!unitresult)
        return std::nullopt;

    return unitresult->value();
}

template <unit::muldivSource Source1, unit::muldivSources<Source1> Source2>
std::optional<std::uint64_t>
mulDiv(std::uint64_t value, Source1 mul, Source2 div)
{
    // Multiplication is commutative
    return mulDiv(mul, value, div);
}

template <unit::IntegralValue Dest, unit::CastableValue<Dest> Src>
constexpr Dest
safe_cast(Src s) noexcept
{
    // Dest may not have an explicit value constructor
    return Dest{safe_cast<typename Dest::value_type>(s.value())};
}

template <unit::IntegralValue Dest, unit::Integral Src>
constexpr Dest
safe_cast(Src s) noexcept
{
    // Dest may not have an explicit value constructor
    return Dest{safe_cast<typename Dest::value_type>(s)};
}

template <unit::IntegralValue Dest, unit::CastableValue<Dest> Src>
constexpr Dest
unsafe_cast(Src s) noexcept
{
    // Dest may not have an explicit value constructor
    return Dest{unsafe_cast<typename Dest::value_type>(s.value())};
}

template <unit::IntegralValue Dest, unit::Integral Src>
constexpr Dest
unsafe_cast(Src s) noexcept
{
    // Dest may not have an explicit value constructor
    return Dest{unsafe_cast<typename Dest::value_type>(s)};
}

}  // namespace ripple

#endif  // PROTOCOL_UNITS_H_INCLUDED
