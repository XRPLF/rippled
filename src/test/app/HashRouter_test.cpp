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

#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/core/Config.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test.h>

namespace ripple {
namespace test {

class HashRouter_test : public beast::unit_test::suite
{
    HashRouter::Setup
    getSetup(std::chrono::seconds hold, std::chrono::seconds relay)
    {
        HashRouter::Setup setup;
        setup.holdTime = hold;
        setup.relayTime = relay;
        return setup;
    }

    void
    testNonExpiration()
    {
        testcase("Non-expiration");
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(getSetup(2s, 1s), stopwatch);

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
        testcase("Expiration");
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(getSetup(2s, 1s), stopwatch);

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
        testcase("Suppression");
        // Normal HashRouter
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(getSetup(2s, 1s), stopwatch);

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
        testcase("Set Flags");
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(getSetup(2s, 1s), stopwatch);

        uint256 const key1(1);
        BEAST_EXPECT(router.setFlags(key1, 10));
        BEAST_EXPECT(!router.setFlags(key1, 10));
        BEAST_EXPECT(router.setFlags(key1, 20));
    }

    void
    testRelay()
    {
        testcase("Relay");
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(getSetup(50s, 1s), stopwatch);

        uint256 const key1(1);

        std::optional<std::set<HashRouter::PeerShortID>> peers;

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
    testProcess()
    {
        testcase("Process");
        using namespace std::chrono_literals;
        TestStopwatch stopwatch;
        HashRouter router(getSetup(5s, 1s), stopwatch);
        uint256 const key(1);
        HashRouter::PeerShortID peer = 1;
        int flags;

        BEAST_EXPECT(router.shouldProcess(key, peer, flags, 1s));
        BEAST_EXPECT(!router.shouldProcess(key, peer, flags, 1s));
        ++stopwatch;
        ++stopwatch;
        BEAST_EXPECT(router.shouldProcess(key, peer, flags, 1s));
    }

    void
    testSetup()
    {
        testcase("setup_HashRouter");

        using namespace std::chrono_literals;
        {
            Config cfg;
            // default
            auto const setup = setup_HashRouter(cfg);
            BEAST_EXPECT(setup.holdTime == 300s);
            BEAST_EXPECT(setup.relayTime == 30s);
        }
        {
            Config cfg;
            // non-default
            auto& h = cfg.section("hashrouter");
            h.set("hold_time", "600");
            h.set("relay_time", "15");
            auto const setup = setup_HashRouter(cfg);
            BEAST_EXPECT(setup.holdTime == 600s);
            BEAST_EXPECT(setup.relayTime == 15s);
        }
        {
            Config cfg;
            // equal
            auto& h = cfg.section("hashrouter");
            h.set("hold_time", "400");
            h.set("relay_time", "400");
            auto const setup = setup_HashRouter(cfg);
            BEAST_EXPECT(setup.holdTime == 400s);
            BEAST_EXPECT(setup.relayTime == 400s);
        }
        {
            Config cfg;
            // wrong order
            auto& h = cfg.section("hashrouter");
            h.set("hold_time", "60");
            h.set("relay_time", "120");
            try
            {
                setup_HashRouter(cfg);
                fail();
            }
            catch (std::exception const& e)
            {
                std::string expected =
                    "HashRouter relay time must be less than or equal to hold "
                    "time";
                BEAST_EXPECT(e.what() == expected);
            }
        }
        {
            Config cfg;
            // too small hold
            auto& h = cfg.section("hashrouter");
            h.set("hold_time", "10");
            h.set("relay_time", "120");
            try
            {
                setup_HashRouter(cfg);
                fail();
            }
            catch (std::exception const& e)
            {
                std::string expected =
                    "HashRouter hold time must be at least 12 seconds (the "
                    "approximate validation time for three "
                    "ledgers).";
                BEAST_EXPECT(e.what() == expected);
            }
        }
        {
            Config cfg;
            // too small relay
            auto& h = cfg.section("hashrouter");
            h.set("hold_time", "500");
            h.set("relay_time", "6");
            try
            {
                setup_HashRouter(cfg);
                fail();
            }
            catch (std::exception const& e)
            {
                std::string expected =
                    "HashRouter relay time must be at least 8 seconds (the "
                    "approximate validation time for two ledgers).";
                BEAST_EXPECT(e.what() == expected);
            }
        }
        {
            Config cfg;
            // garbage
            auto& h = cfg.section("hashrouter");
            h.set("hold_time", "alice");
            h.set("relay_time", "bob");
            auto const setup = setup_HashRouter(cfg);
            // The set function ignores values that don't covert, so the
            // defaults are left unchanged
            BEAST_EXPECT(setup.holdTime == 300s);
            BEAST_EXPECT(setup.relayTime == 30s);
        }
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
        testProcess();
        testSetup();
    }
};

BEAST_DEFINE_TESTSUITE(HashRouter, app, ripple);

}  // namespace test
}  // namespace ripple
