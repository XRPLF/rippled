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

// MODULES: ../impl/IPEndpoint.cpp ../impl/IPAddressV4.cpp ../impl/IPAddressV6.cpp

#if BEAST_INCLUDE_BEASTCONFIG
#endif

#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/beast/net/detail/Parse.h>

#include <ripple/beast/unit_test.h>

#include <typeinfo>

namespace beast {
namespace IP {

//------------------------------------------------------------------------------

class IPEndpoint_test : public unit_test::suite
{
public:
    void shouldParseV4 (std::string const& s, std::uint32_t value)
    {
        std::pair <AddressV4, bool> const result (
            AddressV4::from_string (s));

        if (BEAST_EXPECT(result.second))
        {
            if (BEAST_EXPECT(result.first.value == value))
            {
                BEAST_EXPECT(to_string (result.first) == s);
            }
        }
    }

    void failParseV4 (std::string const& s)
    {
        unexpected (AddressV4::from_string (s).second);
    }

    void testAddressV4 ()
    {
        testcase ("AddressV4");

        BEAST_EXPECT(AddressV4().value == 0);
        BEAST_EXPECT(is_unspecified (AddressV4()));
        BEAST_EXPECT(AddressV4(0x01020304).value == 0x01020304);
        BEAST_EXPECT(AddressV4(1, 2, 3, 4).value == 0x01020304);

        unexpected (is_unspecified (AddressV4(1, 2, 3, 4)));

        AddressV4 const v1 (1);
        BEAST_EXPECT(AddressV4(v1).value == 1);

        {
            AddressV4 v;
            v = v1;
            BEAST_EXPECT(v.value == v1.value);
        }

        {
            AddressV4 v;
            v [0] = 1;
            v [1] = 2;
            v [2] = 3;
            v [3] = 4;
            BEAST_EXPECT(v.value == 0x01020304);
        }

        BEAST_EXPECT(to_string (AddressV4(0x01020304)) == "1.2.3.4");

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
      testcase ("AddressV4::Proxy");

      AddressV4 v4 (10, 0, 0, 1);
      BEAST_EXPECT(v4[0]==10);
      BEAST_EXPECT(v4[1]==0);
      BEAST_EXPECT(v4[2]==0);
      BEAST_EXPECT(v4[3]==1);

      BEAST_EXPECT((~((0xff)<<16)) == 0xff00ffff);

      v4[1] = 10;
      BEAST_EXPECT(v4[0]==10);
      BEAST_EXPECT(v4[1]==10);
      BEAST_EXPECT(v4[2]==0);
      BEAST_EXPECT(v4[3]==1);
    }

    //--------------------------------------------------------------------------

    void testAddress ()
    {
        testcase ("Address");

        std::pair <Address, bool> result (
            Address::from_string ("1.2.3.4"));
        BEAST_EXPECT(result.second);
        if (BEAST_EXPECT(result.first.is_v4 ()))
            BEAST_EXPECT(result.first.to_v4() == AddressV4 (1, 2, 3, 4));
    }

    //--------------------------------------------------------------------------

    void testEndpoint ()
    {
        testcase ("Endpoint");

        {
            std::pair <Endpoint, bool> result (
                Endpoint::from_string_checked ("1.2.3.4"));
            BEAST_EXPECT(result.second);
            if (BEAST_EXPECT(result.first.address().is_v4 ()))
            {
                BEAST_EXPECT(result.first.address().to_v4() ==
                    AddressV4 (1, 2, 3, 4));
                BEAST_EXPECT(result.first.port() == 0);
                BEAST_EXPECT(to_string (result.first) == "1.2.3.4");
            }
        }

        {
            std::pair <Endpoint, bool> result (
                Endpoint::from_string_checked ("1.2.3.4:5"));
            BEAST_EXPECT(result.second);
            if (BEAST_EXPECT(result.first.address().is_v4 ()))
            {
                BEAST_EXPECT(result.first.address().to_v4() ==
                    AddressV4 (1, 2, 3, 4));
                BEAST_EXPECT(result.first.port() == 5);
                BEAST_EXPECT(to_string (result.first) == "1.2.3.4:5");
            }
        }

        Endpoint ep;

        ep = Endpoint (AddressV4 (127,0,0,1), 80);
        BEAST_EXPECT(! is_unspecified (ep));
        BEAST_EXPECT(! is_public (ep));
        BEAST_EXPECT(  is_private (ep));
        BEAST_EXPECT(! is_multicast (ep));
        BEAST_EXPECT(  is_loopback (ep));
        BEAST_EXPECT(to_string (ep) == "127.0.0.1:80");

        ep = Endpoint (AddressV4 (10,0,0,1));
        BEAST_EXPECT(AddressV4::get_class (ep.to_v4()) == 'A');
        BEAST_EXPECT(! is_unspecified (ep));
        BEAST_EXPECT(! is_public (ep));
        BEAST_EXPECT(  is_private (ep));
        BEAST_EXPECT(! is_multicast (ep));
        BEAST_EXPECT(! is_loopback (ep));
        BEAST_EXPECT(to_string (ep) == "10.0.0.1");

        ep = Endpoint (AddressV4 (166,78,151,147));
        BEAST_EXPECT(! is_unspecified (ep));
        BEAST_EXPECT(  is_public (ep));
        BEAST_EXPECT(! is_private (ep));
        BEAST_EXPECT(! is_multicast (ep));
        BEAST_EXPECT(! is_loopback (ep));
        BEAST_EXPECT(to_string (ep) == "166.78.151.147");

        {
            ep = Endpoint::from_string ("192.0.2.112");
            BEAST_EXPECT(! is_unspecified (ep));
            BEAST_EXPECT(ep == Endpoint::from_string_altform ("192.0.2.112"));

            auto const ep1 = Endpoint::from_string ("192.0.2.112:2016");
            BEAST_EXPECT(! is_unspecified (ep1));
            BEAST_EXPECT(ep.address() == ep1.address());
            BEAST_EXPECT(ep1.port() == 2016);

            auto const ep2 =
                Endpoint::from_string_altform ("192.0.2.112:2016");
            BEAST_EXPECT(! is_unspecified (ep2));
            BEAST_EXPECT(ep.address() == ep2.address());
            BEAST_EXPECT(ep2.port() == 2016);
            BEAST_EXPECT(ep1 == ep2);

            auto const ep3 =
                Endpoint::from_string_altform ("192.0.2.112 2016");
            BEAST_EXPECT(! is_unspecified (ep3));
            BEAST_EXPECT(ep.address() == ep3.address());
            BEAST_EXPECT(ep3.port() == 2016);
            BEAST_EXPECT(ep2 == ep3);

            auto const ep4 =
                Endpoint::from_string_altform ("192.0.2.112     2016");
            BEAST_EXPECT(! is_unspecified (ep4));
            BEAST_EXPECT(ep.address() == ep4.address());
            BEAST_EXPECT(ep4.port() == 2016);
            BEAST_EXPECT(ep3 == ep4);

            BEAST_EXPECT(to_string(ep1) == to_string(ep2));
            BEAST_EXPECT(to_string(ep1) == to_string(ep3));
            BEAST_EXPECT(to_string(ep1) == to_string(ep4));
        }

        // Failures:
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string ("192.0.2.112:port")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform ("192.0.2.112:port")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform ("192.0.2.112 port")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string ("ip:port")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform ("ip:port")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform ("ip port")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("255")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("255")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("512")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("512")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("1.2.3.256")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3.256")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("1.2.3:80")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3:80")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3 80")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("1.2.3.4:65536")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3:65536")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3 65536")));

        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string("1.2.3.4:89119")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3:89119")));
        BEAST_EXPECT(is_unspecified (
            Endpoint::from_string_altform("1.2.3 89119")));
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
        BEAST_EXPECT(parse (text, t));
        BEAST_EXPECT(to_string (t) == std::string (text));
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
        testcase (name);

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
        shouldFail <T> ("1.2.3:65536");
        shouldFail <T> ("1.2.3:72131");
    }

    void run () override
    {
        testAddressV4 ();
        testAddressV4Proxy();
        testAddress ();
        testEndpoint ();

        testParse <Endpoint> ("Parse Endpoint");
    }
};

BEAST_DEFINE_TESTSUITE(IPEndpoint,net,beast);

}
}
