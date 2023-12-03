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

#ifndef RIPPLE_BASICS_INTEGRALAMOUNT_H_INCLUDED
#define RIPPLE_BASICS_INTEGRALAMOUNT_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/utility/Zero.h>
#include <ripple/json/json_value.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/operators.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

namespace ripple {

class CFTAmount : private boost::totally_ordered<CFTAmount>,
                  private boost::additive<CFTAmount>,
                  private boost::equality_comparable<CFTAmount, std::int64_t>,
                  private boost::additive<CFTAmount, std::int64_t>
{
public:
    using cft_type = std::int64_t;

protected:
    cft_type cft_;

public:
    CFTAmount() = default;
    constexpr CFTAmount(CFTAmount const& other) = default;
    constexpr CFTAmount&
    operator=(CFTAmount const& other) = default;

    constexpr CFTAmount(beast::Zero) : cft_(0)
    {
    }

    constexpr explicit CFTAmount(cft_type value) : cft_(value)
    {
    }

    constexpr CFTAmount& operator=(beast::Zero)
    {
        cft_ = 0;
        return *this;
    }

    CFTAmount&
    operator=(cft_type value)
    {
        cft_ = value;
        return *this;
    }

    constexpr CFTAmount
    operator*(cft_type const& rhs) const
    {
        return CFTAmount{cft_ * rhs};
    }

    friend constexpr CFTAmount
    operator*(cft_type lhs, CFTAmount const& rhs)
    {
        // multiplication is commutative
        return rhs * lhs;
    }

    CFTAmount&
    operator+=(CFTAmount const& other)
    {
        cft_ += other.cft();
        return *this;
    }

    CFTAmount&
    operator-=(CFTAmount const& other)
    {
        cft_ -= other.cft();
        return *this;
    }

    CFTAmount&
    operator+=(cft_type const& rhs)
    {
        cft_ += rhs;
        return *this;
    }

    CFTAmount&
    operator-=(cft_type const& rhs)
    {
        cft_ -= rhs;
        return *this;
    }

    CFTAmount&
    operator*=(cft_type const& rhs)
    {
        cft_ *= rhs;
        return *this;
    }

    CFTAmount
    operator-() const
    {
        return CFTAmount{-cft_};
    }

    bool
    operator==(CFTAmount const& other) const
    {
        return cft_ == other.cft_;
    }

    bool
    operator==(cft_type other) const
    {
        return cft_ == other;
    }

    bool
    operator<(CFTAmount const& other) const
    {
        return cft_ < other.cft_;
    }

    /** Returns true if the amount is not zero */
    explicit constexpr operator bool() const noexcept
    {
        return cft_ != 0;
    }

    /** Return the sign of the amount */
    constexpr int
    signum() const noexcept
    {
        return (cft_ < 0) ? -1 : (cft_ ? 1 : 0);
    }

    Json::Value
    jsonClipped() const
    {
        static_assert(
            std::is_signed_v<cft_type> && std::is_integral_v<cft_type>,
            "Expected CFTAmount to be a signed integral type");

        constexpr auto min = std::numeric_limits<Json::Int>::min();
        constexpr auto max = std::numeric_limits<Json::Int>::max();

        if (cft_ < min)
            return min;
        if (cft_ > max)
            return max;
        return static_cast<Json::Int>(cft_);
    }

    /** Returns the underlying value. Code SHOULD NOT call this
        function unless the type has been abstracted away,
        e.g. in a templated function.
    */
    constexpr cft_type
    cft() const
    {
        return cft_;
    }

    friend std::istream&
    operator>>(std::istream& s, CFTAmount& val)
    {
        s >> val.cft_;
        return s;
    }

    static CFTAmount
    minPositiveAmount()
    {
        return CFTAmount{1};
    }
};

// Output CFTAmount as just the value.
template <class Char, class Traits>
std::basic_ostream<Char, Traits>&
operator<<(std::basic_ostream<Char, Traits>& os, const CFTAmount& q)
{
    return os << q.cft();
}

inline std::string
to_string(CFTAmount const& amount)
{
    return std::to_string(amount.cft());
}

inline CFTAmount
mulRatio(
    CFTAmount const& amt,
    std::uint32_t num,
    std::uint32_t den,
    bool roundUp)
{
    using namespace boost::multiprecision;

    if (!den)
        Throw<std::runtime_error>("division by zero");

    int128_t const amt128(amt.cft());
    auto const neg = amt.cft() < 0;
    auto const m = amt128 * num;
    auto r = m / den;
    if (m % den)
    {
        if (!neg && roundUp)
            r += 1;
        if (neg && !roundUp)
            r -= 1;
    }
    if (r > std::numeric_limits<CFTAmount::cft_type>::max())
        Throw<std::overflow_error>("XRP mulRatio overflow");
    return CFTAmount(r.convert_to<CFTAmount::cft_type>());
}

}  // namespace ripple

#endif  // RIPPLE_BASICS_INTEGRALAMOUNT_H_INCLUDED
