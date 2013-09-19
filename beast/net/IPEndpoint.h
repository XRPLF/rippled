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
#include <ostream>

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
        V4 ()
            : value (0)
        {
        }

        /** Construct from a 32-bit unsigned.
            @note Octets are formed in order from the MSB to the LSB.       
        */
        explicit V4 (uint32 value_)
            : value (value_)
        {
        }

        /** Construct from four individual octets..
            @note The resulting address is a.b.c.d
        */
        V4 (uint8 a, uint8 b, uint8 c, uint8 d)
            : value ((a<<24)|(b<<16)|(c<<8)|d)
        {
        }

        /** Construct a copy of another address. */
        V4 (V4 const& other)
            : value (other.value)
        {
        }

        /** Assign a copy of another address. */
        V4& operator= (V4 const& other)
        {
            value = other.value;
            return *this;
        }

        /** Returns a V4 address representing the local broadcast address. */
        static V4 localBroadcastAddress ()
        {
            return V4 (0xffffffff);
        }

        /** Returns the directed broadcast address for the network implied by this address. */
        V4 broadcastAddress () const
        {
            switch (getClass())
            {
            case 'A': return V4 ((value&0xff000000)|0x00ffffff);
            case 'B': return V4 ((value&0xffff0000)|0x0000ffff);
            case 'C': return V4 ((value&0xffffff00)|0x000000ff);
            default:
            case 'D':
                bassertfalse;
                break;
            }
            return V4();
        }

        /** Returns the IPv4 address class as 'A', 'B', 'C', or 'D'.
            @note Class 'D' represents multicast addresses (224.*.*.*).
        */
        char getClass () const
        {
            static char const* table = "AAAABBCD";
            return table[(value&0xE0000000)>>29];
        }

        /** Returns `true` if this is a public routable address. */
        bool isPublic () const
        {
            return !isPrivate() && !isBroadcast() && !isMulticast();
        }

        /** Returns `true` if this is a private, non-routable address. */
        bool isPrivate () const
        {
            return
                ((value&0xff000000)==0x0a000000) || // Prefix /8,    10.##.#.#
                ((value&0xfff00000)==0xac100000) || // Prefix /12   172.16.#.# - 172.31.#.#
                ((value&0xffff0000)==0xc0a80000) ;  // Prefix /16   192.168.#.#
        }

        /** Returns `true` if this is a broadcast address. */
        bool isBroadcast () const
        {
            return (value == broadcastAddress().value);
        }

        /** Returns `true` if this is a multicast address. */
        bool isMulticast () const
        {
            return getClass() == 'D';
        }

        /** Returns `true` if this refers to any loopback adapter address. */
        bool isLoopback () const
        {
            return (value&0xff000000)==0x7f000000;
        }

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
                bassert (v >= 0 && v <= 255);

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
        Proxy <true> operator[] (std::size_t index) const
        {
            switch (index)
            {
            default:
                bassertfalse;
            case 0: return Proxy <true> (24, &value);
            case 1: return Proxy <true> (16, &value);
            case 2: return Proxy <true> ( 8, &value);
            case 3: return Proxy <true> ( 0, &value);
            };
        };

        /** Provides read/write access to individual octets of the IPv4 address. */
        Proxy <false> operator[] (std::size_t index)
        {
            switch (index)
            {
            default:
                bassertfalse;
            case 0: return Proxy <false> (24, &value);
            case 1: return Proxy <false> (16, &value);
            case 2: return Proxy <false> ( 8, &value);
            case 3: return Proxy <false> ( 0, &value);
            };
        };

        /** Convert the address to a human readable string. */
        /** @{ */
        std::string to_string () const
        {
            std::string s;
            s.reserve (15);
            s.append (numberToString ((int)((*this)[0]))); s.push_back ('.');
            s.append (numberToString ((int)((*this)[1]))); s.push_back ('.');
            s.append (numberToString ((int)((*this)[2]))); s.push_back ('.');
            s.append (numberToString ((int)((*this)[3])));
            return s;
        }

        operator std::string () const
        {
            return to_string();
        }

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
    IPEndpoint ()
        : m_type (none)
    {
    }

    /** Create an IPv4 address with optional port. */
    IPEndpoint (V4 const& v4, uint16 port = 0)
        : m_type (ipv4)
        , m_port (port)
        , m_v4 (v4)
    {
    }

    /** Create an IPv6 address with optional port. */
    IPEndpoint (V6 const& v6, uint16 port = 0)
        : m_type (ipv6)
        , m_port (port)
        , m_v6 (v6)
    {
    }

    /** Create a copy of another IPEndpoint. */
    IPEndpoint (IPEndpoint const& other)
        : m_type (other.m_type)
        , m_port (other.m_port)
    {
        switch (m_type)
        {
        case ipv4: m_v4 = other.m_v4; break;
        case ipv6: m_v6 = other.m_v6; break;
        default:
        case none:
            break;
        };
    }

    /** Copy assign another IPEndpoint. */
    IPEndpoint& operator= (IPEndpoint const& other)
    {
        m_type = other.m_type;
        m_port = other.m_port;
        switch (m_type)
        {
        case ipv4: m_v4 = other.m_v4; break;
        case ipv6: m_v6 = other.m_v6; break;
        default:
        case none:
            break;
        };
        return *this;
    }

    /** Copy assign an IPv4 address.
        The port is set to zero.
    */
    IPEndpoint& operator= (V4 const& address)
    {
        m_type = ipv4;
        m_port = 0;
        m_v4 = address;
        return *this;
    }

    /** Copy assign an IPv6 address.
        The port is set to zero.
    */
    IPEndpoint& operator= (V6 const& address)
    {
        m_type = ipv6;
        m_port = 0;
        m_v6 = address;
        return *this;
    }

    /** Returns a new IPEndpoint with this address, and the given port. */
    IPEndpoint withPort (uint16 port) const
    {
        switch (m_type)
        {
        case ipv4: return IPEndpoint (m_v4, port);
        case ipv6: return IPEndpoint (m_v6, port);
        default:
        case none:
            bassertfalse;
            break;
        };
        return IPEndpoint();
    }

    /** Returns `true` if this IPEndpoint refers to nothing. */
    bool empty () const
    {
        return m_type == none;
    }

    /** Returns `true` if this IPEndpoint refers to nothing. */
    bool isNull () const
    {
        return empty ();
    }

    /** Returns `true` if this IPEndpoint refers to something. */
    bool isNotNull () const
    {
        return ! empty ();
    }

    /** Returns the type of this IPEndpoint. */
    Type type () const
    {
        return m_type;
    }

    /** Returns `true` if this IPEndpoint represents an IPv4 address. */
    bool isV4 () const
    {
        return m_type == ipv4;
    }

    /** Returns `true` if this IPEndpoint represents an IPv6 address. */
    bool isV6 () const
    {
        return m_type == ipv6;
    }

    /** Returns the IPv4 address.
        Undefined behavior results if type() != ipv4.
    */
    V4 const& v4 () const
    {
        return m_v4;
    }

    /** Returns the IPv6 address.
        Undefined behavior results if type() != ipv4.
    */
    V6 const& v6 () const
    {
        return m_v6;
    }

    /** Returns the port number.
        Undefined if type() == none.
    */
    uint16 port () const
    {
        return m_port;
    }

    /** Returns `true` if this is a public routable address. */
    bool isPublic () const
    {
        switch (m_type)
        {
        case ipv4: return m_v4.isPublic();
        case ipv6: return m_v6.isPublic();
        default:
            bassertfalse;
        case none:
            break;
        };
        return false;
    }

    /** Returns `true` if this is a private, non-routable address. */
    bool isPrivate () const
    {
        switch (m_type)
        {
        case ipv4: return m_v4.isPrivate();
        case ipv6: return m_v6.isPrivate();
        default:
            bassertfalse;
        case none:
            break;
        };
        return false;
    }

    /** Returns `true` if this is a broadcast address. */
    bool isBroadcast () const
    {
        switch (m_type)
        {
        case ipv4: return m_v4.isBroadcast();
        case ipv6: return m_v6.isBroadcast();
        default:
            bassertfalse;
        case none:
            break;
        };
        return false;
    }

    /** Returns `true` if this is a multicast address. */
    bool isMulticast () const
    {
        switch (m_type)
        {
        case ipv4: return m_v4.isMulticast();
        case ipv6: return m_v6.isMulticast();
        default:
            bassertfalse;
        case none:
            break;
        };
        return false;
    }

    /** Returns `true` if this refers to any loopback adapter address. */
    bool isLoopback () const
    {
        switch (m_type)
        {
        case ipv4: return m_v4.isLoopback();
        case ipv6: return m_v6.isLoopback();
        default:
            bassertfalse;
        case none:
            break;
        };
        return false;
    }

    /** Convert the address to a human readable string. */
    /** @{ */
    std::string to_string () const
    {
        switch (m_type)
        {
        case ipv4:
            {
                std::string s (m_v4.to_string());
                if (m_port != 0)
                {
                    s.append (":");
                    s.append (numberToString (m_port));
                }
                return s;
            }

        case ipv6:
            return m_v6.to_string();

        default:
        case none:
            bassertfalse;
            break;
        };
        return std::string();
    }

    operator std::string () const
    {
        return to_string();
    }
    /** @} */

private:
    Type m_type;
    uint16 m_port;
    V4 m_v4;
    V6 m_v6;
};

/** Comparison. */
inline bool operator== (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return lhs.value == rhs.value; }
inline bool operator!= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return lhs.value != rhs.value; }
inline bool operator<  (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return lhs.value <  rhs.value; }
inline bool operator<= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return lhs.value <= rhs.value; }
inline bool operator>  (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return lhs.value >  rhs.value; }
inline bool operator>= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return lhs.value >= rhs.value; }
//inline bool operator== (IPEndpoint::V6 const& lhs, IPEndpoint::V6 const& rhs) { return lhs.value == rhs.value; }
//inline bool operator!= (IPEndpoint::V6 const& lhs, IPEndpoint::V6 const& rhs) { return lhs.value != rhs.value; }
//inline bool operator<  (IPEndpoint::V6 const& lhs, IPEndpoint::V6 const& rhs) { return lhs.value <  rhs.value; }
//inline bool operator<= (IPEndpoint::V6 const& lhs, IPEndpoint::V6 const& rhs) { return lhs.value <= rhs.value; }
//inline bool operator>  (IPEndpoint::V6 const& lhs, IPEndpoint::V6 const& rhs) { return lhs.value >  rhs.value; }
//inline bool operator>= (IPEndpoint::V6 const& lhs, IPEndpoint::V6 const& rhs) { return lhs.value >= rhs.value; }

inline bool operator== (IPEndpoint const& lhs, IPEndpoint const& rhs)
{ 
    if (lhs.type() != rhs.type())
        return false;
    switch (lhs.type())
    {
    case IPEndpoint::none: return true;
    case IPEndpoint::ipv4: return lhs.v4() == rhs.v4();
    case IPEndpoint::ipv6: return lhs.v6() == rhs.v6();
    default:
        bassertfalse;
        break;
    }
    return false;
}

inline bool operator!= (IPEndpoint const& lhs, IPEndpoint const& rhs)
{
    return ! (lhs == rhs);
}

/** Output stream conversions. */
/** @{ */
inline std::ostream& operator<< (std::ostream &os, IPEndpoint::V4 const& addr)
{
    os << addr.to_string();
    return os;
}

inline std::ostream& operator<< (std::ostream &os, IPEndpoint::V6 const& addr)
{
    os << addr.to_string();
    return os;
}

inline std::ostream& operator<< (std::ostream &os, IPEndpoint const& endpoint)
{
    os << endpoint.to_string();
    return os;
}
/** @} */

}

#endif
