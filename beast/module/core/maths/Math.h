//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_MATH_H_INCLUDED
#define BEAST_MATH_H_INCLUDED

namespace beast
{

//
// Miscellaneous mathematical calculations
//

// Calculate the bin for a value given the bin size.
// This correctly handles negative numbers. For example
// if value == -1 then calc_bin returns -1.
template <typename Ty>
inline Ty calc_bin (Ty value, int size)
{
    if (value >= 0)
        return value / size;
    else
        return (value - size + 1) / size;
}

// Given a number and a bin size, this returns the first
// corresponding value of the bin associated with the given number.
// It correctly handles negative numbers. For example,
// if value == -1 then calc_bin always returns -size
template <typename Ty>
inline Ty calc_bin_start (Ty value, int size)
{
    return calc_bin (value, size) * size;
}

template <class T>
inline T pi () noexcept
{
    return 3.14159265358979;
}

template <class T>
inline T twoPi () noexcept
{
    return 6.28318530717958;
}

template <class T>
inline T oneOverTwoPi () noexcept
{
    return 0.1591549430918955;
}

template <class T, class U>
inline T degreesToRadians (U degrees)
{
    return T (degrees * 0.0174532925199433);
}

template <class T, class U>
inline T radiansToDegrees (U radians)
{
    T deg = T (radians * U (57.29577951308238));

    if (deg < 0)
        deg += 360;

    return deg;
}

} // beast

#endif
