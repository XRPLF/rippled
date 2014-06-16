//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_AMOUNT_H_INCLUDED
#define RIPPLE_CORE_AMOUNT_H_INCLUDED

#include <ripple/module/data/protocol/SerializedObject.h>

#include <beast/utility/noexcept.h>
#include <beast/cxx14/type_traits.h> // <type_traits>

namespace ripple {
namespace core {

/** Custom floating point asset amount.
    The "representation" may be integral or non-integral. For integral
    representations, the exponent is always zero and the value held in the
    mantissa is an exact quantity.
*/
class AmountType
{
private:
    std::uint64_t m_mantissa;
    int m_exponent;
    bool m_negative;
    bool m_integral;

    AmountType (std::uint64_t mantissa,
        int exponent, bool negative, bool integral)
        : m_mantissa (mantissa)
        , m_exponent (exponent)
        , m_negative (negative)
        , m_integral (integral)
    {
    }

public:
    /** Default construction.
        The value is uninitialized.
    */
    AmountType() noexcept
    {
    }

    /** Construct from an integer.
        The representation is set to integral.
    */
    /** @{ */
    template <class Integer>
    AmountType (Integer value,
        std::enable_if_t <std::is_signed <Integer>::value>* = 0) noexcept
        : m_mantissa (value)
        , m_exponent (0)
        , m_negative (value < 0)
        , m_integral (true)
    {
        static_assert (std::is_integral<Integer>::value,
            "Cannot construct from non-integral type.");
    }

    template <class Integer>
    AmountType (Integer value,
        std::enable_if_t <! std::is_signed <Integer>::value>* = 0) noexcept
        : m_mantissa (value)
        , m_exponent (0)
        , m_negative (false)
    {
        static_assert (std::is_integral<Integer>::value,
            "Cannot construct from non-integral type.");
    }
    /** @} */

    /** Assign the value zero.
        The representation is preserved.
    */
    AmountType&
    operator= (Zero) noexcept
    {
        m_mantissa = 0;
        // VFALCO Why -100?
        //        "We have to use something in range."
        //        "This makes zero the smallest value."
        m_exponent = m_integral ? 0 : -100;
            m_exponent = 0;
        m_negative = false;
        return *this;
    }

    /** Returns the value in canonical format. */
    AmountType
    normal() const noexcept
    {
        if (m_integral)
        {
            AmountType result;
            if (m_mantissa == 0)
            {
                result.m_exponent = 0;
                result.m_negative = false;
            }
            return result;
        }
        return AmountType();
    }

    //
    // Comparison
    //

    int
    signum() const noexcept
    {
        if (m_mantissa == 0)
            return 0;
        return m_negative ? -1 : 1;
    }

    bool
    operator== (AmountType const& other) const noexcept
    {
        return
            m_negative == other.m_negative &&
            m_mantissa == other.m_mantissa &&
            m_exponent == other.m_exponent;
    }

    bool
    operator!= (AmountType const& other) const noexcept
    {
        return ! (*this == other);
    }

    bool
    operator< (AmountType const& other) const noexcept
    {
        return false;
    }

    bool
    operator>= (AmountType const& other) const noexcept
    {
        return ! (*this < other);
    }

    bool
    operator> (AmountType const& other) const noexcept
    {
        return other < *this;
    }

    bool
    operator<= (AmountType const& other) const noexcept
    {
        return ! (other < *this);
    }

    //
    // Arithmetic
    //

    AmountType
    operator-() const noexcept
    {
        return AmountType (m_mantissa, m_exponent, ! m_negative, m_integral);
    }

    //
    // Output
    //

    std::ostream&
    operator<< (std::ostream& os)
    {
        int const sig (signum());

        if (sig == 0)
            return os << "0";
        
        if (sig < 0)
            os << "-";
        if (m_integral)
            return os << m_mantissa;
        if (m_exponent != 0 && (m_exponent < -25 || m_exponent > -5))
            return os << m_mantissa << "e" << m_exponent;

        return os;
    }
};

//------------------------------------------------------------------------------

typedef STAmount Amount;

}
}

#endif
