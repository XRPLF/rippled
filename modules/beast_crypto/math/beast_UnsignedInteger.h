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

#ifndef BEAST_UNSIGNEDINTEGER_H_INCLUDED
#define BEAST_UNSIGNEDINTEGER_H_INCLUDED

/** Represents a set of bits of fixed size.
    Integer representations are stored in network / big endian byte order.
    @note The number of bits represented can only be a multiple of 8.
    @tparam Bytes The number of bytes of storage.
*/
template <size_t Bytes>
class UnsignedInteger : public SafeBool <UnsignedInteger <Bytes> > 
{
public:
    enum
    {
        /** Constant for determining the number of bytes.
        */
        sizeInBytes = Bytes
    };

    // Standard container compatibility
    typedef uint8               value_type;
    typedef value_type*         iterator;
    typedef value_type const*   const_iterator;

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
        HashValue generateHash (UnsignedInteger <Bytes> const& key) const noexcept
        {
            HashValue hash;
            Murmur::Hash (key.cbegin (), key.sizeInBytes, m_seed, &hash);
            return hash;
        }

        HashValue operator() (UnsignedInteger <Bytes> const& key) const noexcept
        {
            HashValue hash;
            Murmur::Hash (key.cbegin (), key.sizeInBytes, m_seed, &hash);
            return hash;
        }

    private:
        HashValue m_seed;
    };

    /** Construct the object.
        The values are uninitialized.
    */
    UnsignedInteger () noexcept
    {
    }

    /** Construct a copy. */
    UnsignedInteger (UnsignedInteger <Bytes> const& other) noexcept
    {
        this->operator= (other);
    }

    /** Construct from raw memory.
        The area pointed to by buffer must be at least Bytes in size,
        or else undefined behavior will result.
    */
    explicit UnsignedInteger (void const* buffer)
    {
        memcpy (m_byte, buffer, Bytes);
    }

    /** Assign from another value. */
    UnsignedInteger <Bytes>& operator= (UnsignedInteger const& other) noexcept
    {
        memcpy (m_byte, other.m_byte, Bytes);
        return *this;
    }

    /** Create from an integer type.

        @invariant IntegerType must be an unsigned integer type.
    */
    template <class IntegerType>
    static UnsignedInteger <Bytes> createFromInteger (IntegerType value)
    {
        static_bassert (Bytes >= sizeof (IntegerType));
        UnsignedInteger <Bytes> result;
        value = toNetworkByteOrder <IntegerType> (value);
        result.clear ();
        memcpy (result.end () - sizeof (value), &value, bmin (Bytes, sizeof (value)));
        return result;
    }

    /** Construct with a filled value.
    */
    static UnsignedInteger <Bytes> createFilled (value_type value)
    {
        UnsignedInteger <Bytes> result;
        result.fill (value);
        return result;
    }

    /** Fill with a particular byte value.
    */
    void fill (value_type value) noexcept
    {
        memset (m_byte, value, Bytes);
    }

    /** Clear the contents to zero.
    */
    void clear () noexcept
    {
        fill (0);
    }

    /** Determine if all bits are zero.
    */
    bool isZero () const noexcept
    {
        for (int i = 0; i < Bytes; ++i)
        {
            if (m_byte [i] != 0)
                return false;
        }

        return true;
    }

    /** Determine if any bit is non-zero.
    */
    bool isNotZero () const noexcept
    {
        return ! isZero ();
    }

    /** Support conversion to `bool`.

        @return `true` if any bit is non-zero.

        @see SafeBool
    */
    bool asBoolean () const noexcept
    {
        return isNotZero ();
    }

    /** Access a particular byte.
    */
    value_type& getByte (int byteIndex) noexcept
    {
        bassert (byteIndex >= 0 && byteIndex < Bytes);

        return m_byte [byteIndex];
    }

    /** Access a particular byte as `const`.
    */
    value_type getByte (int byteIndex) const noexcept
    {
        bassert (byteIndex >= 0 && byteIndex < Bytes);

        return m_byte [byteIndex];
    }

    /** Access a particular byte.
    */
    value_type& operator[] (int byteIndex) noexcept
    {
        return getByte (byteIndex);
    }

    /** Access a particular byte as `const`.
    */
    value_type const operator[] (int byteIndex) const noexcept
    {
        return getByte (byteIndex);
    }

    /** Get an iterator to the beginning.
    */
    iterator begin () noexcept
    {
        return &m_byte [0];
    }

    /** Get an iterator to the end.
    */
    iterator end ()
    {
        return &m_byte [Bytes];
    }

    /** Get a const iterator to the beginning.
    */
    const_iterator cbegin () const noexcept
    {
        return &m_byte [0];
    }

    /** Get a const iterator to the end.
    */
    const_iterator cend () const noexcept
    {
        return &m_byte [Bytes];
    }

    /** Compare two objects.
    */
    int compare (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return memcmp (cbegin (), other.cbegin (), Bytes);
    }

    /** Determine equality.
    */
    bool operator== (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) == 0;
    }

    /** Determine inequality.
    */
    bool operator!= (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) != 0;
    }

    /** Ordered comparison.
    */
    bool operator< (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) < 0;
    }

    /** Ordered comparison.
    */
    bool operator<= (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) <= 0;
    }

    /** Ordered comparison.
    */
    bool operator> (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) > 0;
    }

    /** Ordered comparison.
    */
    bool operator>= (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) >= 0;
    }

    /** Perform bitwise logical-not.
    */
    UnsignedInteger <Bytes> operator~ () const noexcept
    {
        UnsignedInteger <Bytes> result;

        for (int i = 0; i < Bytes; ++i)
            result [i] = ~getByte (i);

        return result;
    }

    /** Perform bitwise logical-or.
    */
    UnsignedInteger <Bytes>& operator|= (UnsignedInteger <Bytes> const& rhs) noexcept
    {
        for (int i = 0; i < Bytes; ++i)
            getByte (i) |= rhs [i];

        return *this;
    }

    /** Perform bitwise logical-or.
    */
    UnsignedInteger <Bytes> operator| (UnsignedInteger <Bytes> const& rhs) const noexcept
    {
        UnsignedInteger <Bytes> result;

        for (int i = 0; i < Bytes; ++i)
            result [i] = getByte (i) | rhs [i];

        return result;
    }

    /** Perform bitwise logical-and.
    */
    UnsignedInteger <Bytes>& operator&= (UnsignedInteger <Bytes> const& rhs) noexcept
    {
        for (int i = 0; i < Bytes; ++i)
            getByte (i) &= rhs [i];

        return *this;
    }

    /** Perform bitwise logical-and.
    */
    UnsignedInteger <Bytes> operator& (UnsignedInteger <Bytes> const& rhs) const noexcept
    {
        UnsignedInteger <Bytes> result;

        for (int i = 0; i < Bytes; ++i)
            result [i] = getByte (i) & rhs [i];

        return result;
    }

    /** Perform bitwise logical-xor.
    */
    UnsignedInteger <Bytes>& operator^= (UnsignedInteger <Bytes> const& rhs) noexcept
    {
        for (int i = 0; i < Bytes; ++i)
            getByte (i) ^= rhs [i];

        return *this;
    }

    /** Perform bitwise logical-xor.
    */
    UnsignedInteger <Bytes> operator^ (UnsignedInteger <Bytes> const& rhs) const noexcept
    {
        UnsignedInteger <Bytes> result;

        for (int i = 0; i < Bytes; ++i)
            result [i] = getByte (i) ^ rhs [i];

        return result;
    }

    // VFALCO TODO:
    //
    //      increment, decrement, add, subtract
    //      negate
    //      other stuff that makes sense from base_uint <>
    //      missing stuff that built-in integers do
    //

private:
    value_type m_byte [Bytes];
};

#endif
