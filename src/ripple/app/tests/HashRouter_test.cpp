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
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class HashRouter_test : public beast::unit_test::suite
{
    void
    testNonExpiration()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s);

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
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s);

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
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s);

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
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s);

        uint256 const key1(1);
        expect(router.setFlags(key1, 10));
        expect(!router.setFlags(key1, 10));
        expect(router.setFlags(key1, 20));
    }

    void
    testRelay()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 1s);

        uint256 const key1(1);

        boost::optional<std::set<HashRouter::PeerShortID>> peers;

        peers = router.shouldRelay(key1);
        expect(peers && peers->empty());
        router.addSuppressionPeer(key1, 1);
        router.addSuppressionPeer(key1, 3);
        router.addSuppressionPeer(key1, 5);
        // No action, because relayed
        expect(!router.shouldRelay(key1));
        // Expire, but since the next search will
        // be for this entry, it will get refreshed
        // instead. However, the relay won't.
        ++stopwatch;
        // Get those peers we added earlier
        peers = router.shouldRelay(key1);
        expect(peers && peers->size() == 3);
        router.addSuppressionPeer(key1, 2);
        router.addSuppressionPeer(key1, 4);
        // No action, because relayed
        expect(!router.shouldRelay(key1));
        // Expire, but since the next search will
        // be for this entry, it will get refreshed
        // instead. However, the relay won't.
        ++stopwatch;
        // Relay again
        peers = router.shouldRelay(key1);
        expect(peers && peers->size() == 2);
        // Expire again
        ++stopwatch;
        // Confirm that peers list is empty.
        peers = router.shouldRelay(key1);
        expect(peers && peers->size() == 0);
    }

public:

    void
    run()
    {
        testNonExpiration();
        testExpiration();
        testSuppression();
        testSetFlags();
        testRelay();
    }
};

BEAST_DEFINE_TESTSUITE(HashRouter, app, ripple);

}
}
