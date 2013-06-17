/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_MATH_BEASTHEADER
#define BEAST_MATH_BEASTHEADER

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

#endif
