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

#ifndef BEAST_BYTESWAP_H_INCLUDED
#define BEAST_BYTESWAP_H_INCLUDED

namespace detail
{

/** Specialized helper class template for swapping bytes.
    
    Normally you won't use this directly, use the helper function
    byteSwap instead. You can specialize this class for your
    own user defined types, as was done for uint24.

    @see swapBytes, uint24
*/
template <typename IntegralType>
struct SwapBytes
{
    inline IntegralType operator() (IntegralType value) const noexcept 
    {
        return ByteOrder::swap (value);
    }
};

// Specializations for signed integers

template <>
struct SwapBytes <int16>
{
    inline int16 operator() (int16 value) const noexcept 
    {
        return static_cast <int16> (ByteOrder::swap (static_cast <uint16> (value)));
    }
};

template <>
struct SwapBytes <int32>
{
    inline int32 operator() (int32 value) const noexcept 
    {
        return static_cast <int32> (ByteOrder::swap (static_cast <uint32> (value)));
    }
};

template <>
struct SwapBytes <int64>
{
    inline int64 operator() (int64 value) const noexcept 
    {
        return static_cast <int64> (ByteOrder::swap (static_cast <uint64> (value)));
    }
};

}

//------------------------------------------------------------------------------

/** Returns a type with the bytes swapped.
    Little endian becomes big endian and vice versa. The underlying
    type must be an integral type or behave like one.
*/
template <class IntegralType>
inline IntegralType swapBytes (IntegralType value) noexcept
{
    return detail::SwapBytes <IntegralType> () (value);
}

/** Returns the machine byte-order value to little-endian byte order. */
template <typename IntegralType>
inline IntegralType toLittleEndian (IntegralType value) noexcept
{
#if BEAST_LITTLE_ENDIAN
    return value;
#else
    return swapBytes (value);
#endif
}

/** Returns the machine byte-order value to big-endian byte order. */
template <typename IntegralType>
inline IntegralType toBigEndian (IntegralType value) noexcept
{
#if BEAST_LITTLE_ENDIAN
    return swapBytes (value);
#else
    return value;
#endif
}

/** Returns the machine byte-order value to network byte order. */
template <typename IntegralType>
inline IntegralType toNetworkByteOrder (IntegralType value) noexcept
{
    return toBigEndian (value);
}

/** Converts from network byte order to machine byte order. */
template <typename IntegralType>
inline IntegralType fromNetworkByteOrder (IntegralType value) noexcept
{
#if BEAST_LITTLE_ENDIAN
    return swapBytes (value);
#else
    return value;
#endif
}

#endif
