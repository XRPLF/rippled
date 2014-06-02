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

#ifndef BEAST_CRYPTO_UNSIGNEDINTEGER_H_INCLUDED
#define BEAST_CRYPTO_UNSIGNEDINTEGER_H_INCLUDED

#include <beast/crypto/UnsignedIntegerCalc.h>
#include <beast/crypto/MurmurHash.h>

#include <beast/ByteOrder.h>
#include <beast/container/hardened_hash.h>

#include <beast/utility/noexcept.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>

namespace beast {

/** Represents a set of bits of fixed size.

    The data is stored in "canonical" format which is network (big endian)
    byte order, most significant byte first.

    In this implementation the pointer to the beginning of the canonical format
    may not be aligned.
*/
template <std::size_t Bytes>
class UnsignedInteger
{
public:
    /** Constant for determining the number of bytes. */
    static std::size_t const size = Bytes;

    // The underlying integer type we use when converting to calculation format.
    typedef std::uint32_t IntCalcType;

    // The type of object resulting from a conversion to calculation format.
    typedef UnsignedIntegerCalc <IntCalcType> CalcType;

    // Standard container compatibility
    typedef std::uint8_t value_type;
    typedef value_type*        iterator;
    typedef value_type const*  const_iterator;

    /** Hardened hash function for use with hash based containers.
        The seed is used to make the hash unpredictable. This prevents
        attackers from exploiting crafted inputs to produce degenerate
        containers.
    */
    typedef hardened_hash <UnsignedInteger> hasher;

    /** Determins if two UnsignedInteger objects are equal. */
    class equal
    {
    public:
        bool operator() (UnsignedInteger const& lhs, UnsignedInteger const& rhs) const
        {
            return lhs.compare (rhs) == 0;
        }
    };

    //--------------------------------------------------------------------------

    /** Construct the object.
        The values are uninitialized.
    */
    UnsignedInteger ()
    {
    }

    /** Construct a copy. */
    UnsignedInteger (UnsignedInteger const& other)
    {
        this->operator= (other);
    }

    /** Construct from a raw memory.
        The area pointed to by buffer must be at least Bytes in size,
        or else undefined behavior will result.
    */
    /** @{ */
    explicit UnsignedInteger (void const* buf)
    {
        m_values [0] = 0; // clear any pad bytes
        std::memcpy (begin(), buf, Bytes);
    }

    template <typename InputIt>
    UnsignedInteger (InputIt first, InputIt last)
    {
        m_values [0] = 0; // clear any pad bytes
        assert (std::distance (first, last) == size);
        std::copy (first, last, begin());
    }
    /** @} */

    /** Assign from another UnsignedInteger. */
    UnsignedInteger& operator= (UnsignedInteger const& other)
    {
        // Perform an aligned, all inclusive copy that includes padding.
        std::copy (other.m_values, other.m_values + CalcCount, m_values);
        return *this;
    }

    /** Create from an integer type.
        @invariant IntegerType must be an unsigned integer type.
    */
    template <class UnsignedIntegralType>
    static UnsignedInteger createFromInteger (UnsignedIntegralType value)
    {
        static_assert (Bytes >= sizeof (UnsignedIntegralType),
            "Bytes is too small.");
        UnsignedInteger <Bytes> result;
        value = toNetworkByteOrder <UnsignedIntegralType> (value);
        result.clear ();
        std::memcpy (result.end () - sizeof (value), &value,
            std::min (Bytes, sizeof (value)));
        return result;
    }

    /** Construct with a filled value. */
    static UnsignedInteger createFilled (value_type value)
    {
        UnsignedInteger result;
        result.fill (value);
        return result;
    }

    /** Fill with a particular byte value. */
    void fill (value_type value)
    {
        IntCalcType c;
        memset (&c, value, sizeof (c));
        std::fill (m_values, m_values + CalcCount, c);
    }

    /** Clear the contents to zero. */
    void clear ()
    {
        std::fill (m_values, m_values + CalcCount, 0);
    }

    /** Convert to calculation format. */
    CalcType toCalcType (bool convert = true)
    {
        return CalcType::fromCanonical (m_values, Bytes, convert);
    }

    /** Determine if all bits are zero. */
    bool isZero () const
    {
        for (int i = 0; i < CalcCount; ++i)
        {
            if (m_values [i] != 0)
                return false;
        }

        return true;
    }

    /** Determine if any bit is non-zero. */
    bool isNotZero () const
    {
        return ! isZero ();
    }

    /** Support conversion to `bool`.
        @return `true` if any bit is non-zero.
    */
    explicit
    operator bool() const
    {
        return isNotZero ();
    }

    /** Get an iterator to the beginning. */
    iterator begin ()
    {
        return get();
    }

    /** Get an iterator to past-the-end. */
    iterator end ()
    {
        return get()+Bytes;
    }

    /** Get a const iterator to the beginning. */
    const_iterator begin () const
    {
        return get();
    }

    /** Get a const iterator to past-the-end. */
    const_iterator end () const
    {
        return get()+Bytes;
    }

    /** Get a const iterator to the beginning. */
    const_iterator cbegin () const
    {
        return get();
    }

    /** Get a const iterator to past-the-end. */
    const_iterator cend () const
    {
        return get()+Bytes;
    }

    /** Compare two objects of equal size.
        The comparison is performed using a numeric lexicographical comparison.
    */
    int compare (UnsignedInteger const& other) const
    {
        return memcmp (cbegin (), other.cbegin (), Bytes);
    }

    /** Determine equality. */
    bool operator== (UnsignedInteger const& other) const
    {
        return compare (other) == 0;
    }

    /** Determine inequality. */
    bool operator!= (UnsignedInteger const& other) const
    {
        return compare (other) != 0;
    }

    /** Ordered comparison. */
    bool operator< (UnsignedInteger const& other) const
    {
        return compare (other) < 0;
    }

    /** Ordered comparison. */
    bool operator<= (UnsignedInteger const& other) const
    {
        return compare (other) <= 0;
    }

    /** Ordered comparison. */
    bool operator> (UnsignedInteger const& other) const
    {
        return compare (other) > 0;
    }

    /** Ordered comparison. */
    bool operator>= (UnsignedInteger const& other) const
    {
        return compare (other) >= 0;
    }

private:
    static std::size_t const CalcCount = (Bytes + sizeof (IntCalcType) - 1) / sizeof (IntCalcType);

    value_type* get ()
    {
        return (reinterpret_cast <value_type*> (&m_values [0])) +
            ((sizeof(IntCalcType)-(Bytes&(sizeof(IntCalcType)-1)))&(sizeof(IntCalcType)-1));
    }

    value_type const* get () const
    {
        return (reinterpret_cast <value_type const*> (&m_values [0])) +
            ((sizeof(IntCalcType)-(Bytes&(sizeof(IntCalcType)-1)))&(sizeof(IntCalcType)-1));
    }

    IntCalcType m_values [CalcCount];
};

}

#endif
