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

#ifndef BEAST_NET_IPADDRESSV4_H_INCLUDED
#define BEAST_NET_IPADDRESSV4_H_INCLUDED

#include <beast/hash/hash_append.h>

#include <cstdint>
#include <functional>
#include <ios>
#include <string>
#include <utility>

namespace beast {
namespace IP {

/** Represents a version 4 IP address. */
struct AddressV4
{
    /** Default constructor represents the 'any' address. */
    AddressV4 ();

    /** Construct from a 32-bit unsigned.
        @note Octets are formed in order from the MSB to the LSB.       
    */
    explicit AddressV4 (std::uint32_t value_);

    /** Construct from four individual octets..
        @note The resulting address is a.b.c.d
    */
    AddressV4 (std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

    /** Create an address from an IPv4 address string in dotted decimal form.
        @return A pair with the address, and bool set to `true` on success.
    */
    static std::pair <AddressV4, bool> from_string (std::string const& s);

    /** Returns an address that represents 'any' address. */
    static AddressV4 any ()
        { return AddressV4(); }

    /** Returns an address that represents the loopback address. */
    static AddressV4 loopback ()
        { return AddressV4 (0x7f000001); }

    /** Returns an address that represents the broadcast address. */
    static AddressV4 broadcast ()
        { return AddressV4 (0xffffffff); }

    /** Returns the broadcast address for the specified address. */
    static AddressV4 broadcast (AddressV4 const& address);

    /** Returns the broadcast address corresponding to the address and mask. */
    static AddressV4 broadcast (
        AddressV4 const& address, AddressV4 const& mask);

    /** Returns `true` if this is a broadcast address. */
    bool is_broadcast () const
        { return *this == broadcast (*this); }

    /** Returns the address class for the given address.
        @note Class 'D' represents multicast addresses (224.*.*.*).
    */
    static char get_class (AddressV4 const& address);

    /** Returns the netmask for the address class or address. */
    /** @{ */
    static AddressV4 netmask (char address_class);
    static AddressV4 netmask (AddressV4 const& v);
    /** @} */

    /** Arithmetic comparison. */
    /** @{ */
    friend bool operator== (AddressV4 const& lhs, AddressV4 const& rhs)
        { return lhs.value == rhs.value; }
    friend bool operator<  (AddressV4 const& lhs, AddressV4 const& rhs)
        { return lhs.value < rhs.value; }

    friend bool operator!= (AddressV4 const& lhs, AddressV4 const& rhs)
        { return ! (lhs == rhs); }
    friend bool operator>  (AddressV4 const& lhs, AddressV4 const& rhs)
        { return rhs < lhs; }
    friend bool operator<= (AddressV4 const& lhs, AddressV4 const& rhs)
        { return ! (lhs > rhs); }
    friend bool operator>= (AddressV4 const& lhs, AddressV4 const& rhs)
        { return ! (rhs > lhs); }
    /** @} */

    /** Array indexing for reading and writing indiviual octets. */
    /** @{ */
    template <bool IsConst>
    class Proxy
    {
    public:
        typedef typename std::conditional <
            IsConst, std::uint32_t const*, std::uint32_t*>::type Pointer;

        Proxy (int shift, Pointer value)
            : m_shift (shift)
            , m_value (value)
        {
        }

        operator std::uint8_t() const
        {
            return ((*m_value)>>m_shift) & 0xff;
        }

        template <typename IntegralType>
        Proxy& operator= (IntegralType v)
        {
            (*m_value) =
                    ( (*m_value) & (~((0xff)<<m_shift)) )
                | ((v&0xff) << m_shift);

            return *this;
        }

    private:
        int m_shift;
        Pointer m_value;
    };

    Proxy <true> operator[] (std::size_t index) const;
    Proxy <false> operator[] (std::size_t index);
    /** @} */

    /** The value as a 32 bit unsigned. */
    std::uint32_t value;
};

//------------------------------------------------------------------------------

/** Returns `true` if this is a loopback address. */
bool is_loopback (AddressV4 const& addr);

/** Returns `true` if the address is unspecified. */
bool is_unspecified (AddressV4 const& addr);

/** Returns `true` if the address is a multicast address. */
bool is_multicast (AddressV4 const& addr);

/** Returns `true` if the address is a private unroutable address. */
bool is_private (AddressV4 const& addr);

/** Returns `true` if the address is a public routable address. */
bool is_public (AddressV4 const& addr);

//------------------------------------------------------------------------------

/** Returns the address represented as a string. */
std::string to_string (AddressV4 const& addr);

/** Output stream conversion. */
template <typename OutputStream>
OutputStream& operator<< (OutputStream& os, AddressV4 const& addr)
    { return os << to_string (addr); }

/** Input stream conversion. */
std::istream& operator>> (std::istream& is, AddressV4& addr);

}

template <>
struct is_contiguously_hashable<IP::AddressV4>
    : public std::integral_constant<bool, sizeof(IP::AddressV4) == sizeof(std::uint32_t)>
{
};

}

//------------------------------------------------------------------------------

namespace std {
/** std::hash support. */
template <>
struct hash <beast::IP::AddressV4>
{
    std::size_t operator() (beast::IP::AddressV4 const& addr) const
        { return addr.value; }
};
}

#endif
