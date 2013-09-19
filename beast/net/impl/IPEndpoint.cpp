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
inline std::istream& operator>> (std::istream &is, IPEndpoint& ep)
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
