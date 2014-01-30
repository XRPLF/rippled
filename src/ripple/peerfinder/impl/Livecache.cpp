//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

namespace ripple {
namespace PeerFinder {

class LivecacheTests : public UnitTest
{
public:
    ManualClock m_clock_source;

    // Add the address as an endpoint
    void add (uint32 index, uint16 port, Livecache& c)
    {
        Endpoint ep;
        ep.hops = 0;
        ep.address = IPAddress (
            IP::AddressV4 (index), port);
        c.insert (ep);
    }

    void testFetch ()
    {
        beginTestCase ("fetch");

        Livecache c (m_clock_source, Journal());

        add (1, 1, c);
        add (2, 1, c);
        add (3, 1, c);
        add (4, 1, c);
        add (4, 2, c);
        add (4, 3, c);
        add (5, 1, c);
        add (6, 1, c);
        add (6, 2, c);
        add (7, 1, c);

        Endpoints const eps (c.fetch_unique ());

        struct IsAddr
        {
            explicit IsAddr (uint32 index_)
                : index (index_)
                { }
            bool operator() (Endpoint const& ep) const
                { return ep.address.to_v4().value == index; }
            uint32 index;
        };

        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (1)) == 1);
        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (2)) == 1);
        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (3)) == 1);
        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (4)) == 1);
        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (5)) == 1);
        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (6)) == 1);
        expect (std::count_if (
            eps.begin(), eps.end(), IsAddr (7)) == 1);

        pass();
    }

    void runTest ()
    {
        testFetch ();
    }

    LivecacheTests () : UnitTest ("PeerFinder:Livecache", "ripple")
    {
    }
};

static LivecacheTests livecacheTests;

}
}
