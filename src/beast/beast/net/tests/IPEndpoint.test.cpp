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
#include "../../BeastConfig.h"
#endif

#include <beast/net/IPEndpoint.h>
#include <beast/net/detail/Parse.h>

#include <beast/unit_test/suite.h>

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
        testcase ("AddressV4");

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
      testcase ("AddressV4::Proxy");

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
        testcase ("Address");

        std::pair <Address, bool> result (
            Address::from_string ("1.2.3.4"));
        expect (result.second);
        if (expect (result.first.is_v4 ()))
            expect (result.first.to_v4() == AddressV4 (1, 2, 3, 4));
    }

    //--------------------------------------------------------------------------

    void testEndpoint ()
    {
        testcase ("Endpoint");

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
    }

    void run ()
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
