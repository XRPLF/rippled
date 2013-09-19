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

class IPEndpointTests : public UnitTest
{
public:
    void testPrint ()
    {
        beginTestCase ("addresses");

        IPEndpoint ep;

        ep = IPEndpoint(IPEndpoint::V4(127,0,0,1)).withPort (80);
        expect (!ep.isPublic());
        expect ( ep.isPrivate());
        expect (!ep.isBroadcast());
        expect (!ep.isMulticast());
        expect (!ep.isLoopback());
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
    }

    IPEndpointTests () : UnitTest ("IPEndpoint", "beast")
    {
    }
};

static IPEndpointTests ipEndpointTests;

}
