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

#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/detail/Logic.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

namespace ripple {
namespace PeerFinder {

class PeerFinder_test : public beast::unit_test::suite
{
    test::SuiteJournal journal_;

public:
    PeerFinder_test() : journal_("PeerFinder_test", *this)
    {
    }

    struct TestStore : Store
    {
        std::size_t
        load(load_callback const& cb) override
        {
            return 0;
        }

        void
        save(std::vector<Entry> const&) override
        {
        }
    };

    struct TestChecker
    {
        void
        stop()
        {
        }

        void
        wait()
        {
        }

        template <class Handler>
        void
        async_connect(beast::IP::Endpoint const& ep, Handler&& handler)
        {
            boost::system::error_code ec;
            handler(ep, ep, ec);
        }
    };

    void
    test_backoff1()
    {
        auto const seconds = 10000;
        testcase("backoff 1");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        logic.addFixedPeer(
            "test", beast::IP::Endpoint::from_string("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            logic.config(c);
        }
        std::size_t n = 0;
        for (std::size_t i = 0; i < seconds; ++i)
        {
            auto const list = logic.autoconnect();
            if (!list.empty())
            {
                BEAST_EXPECT(list.size() == 1);
                auto const slot = logic.new_outbound_slot(list.front());
                BEAST_EXPECT(logic.onConnected(
                    slot, beast::IP::Endpoint::from_string("65.0.0.2:5")));
                logic.on_closed(slot);
                ++n;
            }
            clock.advance(std::chrono::seconds(1));
            logic.once_per_second();
        }
        // Less than 20 attempts
        BEAST_EXPECT(n < 20);
    }

    // with activate
    void
    test_backoff2()
    {
        auto const seconds = 10000;
        testcase("backoff 2");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        logic.addFixedPeer(
            "test", beast::IP::Endpoint::from_string("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            logic.config(c);
        }

        PublicKey const pk(randomKeyPair(KeyType::secp256k1).first);
        std::size_t n = 0;

        for (std::size_t i = 0; i < seconds; ++i)
        {
            auto const list = logic.autoconnect();
            if (!list.empty())
            {
                BEAST_EXPECT(list.size() == 1);
                auto const slot = logic.new_outbound_slot(list.front());
                if (!BEAST_EXPECT(logic.onConnected(
                        slot, beast::IP::Endpoint::from_string("65.0.0.2:5"))))
                    return;
                std::string s = ".";
                if (!BEAST_EXPECT(
                        logic.activate(slot, pk, false) ==
                        PeerFinder::Result::success))
                    return;
                logic.on_closed(slot);
                ++n;
            }
            clock.advance(std::chrono::seconds(1));
            logic.once_per_second();
        }
        // No more often than once per minute
        BEAST_EXPECT(n <= (seconds + 59) / 60);
    }

    void
    test_duplicateOutIn()
    {
        testcase("duplicate out/in");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        logic.addFixedPeer(
            "test", beast::IP::Endpoint::from_string("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const list = logic.autoconnect();
        if (BEAST_EXPECT(!list.empty()))
        {
            BEAST_EXPECT(list.size() == 1);
            auto const remote = list.front();
            auto const slot1 = logic.new_outbound_slot(remote);
            if (BEAST_EXPECT(slot1 != nullptr))
            {
                BEAST_EXPECT(
                    logic.connectedAddresses_.count(remote.address()) == 1);
                auto const local =
                    beast::IP::Endpoint::from_string("65.0.0.2:1024");
                auto const slot2 = logic.new_inbound_slot(local, remote);
                BEAST_EXPECT(
                    logic.connectedAddresses_.count(remote.address()) == 1);
                if (!BEAST_EXPECT(slot2 == nullptr))
                    logic.on_closed(slot2);
                logic.on_closed(slot1);
            }
        }
    }

    void
    test_duplicateInOut()
    {
        testcase("duplicate in/out");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        logic.addFixedPeer(
            "test", beast::IP::Endpoint::from_string("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const list = logic.autoconnect();
        if (BEAST_EXPECT(!list.empty()))
        {
            BEAST_EXPECT(list.size() == 1);
            auto const remote = list.front();
            auto const local =
                beast::IP::Endpoint::from_string("65.0.0.2:1024");
            auto const slot1 = logic.new_inbound_slot(local, remote);
            if (BEAST_EXPECT(slot1 != nullptr))
            {
                BEAST_EXPECT(
                    logic.connectedAddresses_.count(remote.address()) == 1);
                auto const slot2 = logic.new_outbound_slot(remote);
                BEAST_EXPECT(
                    logic.connectedAddresses_.count(remote.address()) == 1);
                if (!BEAST_EXPECT(slot2 == nullptr))
                    logic.on_closed(slot2);
                logic.on_closed(slot1);
            }
        }
    }

    void
    test_peer_config()
    {
        struct TestCase
        {
            std::string name;
            std::optional<std::uint16_t> maxPeers;
            std::optional<std::uint16_t> maxIn;
            std::optional<std::uint16_t> maxOut;
            std::uint16_t port;
            std::uint16_t expectOut;
            std::uint16_t expectIn;
            std::uint16_t expectIpLimit;
        };

        // if peers_max is configured then peers_in_max and peers_out_max are
        // ignored
        auto run = [&](TestCase const& tc) {
            ripple::Config c;

            testcase(tc.name);

            std::string toLoad = "";
            int max = 0;
            if (tc.maxPeers)
            {
                max = tc.maxPeers.value();
                toLoad += "[peers_max]\n" + std::to_string(max) + "\n" +
                    "[peers_in_max]\n" + std::to_string(tc.maxIn.value_or(0)) +
                    "\n" + "[peers_out_max]\n" +
                    std::to_string(tc.maxOut.value_or(0)) + "\n";
            }
            else if (tc.maxIn && tc.maxOut)
            {
                toLoad += "[peers_in_max]\n" + std::to_string(*tc.maxIn) +
                    "\n" + "[peers_out_max]\n" + std::to_string(*tc.maxOut) +
                    "\n";
            }

            c.loadFromString(toLoad);
            BEAST_EXPECT(
                (c.PEERS_MAX == max && c.PEERS_IN_MAX == 0 &&
                 c.PEERS_OUT_MAX == 0) ||
                (c.PEERS_IN_MAX == *tc.maxIn && c.PEERS_OUT_MAX == *tc.maxOut));

            Config config = Config::makeConfig(c, tc.port, false, 0);

            Counts counts;
            counts.onConfig(config);
            BEAST_EXPECT(
                counts.out_max() == tc.expectOut &&
                counts.inboundSlots() == tc.expectIn &&
                config.ipLimit == tc.expectIpLimit);
        };

        // if max_peers == 0 => maxPeers = 21,
        //   else if max_peers < 10 => maxPeers = 10 else maxPeers = max_peers
        // expectOut => if legacy => max(0.15 * maxPeers, 10),
        //   if legacy && !wantIncoming => maxPeers else max_out_peers
        // expectIn => if legacy && wantIncoming => maxPeers - outPeers
        //   else if !wantIncoming => 0 else max_in_peers
        // ipLimit => if expectIn <= 21 => 2 else 2 + min(5, expectIn/21)
        // ipLimit = max(1, min(ipLimit, expectIn/2))

        auto testcases = {

            // legacy test with max_peers
            TestCase{
                .name = "legacy no config",
                .maxPeers = {},
                .maxIn = {},
                .maxOut = {},
                .port = 4000,
                .expectOut = 10,
                .expectIn = 11,
                .expectIpLimit = 2,
            },
            TestCase{
                .name = "legacy max_peers 0",
                .maxPeers = 0,
                .maxIn = 100,
                .maxOut = 10,
                .port = 4000,
                .expectOut = 10,
                .expectIn = 11,
                .expectIpLimit = 2,
            },
            TestCase{
                .name = "legacy max_peers 5",
                .maxPeers = 5,
                .maxIn = 100,
                .maxOut = 10,
                .port = 4000,
                .expectOut = 10,
                .expectIn = 0,
                .expectIpLimit = 1,
            },
            TestCase{
                .name = "legacy max_peers 20",
                .maxPeers = 20,
                .maxIn = 100,
                .maxOut = 10,
                .port = 4000,
                .expectOut = 10,
                .expectIn = 10,
                .expectIpLimit = 2,
            },
            TestCase{
                .name = "legacy max_peers 100",
                .maxPeers = 100,
                .maxIn = 100,
                .maxOut = 10,
                .port = 4000,
                .expectOut = 15,
                .expectIn = 85,
                .expectIpLimit = 6,
            },
            TestCase{
                .name = "legacy max_peers 20, private",
                .maxPeers = 20,
                .maxIn = 100,
                .maxOut = 10,
                .port = 0,
                .expectOut = 20,
                .expectIn = 0,
                .expectIpLimit = 1,
            },

            // test with max_in_peers and max_out_peers
            TestCase{
                .name = "new in 100/out 10",
                .maxPeers = {},
                .maxIn = 100,
                .maxOut = 10,
                .port = 4000,
                .expectOut = 10,
                .expectIn = 100,
                .expectIpLimit = 6,
            },
            TestCase{
                .name = "new in 0/out 10",
                .maxPeers = {},
                .maxIn = 0,
                .maxOut = 10,
                .port = 4000,
                .expectOut = 10,
                .expectIn = 0,
                .expectIpLimit = 1,
            },
            TestCase{
                .name = "new in 100/out 10, private",
                .maxPeers = {},
                .maxIn = 100,
                .maxOut = 10,
                .port = 0,
                .expectOut = 10,
                .expectIn = 0,
                .expectIpLimit = 6,
            }};

        for (auto const& tc : testcases)
            run(tc);
    }

    void
    test_private_ip_config()
    {
        testcase("private_ip_config");
        auto run = [&](std::string const& toLoad) {
            ripple::Config c;
            c.loadFromString(toLoad);
            Config config = Config::makeConfig(c, 0, false, 0);

            BEAST_EXPECT(
                config.allowPrivateEndpoints == c.ALLOW_PRIVATE_ENDPOINTS);
        };
        run(R"rippleConfig(
[allow_private_endpoints]
true
)rippleConfig");

        run(R"rippleConfig(
[allow_private_endpoints]
false
)rippleConfig");

        run(R"rippleConfig()rippleConfig");
    }

    void
    test_invalid_config()
    {
        testcase("invalid config");

        auto run = [&](std::string const& toLoad) {
            ripple::Config c;
            try
            {
                c.loadFromString(toLoad);
                fail();
            }
            catch (...)
            {
                pass();
            }
        };
        run(R"rippleConfig(
[peers_in_max]
100
)rippleConfig");
        run(R"rippleConfig(
[peers_out_max]
100
)rippleConfig");
        run(R"rippleConfig(
[peers_in_max]
100
[peers_out_max]
5
)rippleConfig");
        run(R"rippleConfig(
[peers_in_max]
1001
[peers_out_max]
10
)rippleConfig");
        run(R"rippleConfig(
[peers_in_max]
10
[peers_out_max]
1001
)rippleConfig");
    }

    void
    test_preprocess()
    {
        struct TestCase
        {
            std::string name;
            bool allowPrivateEndpoints;
            Endpoints endpoints;
            int expectedSize;
        };

        auto run = [&](TestCase tc) {
            testcase(tc.name);
            TestStore store;
            TestChecker checker;
            TestStopwatch clock;
            Config c{};
            c.allowPrivateEndpoints = tc.allowPrivateEndpoints;

            Logic<TestChecker> logic(clock, store, checker, journal_);
            logic.config(c);

            auto slot = logic.new_outbound_slot(
                beast::IP::Endpoint::from_string("65.0.0.1:5"));

            logic.preprocess(slot, tc.endpoints);

            BEAST_EXPECT(tc.endpoints.size() == tc.expectedSize);
        };

        auto testcases = {
            TestCase{
                .name = "remove private IP",
                .allowPrivateEndpoints = false,
                .endpoints =
                    Endpoints{
                        Endpoint{
                            beast::IP::Endpoint::from_string("10.1.1.1:5"), 1},
                        Endpoint{
                            beast::IP::Endpoint::from_string("300.1.1.1:5"), 1},
                        Endpoint{
                            beast::IP::Endpoint::from_string("65.1.1.1:5"), 1},
                    },
                .expectedSize = 1,
            },
            TestCase{
                .name = "allow private IPs",
                .allowPrivateEndpoints = true,
                .endpoints =
                    Endpoints{
                        Endpoint{
                            beast::IP::Endpoint::from_string("10.1.1.1:5"), 1},
                        Endpoint{
                            beast::IP::Endpoint::from_string("300.1.1.1:5"), 1},
                        Endpoint{
                            beast::IP::Endpoint::from_string("65.1.1.1:5"), 1},
                    },
                .expectedSize = 2,
            }};

        for (auto const& tc : testcases)
            run(tc);
    }

    void
    run() override
    {
        test_backoff1();
        test_backoff2();
        test_duplicateOutIn();
        test_duplicateInOut();
        test_peer_config();
        test_private_ip_config();
        test_invalid_config();
        test_preprocess();
    }
};

BEAST_DEFINE_TESTSUITE(PeerFinder, PeerFinder, ripple);

}  // namespace PeerFinder
}  // namespace ripple
