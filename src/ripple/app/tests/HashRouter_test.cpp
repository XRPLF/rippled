//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/basics/chrono.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

class HashRouter_test : public beast::unit_test::suite
{
    void
    testNonExpiration()
    {
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, std::chrono::seconds(2));

        uint256 const key1(1);
        uint256 const key2(2);
        uint256 const key3(3);

        // t=0
        router.setFlags(key1, 11111);
        expect(router.getFlags(key1) == 11111);
        router.setFlags(key2, 22222);
        expect(router.getFlags(key2) == 22222);
        // key1 : 0
        // key2 : 0
        // key3: null

        ++stopwatch;

        // Because we are accessing key1 here, it
        // will NOT be expired for another two ticks
        expect(router.getFlags(key1) == 11111);
        // key1 : 1
        // key2 : 0
        // key3 null

        ++stopwatch;

        // t=3
        router.setFlags(key3,33333); // force expiration
        expect(router.getFlags(key1) == 11111);
        expect(router.getFlags(key2) == 0);
    }

    void
    testExpiration()
    {
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, std::chrono::seconds(2));

        uint256 const key1(1);
        uint256 const key2(2);
        uint256 const key3(3);
        uint256 const key4(4);
        expect(key1 != key2 &&
            key2 != key3 &&
            key3 != key4);

        // t=0
        router.setFlags(key1, 12345);
        expect(router.getFlags(key1) == 12345);
        // key1 : 0
        // key2 : null
        // key3 : null

        ++stopwatch;

        // Expiration is triggered by insertion,
        // and timestamps are updated on access,
        // so key1 will be expired after the second
        // call to setFlags.
        // t=1
        router.setFlags(key2, 9999);
        expect(router.getFlags(key1) == 12345);
        expect(router.getFlags(key2) == 9999);
        // key1 : 1
        // key2 : 1
        // key3 : null

        ++stopwatch;
        // t=2
        expect(router.getFlags(key2) == 9999);
        // key1 : 1
        // key2 : 2
        // key3 : null

        ++stopwatch;
        // t=3
        router.setFlags(key3, 2222);
        expect(router.getFlags(key1) == 0);
        expect(router.getFlags(key2) == 9999);
        expect(router.getFlags(key3) == 2222);
        // key1 : 3
        // key2 : 3
        // key3 : 3

        ++stopwatch;
        // t=4
        // No insertion, no expiration
        router.setFlags(key1, 7654);
        expect(router.getFlags(key1) == 7654);
        expect(router.getFlags(key2) == 9999);
        expect(router.getFlags(key3) == 2222);
        // key1 : 4
        // key2 : 4
        // key3 : 4

        ++stopwatch;
        ++stopwatch;

        // t=6
        router.setFlags(key4, 7890);
        expect(router.getFlags(key1) == 0);
        expect(router.getFlags(key2) == 0);
        expect(router.getFlags(key3) == 0);
        expect(router.getFlags(key4) == 7890);
        // key1 : 6
        // key2 : 6
        // key3 : 6
        // key4 : 6
    }

    void testSuppression()
    {
        // Normal HashRouter
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, std::chrono::seconds(2));

        uint256 const key1(1);
        uint256 const key2(2);
        uint256 const key3(3);
        uint256 const key4(4);
        expect(key1 != key2 &&
            key2 != key3 &&
            key3 != key4);

        int flags = 12345;  // This value is ignored
        router.addSuppression(key1);
        expect(router.addSuppressionPeer(key2, 15));
        expect(router.addSuppressionPeer(key3, 20, flags));
        expect(flags == 0);

        ++stopwatch;

        expect(!router.addSuppressionPeer(key1, 2));
        expect(!router.addSuppressionPeer(key2, 3));
        expect(!router.addSuppressionPeer(key3, 4, flags));
        expect(flags == 0);
        expect(router.addSuppressionPeer(key4, 5));
    }

    void
    testSetFlags()
    {
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, std::chrono::seconds(2));

        uint256 const key1(1);
        expect(router.setFlags(key1, 10));
        expect(!router.setFlags(key1, 10));
        expect(router.setFlags(key1, 20));
    }

    void
    testSwapSet()
    {
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, std::chrono::seconds(2));

        uint256 const key1(1);

        std::set<HashRouter::PeerShortID> peers1;
        std::set<HashRouter::PeerShortID> peers2;

        peers1.emplace(1);
        peers1.emplace(3);
        peers1.emplace(5);
        peers2.emplace(2);
        peers2.emplace(4);

        expect(router.swapSet(key1, peers1, 135));
        expect(peers1.empty());
        expect(router.getFlags(key1) == 135);
        // No action, because flag matches
        expect(!router.swapSet(key1, peers2, 135));
        expect(peers2.size() == 2);
        expect(router.getFlags(key1) == 135);
        // Do a swap
        expect(router.swapSet(key1, peers2, 24));
        expect(peers2.size() == 3);
        expect(router.getFlags(key1) == (135 | 24));
    }

public:

    void
    run()
    {
        testNonExpiration();
        testExpiration();
        testSuppression();
        testSetFlags();
        testSwapSet();
    }
};

BEAST_DEFINE_TESTSUITE(HashRouter, app, ripple)

}
}
