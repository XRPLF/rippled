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

#include "../IPEndpoint.h"

namespace beast
{

IPEndpoint::V4::V4 ()
    : value (0)
{
}

IPEndpoint::V4::V4 (uint32 value_)
    : value (value_)
{
}

IPEndpoint::V4::V4 (uint8 a, uint8 b, uint8 c, uint8 d)
    : value ((a<<24)|(b<<16)|(c<<8)|d)
{
}

IPEndpoint::V4::V4 (V4 const& other)
    : value (other.value)
{
}

IPEndpoint::V4& IPEndpoint::V4::operator= (V4 const& other)
{
    value = other.value;
    return *this;
}

IPEndpoint::V4 IPEndpoint::V4::localBroadcastAddress ()
{
    return V4 (0xffffffff);
}

IPEndpoint::V4 IPEndpoint::V4::broadcastAddress () const
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

char IPEndpoint::V4::getClass () const
{
    static char const* table = "AAAABBCD";
    return table[(value&0xE0000000)>>29];
}

bool IPEndpoint::V4::isPublic () const
{
    return !isPrivate() && !isBroadcast() && !isMulticast();
}

bool IPEndpoint::V4::isPrivate () const
{
    return
        ((value&0xff000000)==0x0a000000) || // Prefix /8,    10.##.#.#
        ((value&0xfff00000)==0xac100000) || // Prefix /12   172.16.#.# - 172.31.#.#
        ((value&0xffff0000)==0xc0a80000) || // Prefix /16   192.168.#.#
        isLoopback();
}

bool IPEndpoint::V4::isBroadcast () const
{
    return (value == broadcastAddress().value);
}

bool IPEndpoint::V4::isMulticast () const
{
    return getClass() == 'D';
}

bool IPEndpoint::V4::isLoopback () const
{
    return (value&0xff000000)==0x7f000000;
}

IPEndpoint::V4::Proxy <true> IPEndpoint::V4::operator[] (std::size_t index) const
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

IPEndpoint::V4::Proxy <false> IPEndpoint::V4::operator[] (std::size_t index)
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

std::string IPEndpoint::V4::to_string () const
{
    std::string s;
    s.reserve (15);
    s.append (numberToString ((int)((*this)[0]))); s.push_back ('.');
    s.append (numberToString ((int)((*this)[1]))); s.push_back ('.');
    s.append (numberToString ((int)((*this)[2]))); s.push_back ('.');
    s.append (numberToString ((int)((*this)[3])));
    return s;
}

IPEndpoint::V4::operator std::string () const
{
    return to_string();
}

//------------------------------------------------------------------------------

IPEndpoint::IPEndpoint ()
    : m_type (none)
{
}

IPEndpoint::IPEndpoint (V4 const& v4, uint16 port)
    : m_type (ipv4)
    , m_port (port)
    , m_v4 (v4)
{
}

IPEndpoint::IPEndpoint (V6 const& v6, uint16 port)
    : m_type (ipv6)
    , m_port (port)
    , m_v6 (v6)
{
}

IPEndpoint::IPEndpoint (IPEndpoint const& other)
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

IPEndpoint& IPEndpoint::operator= (IPEndpoint const& other)
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

IPEndpoint IPEndpoint::from_string (std::string const& s)
{
    std::stringstream is (s);
    IPEndpoint ep;
    is >> ep;
    if (! is.fail() && is.rdbuf()->in_avail() == 0)
        return ep;
    return IPEndpoint();
}

IPEndpoint& IPEndpoint::operator= (V4 const& address)
{
    m_type = ipv4;
    m_port = 0;
    m_v4 = address;
    return *this;
}

IPEndpoint& IPEndpoint::operator= (V6 const& address)
{
    m_type = ipv6;
    m_port = 0;
    m_v6 = address;
    return *this;
}

IPEndpoint IPEndpoint::withPort (uint16 port) const
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

bool IPEndpoint::empty () const
{
    return m_type == none;
}

bool IPEndpoint::isNull () const
{
    return empty ();
}

bool IPEndpoint::isNotNull () const
{
    return ! empty ();
}

IPEndpoint::Type IPEndpoint::type () const
{
    return m_type;
}

bool IPEndpoint::isV4 () const
{
    return m_type == ipv4;
}

bool IPEndpoint::isV6 () const
{
    return m_type == ipv6;
}

IPEndpoint::V4 const& IPEndpoint::v4 () const
{
    return m_v4;
}

IPEndpoint::V6 const& IPEndpoint::v6 () const
{
    return m_v6;
}

uint16 IPEndpoint::port () const
{
    return m_port;
}

bool IPEndpoint::isPublic () const
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

bool IPEndpoint::isPrivate () const
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

bool IPEndpoint::isBroadcast () const
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

bool IPEndpoint::isMulticast () const
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

bool IPEndpoint::isLoopback () const
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

std::string IPEndpoint::to_string () const
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

IPEndpoint::operator std::string () const
{
    return to_string();
}

//------------------------------------------------------------------------------

namespace parse
{

/** Require and consume the specified character from the input.
    @return `true` if the character matched.
*/
bool expect (std::istream& is, char v)
{
    char c;
    if (is.get(c) && v == c)
        return true;

    is.unget();
    is.setstate (std::ios_base::failbit);
    return false;
}

namespace detail
{

/** Used to disambiguate 8-bit integers from characters. */
template <typename IntType>
struct integer_holder
{
    IntType* pi;
    explicit integer_holder (IntType& i)
        : pi (&i)
    {
    }
    template <typename OtherIntType>
    IntType& operator= (OtherIntType o) const
    {
        *pi = o;
        return *pi;
    }
};

/** Parse 8-bit unsigned integer. */
std::istream& operator>> (std::istream& is, integer_holder <uint8> const& i)
{
    uint16 v;
    is >> v;
    if (! (v>=0 && v<=255))
    {
        is.setstate (std::ios_base::failbit);
        return is;
    }
    i = uint8(v);
    return is;
}

}

/** Free function for template argument deduction. */
template <typename IntType>
detail::integer_holder <IntType> integer (IntType& i)
{
    return detail::integer_holder <IntType> (i);
}

}

/** Parse IPv4 address. */
std::istream& operator>> (std::istream &is, IPEndpoint::V4& addr)
{
    uint8 octets [4];
    is >> parse::integer (octets [0]);
    for (int i = 1; i < 4; ++i)
    {
        if (!is || !parse::expect (is, '.'))
            return is;
        is >> parse::integer (octets [i]);
        if (!is)
            return is;
    }
    addr = IPEndpoint::V4 (octets[0], octets[1], octets[2], octets[3]);
    return is;
}

/** Parse an IPEndpoint.
    @note Currently only IPv4 addresses are supported.
*/
std::istream& operator>> (std::istream &is, IPEndpoint& ep)
{
    IPEndpoint::V4 v4;
    is >> v4;
    if (is.fail())
        return is;

    if (is.rdbuf()->in_avail()>0)
    {
        char c;
        is.get(c);
        if (c != ':')
        {
            is.unget();
            ep = IPEndpoint (v4);
            return is;
        }

        uint16 port;
        is >> port;
        if (is.fail())
            return is;

        ep = IPEndpoint (v4, port);
    }
    else
    {
        ep = IPEndpoint (v4);
    }
    
    return is;
}

//------------------------------------------------------------------------------

IPEndpoint IPEndpoint::from_string_altform (std::string const& s)
{
    // Accept the regular form if it parses
    {
        IPEndpoint ep (IPEndpoint::from_string (s));
        if (! ep.empty())
            return ep;
    }

    // Now try the alt form
    std::stringstream is (s);

    IPEndpoint::V4 v4;
    is >> v4;
    if (! is.fail())
    {
        IPEndpoint ep (v4);

        if (is.rdbuf()->in_avail()>0)
        {
            if (! parse::expect (is, ' '))
                return IPEndpoint();

            while (is.rdbuf()->in_avail()>0)
            {
                char c;
                is.get(c);
                if (c != ' ')
                {
                    is.unget();
                    break;
                }
            }

            uint16 port;
            is >> port;
            if (is.fail())
                return IPEndpoint();

            return ep.withPort (port);
        }
        else
        {
            // Just an address with no port
            return ep;
        }
    }

    // Could be V6 here...

    return IPEndpoint();
}

//------------------------------------------------------------------------------

int compare (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs)
{
    if (lhs.value < rhs.value)
        return -1;
    else if (lhs.value > rhs.value)
        return 1;
    return 0;
}

bool operator== (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return compare (lhs, rhs) == 0; }
bool operator!= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return compare (lhs, rhs) != 0; }
bool operator<  (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return compare (lhs, rhs) <  0; }
bool operator<= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return compare (lhs, rhs) <= 0; }
bool operator>  (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return compare (lhs, rhs) >  0; }
bool operator>= (IPEndpoint::V4 const& lhs, IPEndpoint::V4 const& rhs) { return compare (lhs, rhs) >= 0; }

static int type_compare (IPEndpoint const& lhs, IPEndpoint const& rhs)
{
    if (lhs.type() < rhs.type())
        return -1;
    else if (lhs.type() > rhs.type())
        return 1;
    return 0;
}

int compare (IPEndpoint const& lhs, IPEndpoint const& rhs)
{
    int const tc (type_compare (lhs, rhs));

    if (tc < 0)
        return -1;
    else if (tc > 0)
        return 1;

    switch (lhs.type())
    {
    case IPEndpoint::none: return 0;
    case IPEndpoint::ipv4: return compare (lhs.v4(), rhs.v4());
    default:
    case IPEndpoint::ipv6:
        break;
    };
    bassertfalse;
    return 0;
}

bool operator== (IPEndpoint const& lhs, IPEndpoint const& rhs) { return compare (lhs, rhs) == 0; }
bool operator!= (IPEndpoint const& lhs, IPEndpoint const& rhs) { return compare (lhs, rhs) != 0; }
bool operator<  (IPEndpoint const& lhs, IPEndpoint const& rhs) { return compare (lhs, rhs) <  0; }
bool operator<= (IPEndpoint const& lhs, IPEndpoint const& rhs) { return compare (lhs, rhs) <= 0; }
bool operator>  (IPEndpoint const& lhs, IPEndpoint const& rhs) { return compare (lhs, rhs) >  0; }
bool operator>= (IPEndpoint const& lhs, IPEndpoint const& rhs) { return compare (lhs, rhs) >= 0; }

std::ostream& operator<< (std::ostream &os, IPEndpoint::V4 const& addr)
{
    os << addr.to_string();
    return os;
}

std::ostream& operator<< (std::ostream &os, IPEndpoint::V6 const& addr)
{
    os << addr.to_string();
    return os;
}

std::ostream& operator<< (std::ostream &os, IPEndpoint const& ep)
{
    os << ep.to_string();
    return os;
}

//------------------------------------------------------------------------------

class IPEndpointTests : public UnitTest
{
public:
    bool parse (char const* text, IPEndpoint& ep)
    {
        std::string input (text);
        std::istringstream stream (input);
        stream >> ep;
        return !stream.fail();
    }

    void shouldPass (char const* text)
    {
        IPEndpoint ep;
        expect (parse (text, ep));
        expect (ep.to_string() == std::string(text));
    }

    void shouldFail (char const* text)
    {
        IPEndpoint ep;
        unexpected (parse (text, ep));
    }

    void testParse ()
    {
        beginTestCase ("parse");

        shouldPass ("0.0.0.0");
        shouldPass ("192.168.0.1");
        shouldPass ("168.127.149.132");
        shouldPass ("168.127.149.132:80");
        shouldPass ("168.127.149.132:54321");

        shouldFail ("");
        shouldFail ("255");
        shouldFail ("512");
        shouldFail ("1.2.3.256");
        shouldFail ("1.2.3:80");
    }

    void testPrint ()
    {
        beginTestCase ("addresses");

        IPEndpoint ep;

        ep = IPEndpoint(IPEndpoint::V4(127,0,0,1)).withPort (80);
        expect (!ep.isPublic());
        expect ( ep.isPrivate());
        expect (!ep.isBroadcast());
        expect (!ep.isMulticast());
        expect ( ep.isLoopback());
        expect (ep.to_string() == "127.0.0.1:80");

        ep = IPEndpoint::V4(10,0,0,1);
        expect ( ep.v4().getClass() == 'A');
        expect (!ep.isPublic());
        expect ( ep.isPrivate());
        expect (!ep.isBroadcast());
        expect (!ep.isMulticast());
        expect (!ep.isLoopback());
        expect (ep.to_string() == "10.0.0.1");

        ep = IPEndpoint::V4(166,78,151,147);
        expect ( ep.isPublic());
        expect (!ep.isPrivate());
        expect (!ep.isBroadcast());
        expect (!ep.isMulticast());
        expect (!ep.isLoopback());
        expect (ep.to_string() == "166.78.151.147");
    }

    void runTest ()
    {
        testPrint();
        testParse();
    }

    IPEndpointTests () : UnitTest ("IPEndpoint", "beast")
    {
    }
};

static IPEndpointTests ipEndpointTests;

}
