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

#include <ripple/basics/Slice.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/Config.h>
#include <ripple/peerfinder/impl/Logic.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <test/unit_test/SuiteJournal.h>

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
    test_config()
    {
        // if peers_max is configured then peers_in_max and peers_out_max are
        // ignored
        auto run = [&](std::string const& test,
                       std::optional<std::uint16_t> maxPeers,
                       std::optional<std::uint16_t> maxIn,
                       std::optional<std::uint16_t> maxOut,
                       std::uint16_t port,
                       std::uint16_t expectOut,
                       std::uint16_t expectIn,
                       std::uint16_t expectIpLimit) {
            ripple::Config c;

            testcase(test);

            std::string toLoad = "";
            int max = 0;
            if (maxPeers)
            {
                max = maxPeers.value();
                toLoad += "[peers_max]\n" + std::to_string(max) + "\n" +
                    "[peers_in_max]\n" + std::to_string(maxIn.value_or(0)) + "\n" +
                    "[peers_out_max]\n" + std::to_string(maxOut.value_or(0)) + "\n";
            }
            else if (maxIn && maxOut)
            {
                toLoad += "[peers_in_max]\n" + std::to_string(*maxIn) + "\n" +
                    "[peers_out_max]\n" + std::to_string(*maxOut) + "\n";
            }

            c.loadFromString(toLoad);
            BEAST_EXPECT(
                (c.PEERS_MAX == max &&
                 c.PEERS_IN_MAX == 0 && c.PEERS_OUT_MAX == 0) ||
                (c.PEERS_IN_MAX == *maxIn &&
                 c.PEERS_OUT_MAX == *maxOut));

            Config config = Config::makeConfig(c, port, false, 0);

            Counts counts;
            counts.onConfig(config);
            BEAST_EXPECT(
                counts.out_max() == expectOut &&
                counts.inboundSlots() == expectIn &&
                config.ipLimit == expectIpLimit);
        };

        // if max_peers == 0 => maxPeers = 21,
        //   else if max_peers < 10 => maxPeers = 10 else maxPeers = max_peers
        // expectOut => if legacy => max(0.15 * maxPeers, 10),
        //   if legacy && !wantIncoming => maxPeers else max_out_peers
        // expectIn => if legacy && wantIncoming => maxPeers - outPeers
        //   else if !wantIncoming => 0 else max_in_peers
        // ipLimit => if expectIn <= 21 => 2 else 2 + min(5, expectIn/21)
        // ipLimit = max(1, min(ipLimit, expectIn/2))

        // legacy test with max_peers
        run("legacy no config", {}, {}, {}, 4000, 10, 11, 2);
        run("legacy max_peers 0", 0, 100, 10, 4000, 10, 11, 2);
        run("legacy max_peers 5", 5, 100, 10, 4000, 10, 0, 1);
        run("legacy max_peers 20", 20, 100, 10, 4000, 10, 10, 2);
        run("legacy max_peers 100", 100, 100, 10, 4000, 15, 85, 6);
        run("legacy max_peers 20, private", 20, 100, 10, 0, 20, 0, 1);

        // test with max_in_peers and max_out_peers
        run("new in 100/out 10", {}, 100, 10, 4000, 10, 100, 6);
        run("new in 0/out 10", {}, 0, 10, 4000, 10, 0, 1);
        run("new in 100/out 10, private", {}, 100, 10, 0, 10, 0, 6);
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
    run() override
    {
        test_backoff1();
        test_backoff2();
        test_config();
        test_invalid_config();
    }
};

BEAST_DEFINE_TESTSUITE(PeerFinder, PeerFinder, ripple);

}  // namespace PeerFinder
}  // namespace ripple
