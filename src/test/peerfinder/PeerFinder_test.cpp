#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
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
                auto const [slot, _] = logic.new_outbound_slot(list.front());
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
                auto const [slot, _] = logic.new_outbound_slot(list.front());
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

    // test accepting an incoming slot for an already existing outgoing slot
    void
    test_duplicateOutIn()
    {
        testcase("duplicate out/in");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const remote = beast::IP::Endpoint::from_string("65.0.0.1:5");
        auto const [slot1, r] = logic.new_outbound_slot(remote);
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r == Result::success);
        BEAST_EXPECT(logic.connectedAddresses_.count(remote.address()) == 1);

        auto const local = beast::IP::Endpoint::from_string("65.0.0.2:1024");
        auto const [slot2, r2] = logic.new_inbound_slot(local, remote);
        BEAST_EXPECT(logic.connectedAddresses_.count(remote.address()) == 1);
        BEAST_EXPECT(r2 == Result::duplicatePeer);

        if (!BEAST_EXPECT(slot2 == nullptr))
            logic.on_closed(slot2);

        logic.on_closed(slot1);
    }

    // test establishing outgoing slot for an already existing incoming slot
    void
    test_duplicateInOut()
    {
        testcase("duplicate in/out");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const remote = beast::IP::Endpoint::from_string("65.0.0.1:5");
        auto const local = beast::IP::Endpoint::from_string("65.0.0.2:1024");

        auto const [slot1, r] = logic.new_inbound_slot(local, remote);
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r == Result::success);
        BEAST_EXPECT(logic.connectedAddresses_.count(remote.address()) == 1);

        auto const [slot2, r2] = logic.new_outbound_slot(remote);
        BEAST_EXPECT(r2 == Result::duplicatePeer);
        BEAST_EXPECT(logic.connectedAddresses_.count(remote.address()) == 1);
        if (!BEAST_EXPECT(slot2 == nullptr))
            logic.on_closed(slot2);
        logic.on_closed(slot1);
    }

    void
    test_peerLimitExceeded()
    {
        testcase("peer limit exceeded");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const local = beast::IP::Endpoint::from_string("65.0.0.2:1024");
        auto const [slot, r] = logic.new_inbound_slot(
            local, beast::IP::Endpoint::from_string("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(r == Result::success);

        auto const [slot1, r1] = logic.new_inbound_slot(
            local, beast::IP::Endpoint::from_string("55.104.0.2:1026"));
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r1 == Result::success);

        auto const [slot2, r2] = logic.new_inbound_slot(
            local, beast::IP::Endpoint::from_string("55.104.0.2:1027"));
        BEAST_EXPECT(r2 == Result::ipLimitExceeded);

        if (!BEAST_EXPECT(slot2 == nullptr))
            logic.on_closed(slot2);
        logic.on_closed(slot1);
        logic.on_closed(slot);
    }

    void
    test_activate_duplicate_peer()
    {
        testcase("test activate duplicate peer");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const local = beast::IP::Endpoint::from_string("65.0.0.2:1024");

        PublicKey const pk1(randomKeyPair(KeyType::secp256k1).first);

        auto const [slot, rSlot] = logic.new_outbound_slot(
            beast::IP::Endpoint::from_string("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(rSlot == Result::success);

        auto const [slot2, r2Slot] = logic.new_outbound_slot(
            beast::IP::Endpoint::from_string("55.104.0.2:1026"));
        BEAST_EXPECT(slot2 != nullptr);
        BEAST_EXPECT(r2Slot == Result::success);

        BEAST_EXPECT(logic.onConnected(slot, local));
        BEAST_EXPECT(logic.onConnected(slot2, local));

        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::success);

        // activating a different slot with the same node ID (pk) must fail
        BEAST_EXPECT(
            logic.activate(slot2, pk1, false) == Result::duplicatePeer);

        logic.on_closed(slot);

        // accept the same key for a new slot after removing the old slot
        BEAST_EXPECT(logic.activate(slot2, pk1, false) == Result::success);
        logic.on_closed(slot2);
    }

    void
    test_activate_inbound_disabled()
    {
        testcase("test activate inbound disabled");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            logic.config(c);
        }

        PublicKey const pk1(randomKeyPair(KeyType::secp256k1).first);
        auto const local = beast::IP::Endpoint::from_string("65.0.0.2:1024");

        auto const [slot, rSlot] = logic.new_inbound_slot(
            local, beast::IP::Endpoint::from_string("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(rSlot == Result::success);

        BEAST_EXPECT(
            logic.activate(slot, pk1, false) == Result::inboundDisabled);

        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            c.inPeers = 1;
            logic.config(c);
        }
        // new inbound slot must succeed when inbound connections are enabled
        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::success);

        // creating a new inbound slot must succeed as IP Limit is not exceeded
        auto const [slot2, r2Slot] = logic.new_inbound_slot(
            local, beast::IP::Endpoint::from_string("55.104.0.2:1026"));
        BEAST_EXPECT(slot2 != nullptr);
        BEAST_EXPECT(r2Slot == Result::success);

        PublicKey const pk2(randomKeyPair(KeyType::secp256k1).first);

        // an inbound slot exceeding inPeers limit must fail
        BEAST_EXPECT(logic.activate(slot2, pk2, false) == Result::full);

        logic.on_closed(slot2);
        logic.on_closed(slot);
    }

    void
    test_addFixedPeer_no_port()
    {
        testcase("test addFixedPeer no port");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        try
        {
            logic.addFixedPeer(
                "test", beast::IP::Endpoint::from_string("65.0.0.2"));
            fail("invalid endpoint successfully added");
        }
        catch (std::runtime_error const& e)
        {
            pass();
        }
    }

    void
    test_onConnected_self_connection()
    {
        testcase("test onConnected self connection");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);

        auto const local = beast::IP::Endpoint::from_string("65.0.0.2:1234");
        auto const [slot, r] = logic.new_outbound_slot(local);
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(r == Result::success);

        // Must fail when a slot is to our own IP address
        BEAST_EXPECT(!logic.onConnected(slot, local));
        logic.on_closed(slot);
    }

    void
    test_config()
    {
        // if peers_max is configured then peers_in_max and peers_out_max
        // are ignored
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
                    "[peers_in_max]\n" + std::to_string(maxIn.value_or(0)) +
                    "\n" + "[peers_out_max]\n" +
                    std::to_string(maxOut.value_or(0)) + "\n";
            }
            else if (maxIn && maxOut)
            {
                toLoad += "[peers_in_max]\n" + std::to_string(*maxIn) + "\n" +
                    "[peers_out_max]\n" + std::to_string(*maxOut) + "\n";
            }

            c.loadFromString(toLoad);
            BEAST_EXPECT(
                (c.PEERS_MAX == max && c.PEERS_IN_MAX == 0 &&
                 c.PEERS_OUT_MAX == 0) ||
                (c.PEERS_IN_MAX == *maxIn && c.PEERS_OUT_MAX == *maxOut));

            Config config = Config::makeConfig(c, port, false, 0);

            Counts counts;
            counts.onConfig(config);
            BEAST_EXPECT(
                counts.out_max() == expectOut && counts.in_max() == expectIn &&
                config.ipLimit == expectIpLimit);

            TestStore store;
            TestChecker checker;
            TestStopwatch clock;
            Logic<TestChecker> logic(clock, store, checker, journal_);
            logic.config(config);

            BEAST_EXPECT(logic.config() == config);
        };

        // if max_peers == 0 => maxPeers = 21,
        //   else if max_peers < 10 => maxPeers = 10 else maxPeers =
        //   max_peers
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
    run() override
    {
        test_backoff1();
        test_backoff2();
        test_duplicateOutIn();
        test_duplicateInOut();
        test_config();
        test_invalid_config();
        test_peerLimitExceeded();
        test_activate_duplicate_peer();
        test_activate_inbound_disabled();
        test_addFixedPeer_no_port();
        test_onConnected_self_connection();
    }
};

BEAST_DEFINE_TESTSUITE(PeerFinder, peerfinder, ripple);

}  // namespace PeerFinder
}  // namespace ripple
