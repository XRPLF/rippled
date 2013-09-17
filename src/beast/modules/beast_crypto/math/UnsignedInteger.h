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

/** Represents a set of bits of fixed size.

    The data is stored in "canonical" format which is network (big endian)
    byte order, most significant byte first.

    In this implementation the pointer to the beginning of the canonical format
    may not be aligned.
*/
template <std::size_t Bytes>
class UnsignedInteger : public SafeBool <UnsignedInteger <Bytes> > 
{
public:
    enum
    {
        /** Constant for determining the number of bytes.
        */
        sizeInBytes = Bytes
    };

    // The underlying integer type we use when converting to calculation format.
    typedef uint32             IntCalcType;

    // The type of object resulting from a conversion to calculation format.
    typedef UnsignedIntegerCalc <IntCalcType> CalcType;

    // Standard container compatibility
    typedef uint8              ValueType;
    typedef ValueType*         iterator;
    typedef ValueType const*   const_iterator;

    /** Hardened hash function for use with HashMap.
        The seed is used to make the hash unpredictable. This prevents
        attackers from exploiting crafted inputs to produce degenerate
        containers.
        @see HashMap
    */
    class HashFunction
    {
    public:
        /** Construct a hash function
            If a seed is specified it will be used, else a random seed
            will be generated from the system
            @param seedToUse An optional seed to use.
        */
        explicit HashFunction (HashValue seedToUse = Random::getSystemRandom ().nextInt ())
            : m_seed (seedToUse)
        {
        }

        /** Generates a simple hash from an UnsignedInteger. */
        HashValue generateHash (UnsignedInteger const& key) const
        {
            HashValue hash;
            Murmur::Hash (key.cbegin (), key.sizeInBytes, m_seed, &hash);
            return hash;
        }

        HashValue operator() (UnsignedInteger const& key) const
        {
            HashValue hash;
            Murmur::Hash (key.cbegin (), key.sizeInBytes, m_seed, &hash);
            return hash;
        }

    private:
        HashValue m_seed;
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

    template <typename T>
    explicit UnsignedInteger (T const* buf)
    {
        m_values [0] = 0; // clear any pad bytes
        std::memcpy (begin(), buf, Bytes);
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
    template <class IntegerType>
    static UnsignedInteger createFromInteger (IntegerType value)
    {
        static_bassert (Bytes >= sizeof (IntegerType));
        UnsignedInteger <Bytes> result;
        value = toNetworkByteOrder <IntegerType> (value);
        result.clear ();
        std::memcpy (result.end () - sizeof (value), &value, bmin (Bytes, sizeof (value)));
        return result;
    }

    /** Construct with a filled value.
    */
    static UnsignedInteger createFilled (ValueType value)
    {
        UnsignedInteger result;
        result.fill (value);
        return result;
    }

    /** Fill with a particular byte value. */
    void fill (ValueType value)
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
        @see SafeBool
    */
    bool asBoolean () const
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

    ValueType* get ()
    {
        return (reinterpret_cast <ValueType*> (&m_values [0])) +
            ((sizeof(IntCalcType)-(Bytes&(sizeof(IntCalcType)-1)))&(sizeof(IntCalcType)-1));
    }

    ValueType const* get () const
    {
        return (reinterpret_cast <ValueType const*> (&m_values [0])) +
            ((sizeof(IntCalcType)-(Bytes&(sizeof(IntCalcType)-1)))&(sizeof(IntCalcType)-1));
    }

    IntCalcType m_values [CalcCount];
};

#endif
