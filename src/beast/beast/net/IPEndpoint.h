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

#ifndef BEAST_NET_IPENDPOINT_H_INCLUDED
#define BEAST_NET_IPENDPOINT_H_INCLUDED

#include <string>
#include <ios>
#include <sstream>
    
#include "../CStdInt.h"
#include "../mpl/IfCond.h"

namespace beast
{

/** Represents an IP address (v4 or v6) and port combination. */
class IPEndpoint
{
public:
    enum Type
    {
        none,
        ipv4,
        ipv6
    };

    template <typename Number>
    static std::string numberToString (Number n)
    {
        std::ostringstream os;
        os << std::dec << n;
        return std::string (os.str());
    }

    //--------------------------------------------------------------------------

    /** Used for IPv4 formats. */
    struct V4
    {
        /** Construct the "any" address. */
        V4 ();

        /** Construct from a 32-bit unsigned.
            @note Octets are formed in order from the MSB to the LSB.       
        */
        explicit V4 (uint32 value_);

        /** Construct from four individual octets..
            @note The resulting address is a.b.c.d
        */
        V4 (uint8 a, uint8 b, uint8 c, uint8 d);

        /** Construct a copy of another address. */
        V4 (V4 const& other);

        /** Assign a copy of another address. */
        V4& operator= (V4 const& other);

        /** Returns a V4 address representing the local broadcast address. */
        static V4 localBroadcastAddress ();

        /** Returns the directed broadcast address for the network implied by this address. */
        V4 broadcastAddress () const;

        /** Returns the IPv4 address class as 'A', 'B', 'C', or 'D'.
            @note Class 'D' represents multicast addresses (224.*.*.*).
        */
        char getClass () const;

        /** Returns `true` if this is a public routable address. */
        bool isPublic () const;

        /** Returns `true` if this is a private, non-routable address. */
        bool isPrivate () const;

        /** Returns `true` if this is a broadcast address. */
        bool isBroadcast () const;

        /** Returns `true` if this is a multicast address. */
        bool isMulticast () const;

        /** Returns `true` if this refers to any loopback adapter address. */
        bool isLoopback () const;

        /** Supports access via operator[]. */
        template <bool IsConst>
        class Proxy
        {
        public:
            typedef typename mpl::IfCond <IsConst, uint32 const*, uint32*>::type Pointer;

            Proxy (int shift, Pointer value)
                : m_shift (shift)
                , m_value (value)
            {
            }

            operator uint8() const
            {
                return ((*m_value)>>m_shift)&0xff;
            }

            template <typename IntegralType>
            Proxy& operator= (IntegralType v)
            {
                (*m_value)=
                    (*m_value)&(!((0xff)<<m_shift)) |
                    ((v&0xff)<<m_shift);

                return *this;
            }

        private:
            int m_shift;
            Pointer m_value;
        };

        /** Provides read access to individual octets of the IPv4 address. */
        Proxy <true> operator[] (std::size_t index) const;

        /** Provides read/write access to individual octets of the IPv4 address. */
        Proxy <false> operator[] (std::size_t index);

        /** Convert the address to a human readable string. */
        /** @{ */
        std::string to_string () const;
        operator std::string () const;
        /** @} */

        /** The value as a 32 bit unsigned. */
        uint32 value;
    };

    //--------------------------------------------------------------------------

    /** Used for IPv6 formats. */
    struct V6
    {
        /** Returns `true` if this is a public routable address. */
        bool isPublic () const
        {
            return false;
        }

        /** Returns `true` if this is a private, non-routable address. */
        bool isPrivate () const
        {
            return false;
        }

        /** Returns `true` if this is a broadcast address. */
        bool isBroadcast () const
        {
            return false;
        }

        /** Returns `true` if this is a multicast address. */
        bool isMulticast () const
        {
            return false;
        }

        /** Returns `true` if this refers to any loopback adapter address. */
        bool isLoopback () const
        {
            return false;
        }

        /** Convert the address to a human readable string. */
        /** @{ */
        std::string to_string () const
        {
            return std::string();
        }

        operator std::string () const
        {
            return to_string();
        }
        /** @} */
    };

    //--------------------------------------------------------------------------

    /** Create an empty address. */
    IPEndpoint ();

    /** Create an IPv4 address with optional port. */
    IPEndpoint (V4 const& v4, uint16 port = 0);

    /** Create an IPv6 address with optional port. */
    IPEndpoint (V6 const& v6, uint16 port = 0);

    /** Create a copy of another IPEndpoint. */
    IPEndpoint (IPEndpoint const& other);

    /** Copy assign another IPEndpoint. */
    IPEndpoint& operator= (IPEndpoint const& other);

    /** Create an IPEndpoint from a string.
        If a parsing error occurs, the endpoint will be empty.
    */
    static IPEndpoint from_string (std::string const& s);

    /** Copy assign an IPv4 address.
        The port is set to zero.
    */
    IPEndpoint& operator= (V4 const& address);

    /** Copy assign an IPv6 address.
        The port is set to zero.
    */
    IPEndpoint& operator= (V6 const& address);

    /** Returns a new IPEndpoint with this address, and the given port. */
    IPEndpoint withPort (uint16 port) const;

    /** Returns `true` if this IPEndpoint refers to nothing. */
    bool empty () const;

    /** Returns `true` if this IPEndpoint refers to nothing. */
    bool isNull () const;

    /** Returns `true` if this IPEndpoint refers to something. */
    bool isNotNull () const;

    /** Returns the type of this IPEndpoint. */
    Type type () const;

    /** Returns `true` if this IPEndpoint represents an IPv4 address. */
    bool isV4 () const;

    /** Returns `true` if this IPEndpoint represents an IPv6 address. */
    bool isV6 () const;

    /** Returns the IPv4 address.
        Undefined behavior results if type() != ipv4.
    */
    V4 const& v4 () const;

    /** Returns the IPv6 address.
        Undefined behavior results if type() != ipv4.
    */
    V6 const& v6 () const;

    /** Returns the port number.
        Undefined if type() == none.
    */
    uint16 port () const;

    /** Returns `true` if this is a public routable address. */
    bool isPublic () const;

    /** Returns `true` if this is a private, non-routable address. */
    bool isPrivate () const;

    /** Returns `true` if this is a broadcast address. */
    bool isBroadcast () const;

    /** Returns `true` if this is a multicast address. */
    bool isMulticast () const;

    /** Returns `true` if this refers to any loopback adapter address. */
    bool isLoopback () const;

    /** Convert the address to a human readable string. */
    /** @{ */
    std::string to_string () const;
    operator std::string () const;
    /** @} */

private:
    Type m_type;
    uint16 m_port;
    V4 m_v4;
    V6 m_v6;
};

/** Comparison. */
/** @{ */
int  compare    (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);
bool operator== (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);
bool operator!= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);
bool operator<  (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);
bool operator<= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);
bool operator>  (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);
bool operator>= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs);

int  compare    (IPEndpoint const& lhs, IPEndpoint const& rhs);
bool operator== (IPEndpoint const& lhs, IPEndpoint const& rhs);
bool operator!= (IPEndpoint const& lhs, IPEndpoint const& rhs);
bool operator<  (IPEndpoint const& lhs, IPEndpoint const& rhs);
bool operator<= (IPEndpoint const& lhs, IPEndpoint const& rhs);
bool operator>  (IPEndpoint const& lhs, IPEndpoint const& rhs);
bool operator>= (IPEndpoint const& lhs, IPEndpoint const& rhs);

/** Output stream conversions. */
/** @{ */
std::ostream& operator<< (std::ostream& os, IPEndpoint::V4 const& addr);
std::ostream& operator<< (std::ostream& os, IPEndpoint::V6 const& addr);
std::ostream& operator<< (std::ostream& os, IPEndpoint const& ep);
/** @} */

/** Input stream conversions. */
/** @{ */
std::istream& operator>> (std::istream& is, IPEndpoint::V4& addr);
std::istream& operator>> (std::istream& is, IPEndpoint& ep);
//std::istream& operator>> (std::istream &is, IPEndpoint::V6&);
/** @} */

}

#endif
