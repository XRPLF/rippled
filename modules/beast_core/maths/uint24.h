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

#ifndef BEAST_UINT24_H_INCLUDED
#define BEAST_UINT24_H_INCLUDED

/** A 24 bit unsigned integer.
    We try to be as compatible as possible with built in types.
*/
class uint24 : public SafeBool <uint24>
{
public:
    enum { mask = 0xffffff };
    inline uint24 () noexcept { /* uninitialized */ }
    inline uint24 (uint24 const& other) noexcept : m_value (other.m_value) { }
    inline uint24& operator= (uint24 const& other) noexcept { m_value = other.m_value; return *this; }
    template <typename IntegralType> inline uint24 (IntegralType value) noexcept : m_value (value & mask) { }
    template <typename IntegralType> inline uint24& operator= (IntegralType value) noexcept { m_value = value & mask; return *this; }
    inline uint32 get () const noexcept { return m_value; }
    inline bool asBoolean () const noexcept { return m_value != 0; }
    inline operator String () const { return String (m_value); }
    inline uint24& operator++ ()    noexcept { (++m_value) &= mask; return *this; }
    inline uint24& operator-- ()    noexcept { (--m_value) &= mask; return *this; }
    inline uint24  operator++ (int) noexcept { return uint24 (m_value + 1); }
    inline uint24  operator-- (int) noexcept { return uint24 (m_value - 1); }
    inline uint24& operator~  () { m_value = (~ m_value) & mask; return *this; }
    inline uint24& operator+= (uint24 const& rhs) { m_value = (m_value + rhs.m_value) & mask; return *this; }
    inline uint24& operator-= (uint24 const& rhs) { m_value = (m_value - rhs.m_value) & mask; return *this; }
    inline uint24& operator*= (uint24 const& rhs) { m_value = (m_value * rhs.m_value) & mask; return *this; }
    inline uint24& operator/= (uint24 const& rhs) { m_value = (m_value / rhs.m_value) & mask; return *this; }
    inline uint24& operator|= (uint24 const& rhs) { m_value = (m_value | rhs.m_value) & mask; return *this; }
    inline uint24& operator&= (uint24 const& rhs) { m_value = (m_value & rhs.m_value) & mask; return *this; }
    inline uint24& operator^= (uint24 const& rhs) { m_value = (m_value ^ rhs.m_value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator+= (IntegralType value) { m_value = (m_value + value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator-= (IntegralType value) { m_value = (m_value - value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator*= (IntegralType value) { m_value = (m_value * value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator/= (IntegralType value) { m_value = (m_value / value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator|= (IntegralType value) { m_value = (m_value | value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator&= (IntegralType value) { m_value = (m_value & value) & mask; return *this; }
    template <typename IntegralType> inline uint24& operator^= (IntegralType value) { m_value = (m_value ^ value) & mask; return *this; }
    template <typename IntegralType> inline bool operator== (IntegralType value) const noexcept { return m_value == value; }
    template <typename IntegralType> inline bool operator!= (IntegralType value) const noexcept { return m_value != value; }
    template <typename IntegralType> inline bool operator<  (IntegralType value) const noexcept { return m_value <  value; }
    template <typename IntegralType> inline bool operator>  (IntegralType value) const noexcept { return m_value >  value; }
    template <typename IntegralType> inline bool operator<= (IntegralType value) const noexcept { return m_value <= value; }
    template <typename IntegralType> inline bool operator>= (IntegralType value) const noexcept { return m_value >= value; }

    // Construct from raw bytes
    static uint24 from3RawBytes (void const* buf)
    {
        uint24 result;
        uint8* const raw (reinterpret_cast <uint8*> (&result.m_value));
        uint8 const* const data (reinterpret_cast <uint8 const*> (buf));

#if BEAST_LITTLE_ENDIAN
        raw [0] = data [0];
        raw [1] = data [1];
        raw [2] = data [2];
        raw [3] = 0;
#else
        raw [0] = 0;
        raw [1] = data [0];
        raw [2] = data [1];
        raw [3] = data [2];
#endif
        return result;
    }

private:
    uint24* operator&();
    uint24 const* operator&() const;

    friend struct detail::SwapBytes <uint24>;

    inline uint8& operator[] (int index)       noexcept { bassert (index >= 0 && index < 3); return raw () [index]; }
    inline uint8  operator[] (int index) const noexcept { bassert (index >= 0 && index < 3); return raw () [index]; }

#if BEAST_LITTLE_ENDIAN
    inline uint8*       raw ()       noexcept { return reinterpret_cast <uint8*>       (&m_value); }
    inline uint8 const* raw () const noexcept { return reinterpret_cast <uint8 const*> (&m_value); }
#else
    inline uint8*       raw ()       noexcept { return reinterpret_cast <uint8*>       (&m_value) + 1; }
    inline uint8 const* raw () const noexcept { return reinterpret_cast <uint8 const*> (&m_value) + 1; }
#endif

    uint32 m_value;
};

inline uint24 const operator~ (uint24 const& value) noexcept { return uint24 (~value.get ()); }
inline uint24 const operator+ (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () + rhs.get ()); }
inline uint24 const operator- (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () - rhs.get ()); }
inline uint24 const operator/ (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () / rhs.get ()); }
inline uint24 const operator* (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () * rhs.get ()); }
inline uint24 const operator| (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () ^ rhs.get ()); }
inline uint24 const operator& (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () & rhs.get ()); }
inline uint24 const operator^ (uint24 const& lhs, uint24 const& rhs) noexcept { return uint24 (lhs.get () ^ rhs.get ()); }
inline bool operator== (uint24 const& lhs, uint24 const& rhs) noexcept { return lhs.get () == rhs.get (); }
inline bool operator!= (uint24 const& lhs, uint24 const& rhs) noexcept { return lhs.get () != rhs.get (); }
inline bool operator<  (uint24 const& lhs, uint24 const& rhs) noexcept { return lhs.get () <  rhs.get (); }
inline bool operator>  (uint24 const& lhs, uint24 const& rhs) noexcept { return lhs.get () >  rhs.get (); }
inline bool operator<= (uint24 const& lhs, uint24 const& rhs) noexcept { return lhs.get () <= rhs.get (); }
inline bool operator>= (uint24 const& lhs, uint24 const& rhs) noexcept { return lhs.get () >= rhs.get (); }

/** SwapBytes specialization uint24. */
namespace detail
{

template <>
struct SwapBytes <uint24>
{
    inline uint24 const operator() (uint24 const& value) const noexcept 
    {
#if BEAST_LITTLE_ENDIAN
        uint24 result;
        result [0] = value [2];
        result [1] = value [1];
        result [2] = value [0];
        result [3] = 0;
        return result;
#else
        return value;
#endif
    }
};

}

#endif
