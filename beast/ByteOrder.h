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

#ifndef BEAST_BYTEORDER_H_INCLUDED
#define BEAST_BYTEORDER_H_INCLUDED

#include "Config.h"
#include "CStdInt.h"
#include "Uncopyable.h"

namespace beast {

//==============================================================================
/** Contains static methods for converting the byte order between different
    endiannesses.
*/
class ByteOrder : public Uncopyable
{
public:
    //==============================================================================
    /** Swaps the upper and lower bytes of a 16-bit integer. */
    static uint16 swap (uint16 value);

    /** Reverses the order of the 4 bytes in a 32-bit integer. */
    static uint32 swap (uint32 value);

    /** Reverses the order of the 8 bytes in a 64-bit integer. */
    static uint64 swap (uint64 value);

    //==============================================================================
    /** Swaps the byte order of a 16-bit int if the CPU is big-endian */
    static uint16 swapIfBigEndian (uint16 value);

    /** Swaps the byte order of a 32-bit int if the CPU is big-endian */
    static uint32 swapIfBigEndian (uint32 value);

    /** Swaps the byte order of a 64-bit int if the CPU is big-endian */
    static uint64 swapIfBigEndian (uint64 value);

    /** Swaps the byte order of a 16-bit int if the CPU is little-endian */
    static uint16 swapIfLittleEndian (uint16 value);

    /** Swaps the byte order of a 32-bit int if the CPU is little-endian */
    static uint32 swapIfLittleEndian (uint32 value);

    /** Swaps the byte order of a 64-bit int if the CPU is little-endian */
    static uint64 swapIfLittleEndian (uint64 value);

    //==============================================================================
    /** Turns 2 bytes into a little-endian integer. */
    static uint16 littleEndianShort (const void* bytes);

    /** Turns 4 bytes into a little-endian integer. */
    static uint32 littleEndianInt (const void* bytes);

    /** Turns 4 bytes into a little-endian integer. */
    static uint64 littleEndianInt64 (const void* bytes);

    /** Turns 2 bytes into a big-endian integer. */
    static uint16 bigEndianShort (const void* bytes);

    /** Turns 4 bytes into a big-endian integer. */
    static uint32 bigEndianInt (const void* bytes);

    /** Turns 4 bytes into a big-endian integer. */
    static uint64 bigEndianInt64 (const void* bytes);

    //==============================================================================
    /** Converts 3 little-endian bytes into a signed 24-bit value (which is sign-extended to 32 bits). */
    static int littleEndian24Bit (const char* bytes);

    /** Converts 3 big-endian bytes into a signed 24-bit value (which is sign-extended to 32 bits). */
    static int bigEndian24Bit (const char* bytes);

    /** Copies a 24-bit number to 3 little-endian bytes. */
    static void littleEndian24BitToChars (int value, char* destBytes);

    /** Copies a 24-bit number to 3 big-endian bytes. */
    static void bigEndian24BitToChars (int value, char* destBytes);

    //==============================================================================
    /** Returns true if the current CPU is big-endian. */
    static bool isBigEndian();

private:
    ByteOrder();
};

//==============================================================================
#if BEAST_USE_INTRINSICS && ! defined (__INTEL_COMPILER)
 #pragma intrinsic (_byteswap_ulong)
#endif

inline uint16 ByteOrder::swap (uint16 n)
{
   #if BEAST_USE_INTRINSICSxxx // agh - the MS compiler has an internal error when you try to use this intrinsic!
    return static_cast <uint16> (_byteswap_ushort (n));
   #else
    return static_cast <uint16> ((n << 8) | (n >> 8));
   #endif
}

inline uint32 ByteOrder::swap (uint32 n)
{
   #if BEAST_MAC || BEAST_IOS
    return OSSwapInt32 (n);
   #elif BEAST_GCC && BEAST_INTEL && ! BEAST_NO_INLINE_ASM
    asm("bswap %%eax" : "=a"(n) : "a"(n));
    return n;
   #elif BEAST_USE_INTRINSICS
    return _byteswap_ulong (n);
   #elif BEAST_MSVC && ! BEAST_NO_INLINE_ASM
    __asm {
        mov eax, n
        bswap eax
        mov n, eax
    }
    return n;
   #elif BEAST_ANDROID
    return bswap_32 (n);
   #else
    return (n << 24) | (n >> 24) | ((n & 0xff00) << 8) | ((n & 0xff0000) >> 8);
   #endif
}

inline uint64 ByteOrder::swap (uint64 value)
{
   #if BEAST_MAC || BEAST_IOS
    return OSSwapInt64 (value);
   #elif BEAST_USE_INTRINSICS
    return _byteswap_uint64 (value);
   #else
    return (((int64) swap ((uint32) value)) << 32) | swap ((uint32) (value >> 32));
   #endif
}

#if BEAST_LITTLE_ENDIAN
 inline uint16 ByteOrder::swapIfBigEndian (const uint16 v)                                  { return v; }
 inline uint32 ByteOrder::swapIfBigEndian (const uint32 v)                                  { return v; }
 inline uint64 ByteOrder::swapIfBigEndian (const uint64 v)                                  { return v; }
 inline uint16 ByteOrder::swapIfLittleEndian (const uint16 v)                               { return swap (v); }
 inline uint32 ByteOrder::swapIfLittleEndian (const uint32 v)                               { return swap (v); }
 inline uint64 ByteOrder::swapIfLittleEndian (const uint64 v)                               { return swap (v); }
 inline uint16 ByteOrder::littleEndianShort (const void* const bytes)                       { return *static_cast <const uint16*> (bytes); }
 inline uint32 ByteOrder::littleEndianInt (const void* const bytes)                         { return *static_cast <const uint32*> (bytes); }
 inline uint64 ByteOrder::littleEndianInt64 (const void* const bytes)                       { return *static_cast <const uint64*> (bytes); }
 inline uint16 ByteOrder::bigEndianShort (const void* const bytes)                          { return swap (*static_cast <const uint16*> (bytes)); }
 inline uint32 ByteOrder::bigEndianInt (const void* const bytes)                            { return swap (*static_cast <const uint32*> (bytes)); }
 inline uint64 ByteOrder::bigEndianInt64 (const void* const bytes)                          { return swap (*static_cast <const uint64*> (bytes)); }
 inline bool ByteOrder::isBigEndian()                                                       { return false; }
#else
 inline uint16 ByteOrder::swapIfBigEndian (const uint16 v)                                  { return swap (v); }
 inline uint32 ByteOrder::swapIfBigEndian (const uint32 v)                                  { return swap (v); }
 inline uint64 ByteOrder::swapIfBigEndian (const uint64 v)                                  { return swap (v); }
 inline uint16 ByteOrder::swapIfLittleEndian (const uint16 v)                               { return v; }
 inline uint32 ByteOrder::swapIfLittleEndian (const uint32 v)                               { return v; }
 inline uint64 ByteOrder::swapIfLittleEndian (const uint64 v)                               { return v; }
 inline uint32 ByteOrder::littleEndianInt (const void* const bytes)                         { return swap (*static_cast <const uint32*> (bytes)); }
 inline uint16 ByteOrder::littleEndianShort (const void* const bytes)                       { return swap (*static_cast <const uint16*> (bytes)); }
 inline uint16 ByteOrder::bigEndianShort (const void* const bytes)                          { return *static_cast <const uint16*> (bytes); }
 inline uint32 ByteOrder::bigEndianInt (const void* const bytes)                            { return *static_cast <const uint32*> (bytes); }
 inline uint64 ByteOrder::bigEndianInt64 (const void* const bytes)                          { return *static_cast <const uint64*> (bytes); }
 inline bool ByteOrder::isBigEndian()                                                       { return true; }
#endif

inline int  ByteOrder::littleEndian24Bit (const char* const bytes)                          { return (((int) bytes[2]) << 16) | (((int) (uint8) bytes[1]) << 8) | ((int) (uint8) bytes[0]); }
inline int  ByteOrder::bigEndian24Bit (const char* const bytes)                             { return (((int) bytes[0]) << 16) | (((int) (uint8) bytes[1]) << 8) | ((int) (uint8) bytes[2]); }
inline void ByteOrder::littleEndian24BitToChars (const int value, char* const destBytes)    { destBytes[0] = (char)(value & 0xff); destBytes[1] = (char)((value >> 8) & 0xff); destBytes[2] = (char)((value >> 16) & 0xff); }
inline void ByteOrder::bigEndian24BitToChars (const int value, char* const destBytes)       { destBytes[0] = (char)((value >> 16) & 0xff); destBytes[1] = (char)((value >> 8) & 0xff); destBytes[2] = (char)(value & 0xff); }

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

}

#endif
