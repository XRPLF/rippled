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

    Integer representations are stored as big endian.

    @note The number of bits represented can only be a multiple of 8.

    @tparam Bytes The number of bytes of storage.
*/
template <unsigned int Bytes>
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
    typedef unsigned char* iterator;
    typedef unsigned char const* const_iterator;

    /** Construct the object.

        The values are uninitialized.
    */
    UnsignedInteger () noexcept
    {
    }

    /** Copy construction.
    */
    UnsignedInteger (UnsignedInteger <Bytes> const& other) noexcept
    {
        this->operator= (other);
    }

    /** Assignment.
    */
    UnsignedInteger <Bytes>& operator= (UnsignedInteger const& other) noexcept
    {
        memcpy (m_byte, other.m_byte, Bytes);
        return *this;
    }

    /** Assignment from an integer type.

        @invariant IntegerType must be an unsigned integer type.
    */
    // If you get ambiguous calls to overloaded function errors it
    // means you're trying to pass a signed integer, which doesn't work here!
    //
    template <class IntegerType>
    UnsignedInteger <Bytes>& operator= (IntegerType value)
    {
        static_bassert (sizeof (Bytes) >= sizeof (IntegerType));
        clear ();
        value = ByteOrder::swapIfLittleEndian (value);
        memcpy (end () - sizeof (value), &value, sizeof (value));
        return *this;
    }

    /** Create from an integer type.

        @invariant IntegerType must be an unsigned integer type.
    */
    template <class IntegerType>
    static UnsignedInteger <Bytes> createFromInteger (IntegerType value)
    {
        UnsignedInteger <Bytes> result;
        result.operator= (value);
        return result;
    }

    /** Construct with a filled value.
    */
    static UnsignedInteger <Bytes> createFilled (unsigned char value)
    {
        UnsignedInteger <Bytes> result;
        result.fill (value);
        return result;
    }

    /** Fill with a particular byte value.
    */
    void fill (unsigned char value) noexcept
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
    unsigned char& getByte (int byteIndex) noexcept
    {
        bassert (byteIndex >= 0 && byteIndex < Bytes);

        return m_byte [byteIndex];
    }

    /** Access a particular byte as `const`.
    */
    unsigned char getByte (int byteIndex) const noexcept
    {
        bassert (byteIndex >= 0 && byteIndex < Bytes);

        return m_byte [byteIndex];
    }

    /** Access a particular byte.
    */
    unsigned char& operator[] (int byteIndex) noexcept
    {
        return getByte (byteIndex);
    }

    /** Access a particular byte as `const`.
    */
    unsigned char const operator[] (int byteIndex) const noexcept
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
        return compare (other) == -1;
    }

    /** Ordered comparison.
    */
    bool operator<= (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) != 1;
    }

    /** Ordered comparison.
    */
    bool operator> (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) == 1;
    }

    /** Ordered comparison.
    */
    bool operator>= (UnsignedInteger <Bytes> const& other) const noexcept
    {
        return compare (other) != -1;
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
    unsigned char m_byte [Bytes];
};

#endif
