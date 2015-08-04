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
#include <ripple/basics/UptimeTimer.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

class HashRouter_test : public beast::unit_test::suite
{
    void
    testExpiration()
    {
        HashRouter router(2);

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

        UptimeTimer::getInstance().incrementElapsedTime();

        // Expiration is triggered by insertion, so
        // key1 will be expired after the second
        // call to setFlags.
        // t=1
        router.setFlags(key2, 9999);
        expect(router.getFlags(key1) == 12345);
        expect(router.getFlags(key2) == 9999);
        // key1 : 0
        // key2 : 1
        // key3 : null

        UptimeTimer::getInstance().incrementElapsedTime();

        // t=2
        router.setFlags(key3, 2222);
        expect(router.getFlags(key1) == 0);
        expect(router.getFlags(key2) == 9999);
        expect(router.getFlags(key3) == 2222);
        // key1 : 2
        // key2 : 1
        // key3 : 2

        UptimeTimer::getInstance().incrementElapsedTime();

        // t=3
        // No insertion, no expiration
        router.setFlags(key1, 7654);
        expect(router.getFlags(key1) == 7654);
        expect(router.getFlags(key2) == 9999);
        expect(router.getFlags(key3) == 2222);
        // key1 : 2
        // key2 : 1
        // key3 : 2

        UptimeTimer::getInstance().incrementElapsedTime();
        UptimeTimer::getInstance().incrementElapsedTime();

        // t=5
        router.setFlags(key4, 7890);
        expect(router.getFlags(key1) == 0);
        expect(router.getFlags(key2) == 0);
        expect(router.getFlags(key3) == 0);
        expect(router.getFlags(key4) == 7890);
        // key1 : 5
        // key2 : 5
        // key3 : 5
        // key4 : 5
    }

    void testSuppression()
    {
        // Normal HashRouter
        HashRouter router(2);

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

        UptimeTimer::getInstance().incrementElapsedTime();

        expect(!router.addSuppressionPeer(key1, 2));
        expect(!router.addSuppressionPeer(key2, 3));
        expect(!router.addSuppressionPeer(key3, 4, flags));
        expect(flags == 0);
        expect(router.addSuppressionPeer(key4, 5));
    }

    void
    testSetFlags()
    {
        HashRouter router(2);

        uint256 const key1(1);
        expect(router.setFlags(key1, 10));
        expect(!router.setFlags(key1, 10));
        expect(router.setFlags(key1, 20));
    }

    void
    testSwapSet()
    {
        HashRouter router(2);

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
        UptimeTimer::getInstance().beginManualUpdates();

        testExpiration();
        testSuppression();
        testSetFlags();
        testSwapSet();

        UptimeTimer::getInstance().endManualUpdates();
    }
};

BEAST_DEFINE_TESTSUITE(HashRouter, app, ripple)

}
}