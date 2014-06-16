//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_ARITHMETIC_H_INCLUDED
#define BEAST_ARITHMETIC_H_INCLUDED

#include <beast/Config.h>

#include <beast/utility/noexcept.h>

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace beast {

// Some indispensible min/max functions

/** Returns the larger of two values. */
template <typename Type>
inline Type bmax (const Type a, const Type b)
    { return (a < b) ? b : a; }

/** Returns the larger of three values. */
template <typename Type>
inline Type bmax (const Type a, const Type b, const Type c)
    { return (a < b) ? ((b < c) ? c : b) : ((a < c) ? c : a); }

/** Returns the larger of four values. */
template <typename Type>
inline Type bmax (const Type a, const Type b, const Type c, const Type d)
    { return bmax (a, bmax (b, c, d)); }

/** Returns the smaller of two values. */
template <typename Type>
inline Type bmin (const Type a, const Type b)
    { return (b < a) ? b : a; }

/** Returns the smaller of three values. */
template <typename Type>
inline Type bmin (const Type a, const Type b, const Type c)
    { return (b < a) ? ((c < b) ? c : b) : ((c < a) ? c : a); }

/** Returns the smaller of four values. */
template <typename Type>
inline Type bmin (const Type a, const Type b, const Type c, const Type d)
    { return bmin (a, bmin (b, c, d)); }

/** Scans an array of values, returning the minimum value that it contains. */
template <typename Type>
const Type findMinimum (const Type* data, int numValues)
{
    if (numValues <= 0)
        return Type();

    Type result (*data++);

    while (--numValues > 0) // (> 0 rather than >= 0 because we've already taken the first sample)
    {
        const Type& v = *data++;
        if (v < result)  result = v;
    }

    return result;
}

/** Scans an array of values, returning the maximum value that it contains. */
template <typename Type>
const Type findMaximum (const Type* values, int numValues)
{
    if (numValues <= 0)
        return Type();

    Type result (*values++);

    while (--numValues > 0) // (> 0 rather than >= 0 because we've already taken the first sample)
    {
        const Type& v = *values++;
        if (result < v)  result = v;
    }

    return result;
}

/** Scans an array of values, returning the minimum and maximum values that it contains. */
template <typename Type>
void findMinAndMax (const Type* values, int numValues, Type& lowest, Type& highest)
{
    if (numValues <= 0)
    {
        lowest = Type();
        highest = Type();
    }
    else
    {
        Type mn (*values++);
        Type mx (mn);

        while (--numValues > 0) // (> 0 rather than >= 0 because we've already taken the first sample)
        {
            const Type& v = *values++;

            if (mx < v)  mx = v;
            if (v < mn)  mn = v;
        }

        lowest = mn;
        highest = mx;
    }
}

//==============================================================================
/** Constrains a value to keep it within a given range.

    This will check that the specified value lies between the lower and upper bounds
    specified, and if not, will return the nearest value that would be in-range. Effectively,
    it's like calling bmax (lowerLimit, bmin (upperLimit, value)).

    Note that it expects that lowerLimit <= upperLimit. If this isn't true,
    the results will be unpredictable.

    @param lowerLimit           the minimum value to return
    @param upperLimit           the maximum value to return
    @param valueToConstrain     the value to try to return
    @returns    the closest value to valueToConstrain which lies between lowerLimit
                and upperLimit (inclusive)
    @see blimit0To, bmin, bmax
*/
template <typename Type>
inline Type blimit (const Type lowerLimit,
                    const Type upperLimit,
                    const Type valueToConstrain) noexcept
{
    bassert (lowerLimit <= upperLimit); // if these are in the wrong order, results are unpredictable..

    return (valueToConstrain < lowerLimit) ? lowerLimit
                                           : ((upperLimit < valueToConstrain) ? upperLimit
                                                                              : valueToConstrain);
}

/** Returns true if a value is at least zero, and also below a specified upper limit.
    This is basically a quicker way to write:
    @code valueToTest >= 0 && valueToTest < upperLimit
    @endcode
*/
template <typename Type>
inline bool isPositiveAndBelow (Type valueToTest, Type upperLimit) noexcept
{
    bassert (Type() <= upperLimit); // makes no sense to call this if the upper limit is itself below zero..
    return Type() <= valueToTest && valueToTest < upperLimit;
}

template <>
inline bool isPositiveAndBelow (const int valueToTest, const int upperLimit) noexcept
{
    bassert (upperLimit >= 0); // makes no sense to call this if the upper limit is itself below zero..
    return static_cast <unsigned int> (valueToTest) < static_cast <unsigned int> (upperLimit);
}

/** Returns true if a value is at least zero, and also less than or equal to a specified upper limit.
    This is basically a quicker way to write:
    @code valueToTest >= 0 && valueToTest <= upperLimit
    @endcode
*/
template <typename Type>
inline bool isPositiveAndNotGreaterThan (Type valueToTest, Type upperLimit) noexcept
{
    bassert (Type() <= upperLimit); // makes no sense to call this if the upper limit is itself below zero..
    return Type() <= valueToTest && valueToTest <= upperLimit;
}

template <>
inline bool isPositiveAndNotGreaterThan (const int valueToTest, const int upperLimit) noexcept
{
    bassert (upperLimit >= 0); // makes no sense to call this if the upper limit is itself below zero..
    return static_cast <unsigned int> (valueToTest) <= static_cast <unsigned int> (upperLimit);
}

//==============================================================================

/** Handy function for getting the number of elements in a simple const C array.
    E.g.
    @code
    static int myArray[] = { 1, 2, 3 };

    int numElements = numElementsInArray (myArray) // returns 3
    @endcode
*/
template <typename Type, int N>
int numElementsInArray (Type (&array)[N])
{
    (void) array; // (required to avoid a spurious warning in MS compilers)
    (void) sizeof (0[array]); // This line should cause an error if you pass an object with a user-defined subscript operator
    return N;
}

/** 64-bit abs function. */
inline std::int64_t abs64 (const std::int64_t n) noexcept
{
    return (n >= 0) ? n : -n;
}


//==============================================================================
#if BEAST_MSVC
 #pragma optimize ("t", off)
 #ifndef __INTEL_COMPILER
  #pragma float_control (precise, on, push)
 #endif
#endif

/** Fast floating-point-to-integer conversion.

    This is faster than using the normal c++ cast to convert a float to an int, and
    it will round the value to the nearest integer, rather than rounding it down
    like the normal cast does.

    Note that this routine gets its speed at the expense of some accuracy, and when
    rounding values whose floating point component is exactly 0.5, odd numbers and
    even numbers will be rounded up or down differently.
*/
template <typename FloatType>
inline int roundToInt (const FloatType value) noexcept
{
  #ifdef __INTEL_COMPILER
   #pragma float_control (precise, on, push)
  #endif

    union { int asInt[2]; double asDouble; } n;
    n.asDouble = ((double) value) + 6755399441055744.0;

   #if BEAST_BIG_ENDIAN
    return n.asInt [1];
   #else
    return n.asInt [0];
   #endif
}

#if BEAST_MSVC
 #ifndef __INTEL_COMPILER
  #pragma float_control (pop)
 #endif
 #pragma optimize ("", on)  // resets optimisations to the project defaults
#endif

}

#endif

