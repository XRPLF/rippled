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

#include <typeinfo>

#include "../IPAddress.h"
#include "../detail/Parse.h"

namespace beast {
namespace IP {

Address::Address ()
    : m_type (ipv4)
{
}

Address::Address (AddressV4 const& addr)
    : m_type (ipv4)
    , m_v4 (addr)
{
}

Address::Address (AddressV6 const& addr)
    : m_type (ipv6)
    , m_v6 (addr)
{
}

Address& Address::operator= (AddressV4 const& addr)
{
    m_type = ipv4;
    m_v6 = AddressV6();
    m_v4 = addr;
    return *this;
}

Address& Address::operator= (AddressV6 const& addr)
{
    m_type = ipv6;
    m_v4 = AddressV4();
    m_v6 = addr;
    return *this;
}

std::pair <Address, bool> Address::from_string (std::string const& s)
{
    std::stringstream is (s);
    Address addr;
    is >> addr;
    if (! is.fail() && is.rdbuf()->in_avail() == 0)
        return std::make_pair (addr, true);
    return std::make_pair (Address (), false);
}

std::string Address::to_string () const
{
    return (is_v4 ())
        ? IP::to_string (to_v4())
        : IP::to_string (to_v6());
}

AddressV4 const& Address::to_v4 () const
{
    if (m_type != ipv4)
        throw std::bad_cast();
    return m_v4;
}

AddressV6 const& Address::to_v6 () const
{
    if (m_type != ipv6)
        throw std::bad_cast();
    return m_v6;
}

bool operator== (Address const& lhs, Address const& rhs)
{
    if (lhs.is_v4 ())
    {
        if (rhs.is_v4 ())
            return lhs.to_v4() == rhs.to_v4();
    }
    else
    {
        if (rhs.is_v6 ())
            return lhs.to_v6() == rhs.to_v6();
    }

    return false;
}

bool operator< (Address const& lhs, Address const& rhs)
{
    if (lhs.m_type < rhs.m_type)
        return true;
    if (lhs.is_v4 ())
        return lhs.to_v4() < rhs.to_v4();
    return lhs.to_v6() < rhs.to_v6();
}

//------------------------------------------------------------------------------

bool is_loopback (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_loopback (addr.to_v4 ())
        : is_loopback (addr.to_v6 ());
}

bool is_unspecified (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_unspecified (addr.to_v4 ())
        : is_unspecified (addr.to_v6 ());
}

bool is_multicast (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_multicast (addr.to_v4 ())
        : is_multicast (addr.to_v6 ());
}

bool is_private (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_private (addr.to_v4 ())
        : is_private (addr.to_v6 ());
}

bool is_public (Address const& addr)
{
    return (addr.is_v4 ())
        ? is_public (addr.to_v4 ())
        : is_public (addr.to_v6 ());
}

//------------------------------------------------------------------------------

std::size_t hash_value (Address const& addr)
{
    return (addr.is_v4 ())
        ? hash_value (addr.to_v4())
        : hash_value (addr.to_v6());
}

std::istream& operator>> (std::istream& is, Address& addr)
{
    // VFALCO TODO Support ipv6!
    AddressV4 addrv4;
    is >> addrv4;
    addr = Address (addrv4);
    return is;
}

//------------------------------------------------------------------------------

class IPAddressTests : public UnitTest
{
public:
    void shouldParseV4 (std::string const& s, uint32 value)
    {
        std::pair <AddressV4, bool> const result (
            AddressV4::from_string (s));

        if (expect (result.second))
        {
            if (expect (result.first.value == value))
            {
                expect (to_string (result.first) == s);
            }
        }
    }

    void failParseV4 (std::string const& s)
    {
        unexpected (AddressV4::from_string (s).second);
    }

    void testAddressV4 ()
    {
        beginTestCase ("AddressV4");

        expect (AddressV4().value == 0);
        expect (is_unspecified (AddressV4()));
        expect (AddressV4(0x01020304).value == 0x01020304);
        expect (AddressV4(1, 2, 3, 4).value == 0x01020304);

        unexpected (is_unspecified (AddressV4(1, 2, 3, 4)));

        AddressV4 const v1 (1);
        expect (AddressV4(v1).value == 1);

        {
            AddressV4 v;
            v = v1;
            expect (v.value == v1.value);
        }

        {
            AddressV4 v;
            v [0] = 1;
            v [1] = 2;
            v [2] = 3;
            v [3] = 4;
            expect (v.value == 0x01020304);
        }

        expect (to_string (AddressV4(0x01020304)) == "1.2.3.4");

        shouldParseV4 ("1.2.3.4", 0x01020304);
        shouldParseV4 ("255.255.255.255", 0xffffffff);
        shouldParseV4 ("0.0.0.0", 0);

        failParseV4 (".");
        failParseV4 ("..");
        failParseV4 ("...");
        failParseV4 ("....");
        failParseV4 ("1");
        failParseV4 ("1.");
        failParseV4 ("1.2");
        failParseV4 ("1.2.");
        failParseV4 ("1.2.3");
        failParseV4 ("1.2.3.");
        failParseV4 ("256.0.0.0");
        failParseV4 ("-1.2.3.4");
    }

    void testAddressV4Proxy ()
    {
      beginTestCase ("AddressV4::Proxy");

      AddressV4 v4 (10, 0, 0, 1);
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

    //--------------------------------------------------------------------------

    void testAddress ()
    {
        beginTestCase ("Address");

        std::pair <Address, bool> result (
            Address::from_string ("1.2.3.4"));
        expect (result.second);
        if (expect (result.first.is_v4 ()))
            expect (result.first.to_v4() == AddressV4 (1, 2, 3, 4));
    }

    //--------------------------------------------------------------------------

    void testEndpoint ()
    {
        beginTestCase ("Endpoint");

        {
            std::pair <Endpoint, bool> result (
                Endpoint::from_string_checked ("1.2.3.4"));
            expect (result.second);
            if (expect (result.first.address().is_v4 ()))
            {
                expect (result.first.address().to_v4() ==
                    AddressV4 (1, 2, 3, 4));
                expect (result.first.port() == 0);
                expect (to_string (result.first) == "1.2.3.4");
            }
        }

        {
            std::pair <Endpoint, bool> result (
                Endpoint::from_string_checked ("1.2.3.4:5"));
            expect (result.second);
            if (expect (result.first.address().is_v4 ()))
            {
                expect (result.first.address().to_v4() ==
                    AddressV4 (1, 2, 3, 4));
                expect (result.first.port() == 5);
                expect (to_string (result.first) == "1.2.3.4:5");
            }
        }

        Endpoint ep;

        ep = Endpoint (AddressV4 (127,0,0,1), 80);
        expect (! is_unspecified (ep));
        expect (! is_public (ep));
        expect (  is_private (ep));
        expect (! is_multicast (ep));
        expect (  is_loopback (ep));
        expect (to_string (ep) == "127.0.0.1:80");

        ep = Endpoint (AddressV4 (10,0,0,1));
        expect (AddressV4::get_class (ep.to_v4()) == 'A');
        expect (! is_unspecified (ep));
        expect (! is_public (ep));
        expect (  is_private (ep));
        expect (! is_multicast (ep));
        expect (! is_loopback (ep));
        expect (to_string (ep) == "10.0.0.1");

        ep = Endpoint (AddressV4 (166,78,151,147));
        expect (! is_unspecified (ep));
        expect (  is_public (ep));
        expect (! is_private (ep));
        expect (! is_multicast (ep));
        expect (! is_loopback (ep));
        expect (to_string (ep) == "166.78.151.147");
    }

    //--------------------------------------------------------------------------

    template <typename T>
    bool parse (char const* text, T& t)
    {
        std::string input (text);
        std::istringstream stream (input);
        stream >> t;
        return !stream.fail();
    }

    template <typename T>
    void shouldPass (char const* text)
    {
        T t;
        expect (parse (text, t));
        expect (to_string (t) == std::string (text));
    }

    template <typename T>
    void shouldFail (char const* text)
    {
        T t;
        unexpected (parse (text, t));
    }

    template <typename T>
    void testParse (char const* name)
    {
        beginTestCase (name);

        shouldPass <T> ("0.0.0.0");
        shouldPass <T> ("192.168.0.1");
        shouldPass <T> ("168.127.149.132");
        shouldPass <T> ("168.127.149.132:80");
        shouldPass <T> ("168.127.149.132:54321");

        shouldFail <T> ("");
        shouldFail <T> ("255");
        shouldFail <T> ("512");
        shouldFail <T> ("1.2.3.256");
        shouldFail <T> ("1.2.3:80");
    }

    void runTest ()
    {
        testAddressV4 ();
        testAddressV4Proxy();
        testAddress ();
        testEndpoint ();

        testParse <Endpoint> ("Parse Endpoint");
    }

    IPAddressTests () : UnitTest ("IPAddress", "beast")
    {
    }
};

static IPAddressTests ipEndpointTests;

}
}
