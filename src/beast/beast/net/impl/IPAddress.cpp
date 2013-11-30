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

#include "../IPAddress.h"

namespace beast {

IPAddress::V4::V4 ()
    : value (0)
{
}

IPAddress::V4::V4 (uint32 value_)
    : value (value_)
{
}

IPAddress::V4::V4 (uint8 a, uint8 b, uint8 c, uint8 d)
    : value ((a<<24)|(b<<16)|(c<<8)|d)
{
}

IPAddress::V4::V4 (V4 const& other)
    : value (other.value)
{
}

IPAddress::V4& IPAddress::V4::operator= (V4 const& other)
{
    value = other.value;
    return *this;
}

IPAddress::V4 IPAddress::V4::localBroadcastAddress ()
{
    return V4 (0xffffffff);
}

IPAddress::V4 IPAddress::V4::broadcastAddress () const
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

char IPAddress::V4::getClass () const
{
    static char const* table = "AAAABBCD";
    return table[(value&0xE0000000)>>29];
}

bool IPAddress::V4::isPublic () const
{
    return !isPrivate() && !isBroadcast() && !isMulticast();
}

bool IPAddress::V4::isPrivate () const
{
    return
        ((value&0xff000000)==0x0a000000) || // Prefix /8,    10.##.#.#
        ((value&0xfff00000)==0xac100000) || // Prefix /12   172.16.#.# - 172.31.#.#
        ((value&0xffff0000)==0xc0a80000) || // Prefix /16   192.168.#.#
        isLoopback();
}

bool IPAddress::V4::isBroadcast () const
{
    return (value == broadcastAddress().value);
}

bool IPAddress::V4::isMulticast () const
{
    return getClass() == 'D';
}

bool IPAddress::V4::isLoopback () const
{
    return (value&0xff000000)==0x7f000000;
}

IPAddress::V4::Proxy <true> IPAddress::V4::operator[] (std::size_t index) const
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

IPAddress::V4::Proxy <false> IPAddress::V4::operator[] (std::size_t index)
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

std::string IPAddress::V4::to_string () const
{
    std::string s;
    s.reserve (15);
    s.append (numberToString ((int)((*this)[0]))); s.push_back ('.');
    s.append (numberToString ((int)((*this)[1]))); s.push_back ('.');
    s.append (numberToString ((int)((*this)[2]))); s.push_back ('.');
    s.append (numberToString ((int)((*this)[3])));
    return s;
}

IPAddress::V4::operator std::string () const
{
    return to_string();
}

//------------------------------------------------------------------------------

IPAddress::IPAddress ()
    : m_type (none)
{
}

IPAddress::IPAddress (V4 const& v4, uint16 port)
    : m_type (ipv4)
    , m_port (port)
    , m_v4 (v4)
{
}

IPAddress::IPAddress (V6 const& v6, uint16 port)
    : m_type (ipv6)
    , m_port (port)
    , m_v6 (v6)
{
}

IPAddress::IPAddress (IPAddress const& other)
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

IPAddress& IPAddress::operator= (IPAddress const& other)
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

IPAddress IPAddress::from_string (std::string const& s)
{
    std::stringstream is (s);
    IPAddress ep;
    is >> ep;
    if (! is.fail() && is.rdbuf()->in_avail() == 0)
        return ep;
    return IPAddress();
}

IPAddress& IPAddress::operator= (V4 const& address)
{
    m_type = ipv4;
    m_port = 0;
    m_v4 = address;
    return *this;
}

IPAddress& IPAddress::operator= (V6 const& address)
{
    m_type = ipv6;
    m_port = 0;
    m_v6 = address;
    return *this;
}

IPAddress IPAddress::withPort (uint16 port) const
{
    switch (m_type)
    {
    case ipv4: return IPAddress (m_v4, port);
    case ipv6: return IPAddress (m_v6, port);
    default:
    case none:
        bassertfalse;
        break;
    };
    return IPAddress();
}

bool IPAddress::empty () const
{
    return m_type == none;
}

bool IPAddress::isNull () const
{
    return empty ();
}

bool IPAddress::isNotNull () const
{
    return ! empty ();
}

IPAddress::Type IPAddress::type () const
{
    return m_type;
}

bool IPAddress::isV4 () const
{
    return m_type == ipv4;
}

bool IPAddress::isV6 () const
{
    return m_type == ipv6;
}

IPAddress::V4 const& IPAddress::v4 () const
{
    return m_v4;
}

IPAddress::V6 const& IPAddress::v6 () const
{
    return m_v6;
}

uint16 IPAddress::port () const
{
    return m_port;
}

bool IPAddress::isPublic () const
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

bool IPAddress::isPrivate () const
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

bool IPAddress::isBroadcast () const
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

bool IPAddress::isMulticast () const
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

bool IPAddress::isLoopback () const
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

std::string IPAddress::to_string () const
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

IPAddress::operator std::string () const
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
std::istream& operator>> (std::istream &is, IPAddress::V4& addr)
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
    addr = IPAddress::V4 (octets[0], octets[1], octets[2], octets[3]);
    return is;
}

/** Parse an IPAddress.
    @note Currently only IPv4 addresses are supported.
*/
std::istream& operator>> (std::istream &is, IPAddress& ep)
{
    IPAddress::V4 v4;
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
            ep = IPAddress (v4);
            return is;
        }

        uint16 port;
        is >> port;
        if (is.fail())
            return is;

        ep = IPAddress (v4, port);
    }
    else
    {
        ep = IPAddress (v4);
    }

    return is;
}

//------------------------------------------------------------------------------

IPAddress IPAddress::from_string_altform (std::string const& s)
{
    // Accept the regular form if it parses
    {
        IPAddress ep (IPAddress::from_string (s));
        if (! ep.empty())
            return ep;
    }

    // Now try the alt form
    std::stringstream is (s);

    IPAddress::V4 v4;
    is >> v4;
    if (! is.fail())
    {
        IPAddress ep (v4);

        if (is.rdbuf()->in_avail()>0)
        {
            if (! parse::expect (is, ' '))
                return IPAddress();

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
                return IPAddress();

            return ep.withPort (port);
        }
        else
        {
            // Just an address with no port
            return ep;
        }
    }

    // Could be V6 here...

    return IPAddress();
}

//------------------------------------------------------------------------------

bool operator== (IPAddress::V4 const& lhs, IPAddress::V4 const& rhs)
    { return lhs.value == rhs.value; }

bool operator< (IPAddress::V4 const& lhs, IPAddress::V4 const& rhs)
    { return lhs.value < rhs.value; }

bool operator!= (IPAddress::V4 const& lhs, IPAddress::V4 const& rhs)
    { return ! (lhs == rhs); }

bool operator> (IPAddress::V4 const& lhs, IPAddress::V4 const& rhs)
    { return rhs < lhs; }

bool operator<= (IPAddress::V4 const& lhs, IPAddress::V4 const& rhs)
    { return ! (rhs < lhs); }

bool operator>= (IPAddress::V4 const& lhs, IPAddress::V4 const& rhs)
    { return ! (lhs < rhs); }

//------------------------------------------------------------------------------

bool operator== (IPAddress const& lhs, IPAddress const& rhs)
{
    if (lhs.type() != rhs.type())
        return false;
    switch (lhs.type())
    {
    case IPAddress::none: return true;
    case IPAddress::ipv4:
        if (lhs.v4() != rhs.v4())
            return false;
        if (lhs.port() != rhs.port())
            return false;
        return true;
    case IPAddress::ipv6:
    default:
        bassertfalse;
    }
    return false;
}

bool operator< (IPAddress const& lhs, IPAddress const& rhs)
{
    if (lhs.type() > rhs.type())
        return false;
    if (lhs.type() < rhs.type())
        return true;
    switch (lhs.type())
    {
    case IPAddress::none: return true;
    case IPAddress::ipv4:
        if (lhs.v4() < rhs.v4())
            return true;
        if (lhs.v4() > rhs.v4())
            return false;
        return lhs.port() < rhs.port();
    case IPAddress::ipv6:
    default:
        bassertfalse;
    }
    return false;
}

bool operator!= (IPAddress const& lhs, IPAddress const& rhs)
    { return ! (lhs == rhs); }

bool operator> (IPAddress const& lhs, IPAddress const& rhs)
    { return rhs < lhs; }

bool operator<= (IPAddress const& lhs, IPAddress const& rhs)
    { return ! (rhs < lhs); }

bool operator>= (IPAddress const& lhs, IPAddress const& rhs)
    { return ! (lhs < rhs); }

//------------------------------------------------------------------------------

std::ostream& operator<< (std::ostream &os, IPAddress::V4 const& addr)
{
    os << addr.to_string();
    return os;
}

std::ostream& operator<< (std::ostream &os, IPAddress::V6 const& addr)
{
    os << addr.to_string();
    return os;
}

std::ostream& operator<< (std::ostream &os, IPAddress const& ep)
{
    os << ep.to_string();
    return os;
}

//------------------------------------------------------------------------------

class IPAddressTests : public UnitTest
{
public:
    bool parse (char const* text, IPAddress& ep)
    {
        std::string input (text);
        std::istringstream stream (input);
        stream >> ep;
        return !stream.fail();
    }

    void shouldPass (char const* text)
    {
        IPAddress ep;
        expect (parse (text, ep));
        expect (ep.to_string() == std::string(text));
    }

    void shouldFail (char const* text)
    {
        IPAddress ep;
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

    void testV4Proxy ()
    {
      beginTestCase("v4 proxy");

      IPAddress::V4 v4 (10, 0, 0, 1);
      expect (v4[0]==10);
      expect (v4[1]==0);
      expect (v4[2]==0);
      expect (v4[3]==1);

      expect((!((0xff)<<16)) == 0x00000000);
      expect((~((0xff)<<16)) == 0xff00ffff);

      v4[1] = 10;
      expect (v4[0]==10);
      expect (v4[1]==10);
      expect (v4[2]==0);
      expect (v4[3]==1);
    }

    void testPrint ()
    {
        beginTestCase ("addresses");

        IPAddress ep;

        ep = IPAddress(IPAddress::V4(127,0,0,1)).withPort (80);
        expect (!ep.isPublic());
        expect ( ep.isPrivate());
        expect (!ep.isBroadcast());
        expect (!ep.isMulticast());
        expect ( ep.isLoopback());
        expect (ep.to_string() == "127.0.0.1:80");

        ep = IPAddress::V4(10,0,0,1);
        expect ( ep.v4().getClass() == 'A');
        expect (!ep.isPublic());
        expect ( ep.isPrivate());
        expect (!ep.isBroadcast());
        expect (!ep.isMulticast());
        expect (!ep.isLoopback());
        expect (ep.to_string() == "10.0.0.1");

        ep = IPAddress::V4(166,78,151,147);
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
        testV4Proxy();
    }

    IPAddressTests () : UnitTest ("IPAddress", "beast")
    {
    }
};

static IPAddressTests ipEndpointTests;

}
