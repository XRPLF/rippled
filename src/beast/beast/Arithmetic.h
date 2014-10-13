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
    // if these are in the wrong order, results are unpredictable.
    bassert (lowerLimit <= upperLimit);

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

}

#endif

