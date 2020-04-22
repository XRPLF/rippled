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
        HashRouter router(stopwatch, 2s, 2);

        uint256 const key1(1);
        uint256 const key2(2);
        uint256 const key3(3);

        // t=0
        router.setFlags(key1, 11111);
        BEAST_EXPECT(router.getFlags(key1) == 11111);
        router.setFlags(key2, 22222);
        BEAST_EXPECT(router.getFlags(key2) == 22222);
        // key1 : 0
        // key2 : 0
        // key3: null

        ++stopwatch;

        // Because we are accessing key1 here, it
        // will NOT be expired for another two ticks
        BEAST_EXPECT(router.getFlags(key1) == 11111);
        // key1 : 1
        // key2 : 0
        // key3 null

        ++stopwatch;

        // t=3
        router.setFlags(key3, 33333);  // force expiration
        BEAST_EXPECT(router.getFlags(key1) == 11111);
        BEAST_EXPECT(router.getFlags(key2) == 0);
    }

    void
    testExpiration()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s, 2);

        uint256 const key1(1);
        uint256 const key2(2);
        uint256 const key3(3);
        uint256 const key4(4);
        BEAST_EXPECT(key1 != key2 && key2 != key3 && key3 != key4);

        // t=0
        router.setFlags(key1, 12345);
        BEAST_EXPECT(router.getFlags(key1) == 12345);
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
        BEAST_EXPECT(router.getFlags(key1) == 12345);
        BEAST_EXPECT(router.getFlags(key2) == 9999);
        // key1 : 1
        // key2 : 1
        // key3 : null

        ++stopwatch;
        // t=2
        BEAST_EXPECT(router.getFlags(key2) == 9999);
        // key1 : 1
        // key2 : 2
        // key3 : null

        ++stopwatch;
        // t=3
        router.setFlags(key3, 2222);
        BEAST_EXPECT(router.getFlags(key1) == 0);
        BEAST_EXPECT(router.getFlags(key2) == 9999);
        BEAST_EXPECT(router.getFlags(key3) == 2222);
        // key1 : 3
        // key2 : 3
        // key3 : 3

        ++stopwatch;
        // t=4
        // No insertion, no expiration
        router.setFlags(key1, 7654);
        BEAST_EXPECT(router.getFlags(key1) == 7654);
        BEAST_EXPECT(router.getFlags(key2) == 9999);
        BEAST_EXPECT(router.getFlags(key3) == 2222);
        // key1 : 4
        // key2 : 4
        // key3 : 4

        ++stopwatch;
        ++stopwatch;

        // t=6
        router.setFlags(key4, 7890);
        BEAST_EXPECT(router.getFlags(key1) == 0);
        BEAST_EXPECT(router.getFlags(key2) == 0);
        BEAST_EXPECT(router.getFlags(key3) == 0);
        BEAST_EXPECT(router.getFlags(key4) == 7890);
        // key1 : 6
        // key2 : 6
        // key3 : 6
        // key4 : 6
    }

    void
    testSuppression()
    {
        // Normal HashRouter
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s, 2);

        uint256 const key1(1);
        uint256 const key2(2);
        uint256 const key3(3);
        uint256 const key4(4);
        BEAST_EXPECT(key1 != key2 && key2 != key3 && key3 != key4);

        int flags = 12345;  // This value is ignored
        router.addSuppression(key1);
        BEAST_EXPECT(router.addSuppressionPeer(key2, 15));
        BEAST_EXPECT(router.addSuppressionPeer(key3, 20, flags));
        BEAST_EXPECT(flags == 0);

        ++stopwatch;

        BEAST_EXPECT(!router.addSuppressionPeer(key1, 2));
        BEAST_EXPECT(!router.addSuppressionPeer(key2, 3));
        BEAST_EXPECT(!router.addSuppressionPeer(key3, 4, flags));
        BEAST_EXPECT(flags == 0);
        BEAST_EXPECT(router.addSuppressionPeer(key4, 5));
    }

    void
    testSetFlags()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 2s, 2);

        uint256 const key1(1);
        BEAST_EXPECT(router.setFlags(key1, 10));
        BEAST_EXPECT(!router.setFlags(key1, 10));
        BEAST_EXPECT(router.setFlags(key1, 20));
    }

    void
    testRelay()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 1s, 2);

        uint256 const key1(1);

        boost::optional<std::set<HashRouter::PeerShortID>> peers;

        peers = router.shouldRelay(key1);
        BEAST_EXPECT(peers && peers->empty());
        router.addSuppressionPeer(key1, 1);
        router.addSuppressionPeer(key1, 3);
        router.addSuppressionPeer(key1, 5);
        // No action, because relayed
        BEAST_EXPECT(!router.shouldRelay(key1));
        // Expire, but since the next search will
        // be for this entry, it will get refreshed
        // instead. However, the relay won't.
        ++stopwatch;
        // Get those peers we added earlier
        peers = router.shouldRelay(key1);
        BEAST_EXPECT(peers && peers->size() == 3);
        router.addSuppressionPeer(key1, 2);
        router.addSuppressionPeer(key1, 4);
        // No action, because relayed
        BEAST_EXPECT(!router.shouldRelay(key1));
        // Expire, but since the next search will
        // be for this entry, it will get refreshed
        // instead. However, the relay won't.
        ++stopwatch;
        // Relay again
        peers = router.shouldRelay(key1);
        BEAST_EXPECT(peers && peers->size() == 2);
        // Expire again
        ++stopwatch;
        // Confirm that peers list is empty.
        peers = router.shouldRelay(key1);
        BEAST_EXPECT(peers && peers->size() == 0);
    }

    void
    testRecover()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 1s, 5);

        uint256 const key1(1);

        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(!router.shouldRecover(key1));
        // Expire, but since the next search will
        // be for this entry, it will get refreshed
        // instead.
        ++stopwatch;
        BEAST_EXPECT(router.shouldRecover(key1));
        // Expire, but since the next search will
        // be for this entry, it will get refreshed
        // instead.
        ++stopwatch;
        // Recover again. Recovery is independent of
        // time as long as the entry doesn't expire.
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(router.shouldRecover(key1));
        // Expire again
        ++stopwatch;
        BEAST_EXPECT(router.shouldRecover(key1));
        BEAST_EXPECT(!router.shouldRecover(key1));
    }

    void
    testProcess()
    {
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(stopwatch, 5s, 5);
        uint256 const key(1);
        HashRouter::PeerShortID peer = 1;
        int flags;

        BEAST_EXPECT(router.shouldProcess(key, peer, flags, 1s));
        BEAST_EXPECT(!router.shouldProcess(key, peer, flags, 1s));
        ++stopwatch;
        ++stopwatch;
        BEAST_EXPECT(router.shouldProcess(key, peer, flags, 1s));
    }

public:
    void
    run() override
    {
        testNonExpiration();
        testExpiration();
        testSuppression();
        testSetFlags();
        testRelay();
        testRecover();
        testProcess();
    }
};

BEAST_DEFINE_TESTSUITE(HashRouter, app, ripple);

}  // namespace test
}  // namespace ripple
