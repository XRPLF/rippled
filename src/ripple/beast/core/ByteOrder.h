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

#include <ripple/beast/core/Config.h>

#include <cstdint>

namespace beast {

//==============================================================================
/** Contains static methods for converting the byte order between different
    endiannesses.
*/
class ByteOrder
{
public:
    //==============================================================================
    /** Swaps the upper and lower bytes of a 16-bit integer. */
    static std::uint16_t swap (std::uint16_t value);

    /** Reverses the order of the 4 bytes in a 32-bit integer. */
    static std::uint32_t swap (std::uint32_t value);

    /** Reverses the order of the 8 bytes in a 64-bit integer. */
    static std::uint64_t swap (std::uint64_t value);

    //==============================================================================
    /** Swaps the byte order of a 16-bit int if the CPU is big-endian */
    static std::uint16_t swapIfBigEndian (std::uint16_t value);

    /** Swaps the byte order of a 32-bit int if the CPU is big-endian */
    static std::uint32_t swapIfBigEndian (std::uint32_t value);

    /** Swaps the byte order of a 64-bit int if the CPU is big-endian */
    static std::uint64_t swapIfBigEndian (std::uint64_t value);

    /** Swaps the byte order of a 16-bit int if the CPU is little-endian */
    static std::uint16_t swapIfLittleEndian (std::uint16_t value);

    /** Swaps the byte order of a 32-bit int if the CPU is little-endian */
    static std::uint32_t swapIfLittleEndian (std::uint32_t value);

    /** Swaps the byte order of a 64-bit int if the CPU is little-endian */
    static std::uint64_t swapIfLittleEndian (std::uint64_t value);

    //==============================================================================
    /** Turns 2 bytes into a little-endian integer. */
    static std::uint16_t littleEndianShort (const void* bytes);

    /** Turns 4 bytes into a little-endian integer. */
    static std::uint32_t littleEndianInt (const void* bytes);

    /** Turns 4 bytes into a little-endian integer. */
    static std::uint64_t littleEndianInt64 (const void* bytes);

    /** Turns 2 bytes into a big-endian integer. */
    static std::uint16_t bigEndianShort (const void* bytes);

    /** Turns 4 bytes into a big-endian integer. */
    static std::uint32_t bigEndianInt (const void* bytes);

    /** Turns 4 bytes into a big-endian integer. */
    static std::uint64_t bigEndianInt64 (const void* bytes);

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
    ByteOrder(ByteOrder const&) = delete;
    ByteOrder& operator= (ByteOrder const&) = delete;
};

//==============================================================================
#if BEAST_USE_INTRINSICS && ! defined (__INTEL_COMPILER)
 #pragma intrinsic (_byteswap_ulong)
#endif

inline std::uint16_t ByteOrder::swap (std::uint16_t n)
{
   #if BEAST_USE_INTRINSICSxxx // agh - the MS compiler has an internal error when you try to use this intrinsic!
    return static_cast <std::uint16_t> (_byteswap_ushort (n));
   #else
    return static_cast <std::uint16_t> ((n << 8) | (n >> 8));
   #endif
}

inline std::uint32_t ByteOrder::swap (std::uint32_t n)
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

inline std::uint64_t ByteOrder::swap (std::uint64_t value)
{
   #if BEAST_MAC || BEAST_IOS
    return OSSwapInt64 (value);
   #elif BEAST_USE_INTRINSICS
    return _byteswap_uint64 (value);
   #else
    return (((std::int64_t) swap ((std::uint32_t) value)) << 32) | swap ((std::uint32_t) (value >> 32));
   #endif
}

#if BEAST_LITTLE_ENDIAN
 inline std::uint16_t ByteOrder::swapIfBigEndian (const std::uint16_t v)                                  { return v; }
 inline std::uint32_t ByteOrder::swapIfBigEndian (const std::uint32_t v)                                  { return v; }
 inline std::uint64_t ByteOrder::swapIfBigEndian (const std::uint64_t v)                                  { return v; }
 inline std::uint16_t ByteOrder::swapIfLittleEndian (const std::uint16_t v)                               { return swap (v); }
 inline std::uint32_t ByteOrder::swapIfLittleEndian (const std::uint32_t v)                               { return swap (v); }
 inline std::uint64_t ByteOrder::swapIfLittleEndian (const std::uint64_t v)                               { return swap (v); }
 inline std::uint16_t ByteOrder::littleEndianShort (const void* const bytes)                       { return *static_cast <const std::uint16_t*> (bytes); }
 inline std::uint32_t ByteOrder::littleEndianInt (const void* const bytes)                         { return *static_cast <const std::uint32_t*> (bytes); }
 inline std::uint64_t ByteOrder::littleEndianInt64 (const void* const bytes)                       { return *static_cast <const std::uint64_t*> (bytes); }
 inline std::uint16_t ByteOrder::bigEndianShort (const void* const bytes)                          { return swap (*static_cast <const std::uint16_t*> (bytes)); }
 inline std::uint32_t ByteOrder::bigEndianInt (const void* const bytes)                            { return swap (*static_cast <const std::uint32_t*> (bytes)); }
 inline std::uint64_t ByteOrder::bigEndianInt64 (const void* const bytes)                          { return swap (*static_cast <const std::uint64_t*> (bytes)); }
 inline bool ByteOrder::isBigEndian()                                                       { return false; }
#else
 inline std::uint16_t ByteOrder::swapIfBigEndian (const std::uint16_t v)                                  { return swap (v); }
 inline std::uint32_t ByteOrder::swapIfBigEndian (const std::uint32_t v)                                  { return swap (v); }
 inline std::uint64_t ByteOrder::swapIfBigEndian (const std::uint64_t v)                                  { return swap (v); }
 inline std::uint16_t ByteOrder::swapIfLittleEndian (const std::uint16_t v)                               { return v; }
 inline std::uint32_t ByteOrder::swapIfLittleEndian (const std::uint32_t v)                               { return v; }
 inline std::uint64_t ByteOrder::swapIfLittleEndian (const std::uint64_t v)                               { return v; }
 inline std::uint32_t ByteOrder::littleEndianInt (const void* const bytes)                         { return swap (*static_cast <const std::uint32_t*> (bytes)); }
 inline std::uint16_t ByteOrder::littleEndianShort (const void* const bytes)                       { return swap (*static_cast <const std::uint16_t*> (bytes)); }
 inline std::uint16_t ByteOrder::bigEndianShort (const void* const bytes)                          { return *static_cast <const std::uint16_t*> (bytes); }
 inline std::uint32_t ByteOrder::bigEndianInt (const void* const bytes)                            { return *static_cast <const std::uint32_t*> (bytes); }
 inline std::uint64_t ByteOrder::bigEndianInt64 (const void* const bytes)                          { return *static_cast <const std::uint64_t*> (bytes); }
 inline bool ByteOrder::isBigEndian()                                                       { return true; }
#endif

inline int  ByteOrder::littleEndian24Bit (const char* const bytes)                          { return (((int) bytes[2]) << 16) | (((int) (std::uint8_t) bytes[1]) << 8) | ((int) (std::uint8_t) bytes[0]); }
inline int  ByteOrder::bigEndian24Bit (const char* const bytes)                             { return (((int) bytes[0]) << 16) | (((int) (std::uint8_t) bytes[1]) << 8) | ((int) (std::uint8_t) bytes[2]); }
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
    explicit SwapBytes() = default;

    inline IntegralType operator() (IntegralType value) const noexcept
    {
        return ByteOrder::swap (value);
    }
};

// Specializations for signed integers

template <>
struct SwapBytes <std::int16_t>
{
    explicit SwapBytes() = default;

    inline std::int16_t operator() (std::int16_t value) const noexcept
    {
        return static_cast <std::int16_t> (ByteOrder::swap (static_cast <std::uint16_t> (value)));
    }
};

template <>
struct SwapBytes <std::int32_t>
{
    explicit SwapBytes() = default;

    inline std::int32_t operator() (std::int32_t value) const noexcept
    {
        return static_cast <std::int32_t> (ByteOrder::swap (static_cast <std::uint32_t> (value)));
    }
};

template <>
struct SwapBytes <std::int64_t>
{
    explicit SwapBytes() = default;

    inline std::int64_t operator() (std::int64_t value) const noexcept
    {
        return static_cast <std::int64_t> (ByteOrder::swap (static_cast <std::uint64_t> (value)));
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
