#ifndef XRPL_PROTOCOL_MPTAMOUNT_H_INCLUDED
#define XRPL_PROTOCOL_MPTAMOUNT_H_INCLUDED

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/operators.hpp>

#include <cstdint>
#include <string>

namespace ripple {

class MPTAmount : private boost::totally_ordered<MPTAmount>,
                  private boost::additive<MPTAmount>,
                  private boost::equality_comparable<MPTAmount, std::int64_t>,
                  private boost::additive<MPTAmount, std::int64_t>
{
public:
    using value_type = std::int64_t;

protected:
    value_type value_;

public:
    MPTAmount() = default;
    constexpr MPTAmount(MPTAmount const& other) = default;
    constexpr MPTAmount&
    operator=(MPTAmount const& other) = default;

    // Round to nearest, even on tie.
    explicit MPTAmount(Number const& x) : MPTAmount(static_cast<value_type>(x))
    {
    }

    constexpr explicit MPTAmount(value_type value);

    constexpr MPTAmount& operator=(beast::Zero);

    MPTAmount&
    operator+=(MPTAmount const& other);

    MPTAmount&
    operator-=(MPTAmount const& other);

    MPTAmount
    operator-() const;

    bool
    operator==(MPTAmount const& other) const;

    bool
    operator==(value_type other) const;

    bool
    operator<(MPTAmount const& other) const;

    /** Returns true if the amount is not zero */
    explicit constexpr
    operator bool() const noexcept;

    operator Number() const noexcept
    {
        return value();
    }

    /** Return the sign of the amount */
    constexpr int
    signum() const noexcept;

    /** Returns the underlying value. Code SHOULD NOT call this
        function unless the type has been abstracted away,
        e.g. in a templated function.
    */
    constexpr value_type
    value() const;

    static MPTAmount
    minPositiveAmount();
};

constexpr MPTAmount::MPTAmount(value_type value) : value_(value)
{
}

constexpr MPTAmount&
MPTAmount::operator=(beast::Zero)
{
    value_ = 0;
    return *this;
}

/** Returns true if the amount is not zero */
constexpr MPTAmount::operator bool() const noexcept
{
    return value_ != 0;
}

/** Return the sign of the amount */
constexpr int
MPTAmount::signum() const noexcept
{
    return (value_ < 0) ? -1 : (value_ ? 1 : 0);
}

/** Returns the underlying value. Code SHOULD NOT call this
    function unless the type has been abstracted away,
    e.g. in a templated function.
*/
constexpr MPTAmount::value_type
MPTAmount::value() const
{
    return value_;
}

inline std::string
to_string(MPTAmount const& amount)
{
    return std::to_string(amount.value());
}

inline MPTAmount
mulRatio(
    MPTAmount const& amt,
    std::uint32_t num,
    std::uint32_t den,
    bool roundUp)
{
    using namespace boost::multiprecision;

    if (!den)
        Throw<std::runtime_error>("division by zero");

    int128_t const amt128(amt.value());
    auto const neg = amt.value() < 0;
    auto const m = amt128 * num;
    auto r = m / den;
    if (m % den)
    {
        if (!neg && roundUp)
            r += 1;
        if (neg && !roundUp)
            r -= 1;
    }
    if (r > std::numeric_limits<MPTAmount::value_type>::max())
        Throw<std::overflow_error>("MPT mulRatio overflow");
    return MPTAmount(r.convert_to<MPTAmount::value_type>());
}

}  // namespace ripple

#endif  // XRPL_BASICS_MPTAMOUNT_H_INCLUDED
