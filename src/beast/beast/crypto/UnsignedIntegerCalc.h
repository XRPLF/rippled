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

#ifndef BEAST_CRYPTO_UNSIGNEDINTEGERCALC_H_INCLUDED
#define BEAST_CRYPTO_UNSIGNEDINTEGERCALC_H_INCLUDED

#include <beast/ByteOrder.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>

namespace beast {

namespace detail {

template <typename UInt>
struct DoubleWidthUInt;

template <>
struct DoubleWidthUInt <std::uint16_t>
{
    typedef std::uint32_t type;
};

template <>
struct DoubleWidthUInt <std::uint32_t>
{
    typedef std::uint64_t type;
};

}

/** Multiprecision unsigned integer suitable for calculations.

    The data is stored in "calculation" format, which means it can be
    readily used for performing calculations, but no raw access to the
    bytes are provided. To transmit a serialized unsigned integer or
    perform base encodings, it must be converted back into UnsignedInteger.
    The number is represented as a series of native UInt unsigned integer
    types, in order of increasing significance.

    This is a lightweight object, storage and ownership of the underlying
    data buffer is an external responsibility. The makes the class cheap to
    copy and pass by value.

    A consequence of this ownership model is that arithmetics operators
    which return results by value cannot be included in the interface.
*/
template <typename UInt>
class UnsignedIntegerCalc
{
public:
    typedef typename detail::DoubleWidthUInt <UInt>::type UIntBig;

    typedef std::size_t     size_type;

    static UInt const       maxUInt = ((UInt)(-1));
    static size_type const  numBits = (sizeof(UInt)*8);

    //--------------------------------------------------------------------------

    /** Construct an empty integer / zero bits. */
    UnsignedIntegerCalc ()
        : m_size (0)
        , m_values (nullptr)
    {
    }

    /** Construct a reference to an existing buffer. */
    UnsignedIntegerCalc (UnsignedIntegerCalc const& other)
        : m_size (other.m_size)
        , m_values (other.m_values)
    {
    }

    /** Construct from an existing array of values.
        The existing data must already be in the "calculation" format.
    */
    UnsignedIntegerCalc (size_type count, UInt* values)
        : m_size (count)
        , m_values (values)
    {
    }

    /** Convert to calculation format from canonical format.
        This overwrites the callers memory without transferring ownership.
        Canonical format is defined as a big endian byte oriented
        multiprecision integer format. The buffer should point to the
        beginning of the storage area and not the beginning of the canonical
        data. Bytes is the desired canonical bytes.
    */
    static UnsignedIntegerCalc fromCanonical (
        void* buffer, size_type const bytes, bool swizzle = true)
    {
        UInt* const values (reinterpret_cast <UInt*> (buffer));
        size_type const count ((bytes + sizeof (UInt) - 1) / sizeof (UInt));
        if (swizzle)
        {
            // Zero fill the possibly uninitialized pad bytes
            std::memset (buffer, 0,
                ((sizeof(UInt)-(bytes&(sizeof(UInt)-1)))&(sizeof(UInt)-1)));
            // Swap and swizzle
            UInt* lo (values);
            UInt* hi (values + count - 1);
            while (lo < hi)
            {
                std::swap (*lo, *hi);
                *lo = fromNetworkByteOrder <UInt> (*lo);
                ++lo;
                *hi = fromNetworkByteOrder <UInt> (*hi);
                ++hi;
            }
            if (lo == hi)
                *lo = fromNetworkByteOrder <UInt> (*lo);
        }
        return UnsignedIntegerCalc (count, values);
    }

    /** Convert the buffer back into canonical format.
        Since ownership was never transferred, the caller's data is
        restored to its original format. Typically this will be done
        as the last step of a series of operations.
    */
    void toCanonical ()
    {
        // Swap and swizzle
        UInt* lo (m_values);
        UInt* hi (m_values + m_size - 1);
        while (lo < hi)
        {
            std::swap (*lo, *hi);
            *lo = toNetworkByteOrder <UInt> (*lo); ++lo;
            *hi = toNetworkByteOrder <UInt> (*hi); --hi;
        }
        if (lo == hi)
            *lo = toNetworkByteOrder <UInt> (*lo);
    }

    /** Assign a value from another integer.
        @note This does not transfer the reference to the buffer, it
              copies the values from one buffer to the other.
    */
    UnsignedIntegerCalc& operator= (UnsignedIntegerCalc const& other)
    {
        assert (other.size() <= size());
        size_type n (size());
        UInt* dest (m_values + size());
        for (; n-- > other.size();)
            *--dest = 0;
        UInt const* rhs (other.m_values + n);
        for (; n--;)
            *--dest = *--rhs;
        return *this;
    }

    /** Returns `true` if this represents the number zero. */
    bool isZero () const
    {
        for (size_type n (size()); n--;)
            if (m_values [n] != 0)
                return false;
        return true;
    }

    /** Returns `true` if this represents any number other than zero. */
    bool isNotZero () const
    {
        return ! isZero ();
    }

    /** Safe conversion to `bool`, `true` means a non-zero value. */
    explicit
    operator bool() const
    {
        return isNotZero ();
    }

    /** Returns `true` if the buffer has 0 values. */
    bool empty () const
    {
        return m_size == 0;
    }

    /** Returns the size of the buffer, in values. */
    size_type size () const
    {
        return m_size;
    }

    /** Safe array indexing to arbitrary positions.
        If the index is out of range, zero is returned.
    */
    UInt operator[] (size_type n) const
    {
        if (n >= 0 && n < size())
            return m_values [n];
        return 0;
    }

    /** Universal comparison.
        The comparison is performed numerically.
        The return values have the same meaning as memcmp().
    */
    int compare (UnsignedIntegerCalc const& other) const
    {
        if (size() == 0)
        {
            if (other.size() == 0)
                return 0;
            return  -1;
        }
        else if (other.size() == 0)
        {
            return 1;
        }

        for (size_type n (std::max (size(), other.size())); n--;)
        {
            UInt lhs ((*this)[n]);
            UInt rhs (other[n]);
            if (lhs < rhs)
                return -1;
            else if (lhs > rhs)
                return 1;
        }

        return 0;
    }

    /** Determine equality. */
    bool operator== (UnsignedIntegerCalc const& other) const
    {
        return compare (other) == 0;
    }

    /** Determine inequality. */
    bool operator!= (UnsignedIntegerCalc const& other) const
    {
        return compare (other) != 0;
    }

    /** Ordered comparison. */
    bool operator< (UnsignedIntegerCalc const& other) const
    {
        return compare (other) < 0;
    }

    /** Ordered comparison. */
    bool operator<= (UnsignedIntegerCalc const& other) const
    {
        return compare (other) <= 0;
    }

    /** Ordered comparison. */
    bool operator> (UnsignedIntegerCalc const& other) const
    {
        return compare (other) > 0;
    }

    /** Ordered comparison. */
    bool operator>= (UnsignedIntegerCalc const& other) const
    {
        return compare (other) >= 0;
    }

    /** Assign zero. */
    void clear ()
    {
        UInt* dest (m_values - 1);
        for (size_type n (size()); n--;)
            *++dest = 0;
    }

    /** Perform bitwise logical-not. */
    /*
    UnsignedIntegerCalc& not ()
    {
        unaryAssign (Not());
        return *this;
    }
    */

    /** Perform bitwise logical-or. */
    UnsignedIntegerCalc& operator|= (UnsignedIntegerCalc const& rhs)
    {
        binaryAssign (rhs, Or());
        return *this;
    }

    /** Perform bitwise logical-and. */
    UnsignedIntegerCalc& operator&= (UnsignedIntegerCalc const& rhs)
    {
        binaryAssign (rhs, And());
        return *this;
    }

    /** Perform bitwise logical-xor. */
    UnsignedIntegerCalc& operator^= (UnsignedIntegerCalc const& rhs)
    {
        binaryAssign (rhs, Xor());
        return *this;
    }

    /** Perform addition. */
    UnsignedIntegerCalc& operator+= (UnsignedIntegerCalc const& v)
    {
        UIntBig carry (0);
        UInt* lhs (m_values);
        UInt const* rhs (v.m_values - 1);
        for (size_type n (0); n<size() || n<v.size(); ++n)
        {
            UIntBig part (carry);
            carry = 0;
            if (n < size())
                part += *lhs;
            if (n < v.size())
                part += *++rhs;
            if (part > maxUInt)
            {
                part &= maxUInt;
                carry = 1;
            }
            *lhs++ = UInt (part);
        }
        assert (carry == 0); // overflow
        return *this;
    }

    /** Perform small addition. */
    UnsignedIntegerCalc& operator+= (UInt rhs)
    {
        UnsignedIntegerCalc const v (1, &rhs);
        return operator+= (v);
    }

    /** Perform small multiply. */
    UnsignedIntegerCalc& operator*= (UInt rhs)
    {
        UIntBig carry (0);
        UInt* lhs (m_values - 1);
        for (size_type n (size()); n--;)
        {
            UIntBig part (carry);
            carry = 0;
            part += (*++lhs) * UIntBig(rhs);
            carry = part >> numBits;
            *lhs = UInt (part & maxUInt);
        }
        assert (carry == 0); // overflow
        return *this;
    }

    /** Small division. */
    UnsignedIntegerCalc operator/= (UInt rhs)
    {
        UIntBig dividend (0);
        UInt* lhs (m_values+size());
        for (size_type n (size()); n--;)
        {
            dividend |= *--lhs;
            *lhs = UInt (dividend / rhs);
            dividend = (dividend % rhs) << numBits;
        }
        return *this;
    }

    /** Small modulus. */
    UInt operator% (UInt rhs) const
    {
        UIntBig modsq = 1;
        UIntBig result = 0;
        UInt const* lhs (m_values);
        for (size_type n (size()); n--; ++lhs)
        {
            for (int bit (0); bit < numBits; ++bit)
            {
                if (((*lhs) & (1 << bit)) != 0)
                {
                    result += modsq;
                    if (result >= rhs)
                        result -= rhs;
                }
                modsq <<= 1;
                if (modsq >= rhs)
                    modsq -= rhs;
            }
        }
        return UInt (result);
    }

private:
    struct Not { void operator() (UInt& rv) const             { rv = ~rv; } };
    struct Or  { void operator() (UInt& lhs, UInt rhs) const { lhs |= rhs; } };
    struct And { void operator() (UInt& lhs, UInt rhs) const { lhs &= rhs; } };
    struct Xor { void operator() (UInt& lhs, UInt rhs) const { lhs ^= rhs; } };

    template <class Operator>
    void unaryAssign (Operator op = Operator())
    {
        UInt* dest (m_values-1);
        for (size_type n (size()); n--;)
            op (*++dest);
    }

    template <class Operator>
    void binaryAssign (UnsignedIntegerCalc const& other, Operator op = Operator ())
    {
        UInt* dest (m_values + size());
        size_type n (size());
        for (; n-- > other.size();)
            *--dest = 0;
        UInt const* rhs (other.m_values + n);
        for (UInt const* rhs (other.m_values + n); n--;)
            op (*--dest, *--rhs);
    }

    size_type m_size;
    UInt* m_values;
};

}

#endif
