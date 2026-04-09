#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Logic.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

namespace xrpl {
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
            handler(ec);
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
        logic.addFixedPeer("test", beast::IP::Endpoint::from_string("65.0.0.1:5"));
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
                BEAST_EXPECT(
                    logic.onConnected(slot, beast::IP::Endpoint::from_string("65.0.0.2:5")));
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
        logic.addFixedPeer("test", beast::IP::Endpoint::from_string("65.0.0.1:5"));
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
                if (!BEAST_EXPECT(
                        logic.onConnected(slot, beast::IP::Endpoint::from_string("65.0.0.2:5"))))
                    return;
                if (!BEAST_EXPECT(logic.activate(slot, pk, false) == PeerFinder::Result::success))
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
        auto const [slot, r] =
            logic.new_inbound_slot(local, beast::IP::Endpoint::from_string("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(r == Result::success);

        auto const [slot1, r1] =
            logic.new_inbound_slot(local, beast::IP::Endpoint::from_string("55.104.0.2:1026"));
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r1 == Result::success);

        auto const [slot2, r2] =
            logic.new_inbound_slot(local, beast::IP::Endpoint::from_string("55.104.0.2:1027"));
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

        auto const [slot, rSlot] =
            logic.new_outbound_slot(beast::IP::Endpoint::from_string("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(rSlot == Result::success);

        auto const [slot2, r2Slot] =
            logic.new_outbound_slot(beast::IP::Endpoint::from_string("55.104.0.2:1026"));
        BEAST_EXPECT(slot2 != nullptr);
        BEAST_EXPECT(r2Slot == Result::success);

        BEAST_EXPECT(logic.onConnected(slot, local));
        BEAST_EXPECT(logic.onConnected(slot2, local));

        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::success);

        // activating a different slot with the same node ID (pk) must fail
        BEAST_EXPECT(logic.activate(slot2, pk1, false) == Result::duplicatePeer);

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

        auto const [slot, rSlot] =
            logic.new_inbound_slot(local, beast::IP::Endpoint::from_string("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(rSlot == Result::success);

        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::inboundDisabled);

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
        auto const [slot2, r2Slot] =
            logic.new_inbound_slot(local, beast::IP::Endpoint::from_string("55.104.0.2:1026"));
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
            logic.addFixedPeer("test", beast::IP::Endpoint::from_string("65.0.0.2"));
            fail("invalid endpoint successfully added");
        }
        catch (std::runtime_error const& e)
        {
            pass();
        }
    }

    void
    test_is_valid_address()
    {
        testcase("is_valid_address");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);

        auto const pass = [&](std::string const& s) {
            BEAST_EXPECT(
                logic.is_valid_address(beast::IP::Endpoint::from_string(s)));
        };
        auto const fail = [&](std::string const& s) {
            BEAST_EXPECT(
                !logic.is_valid_address(beast::IP::Endpoint::from_string(s)));
        };

        // Invalid: port 0
        fail("65.0.0.1:0");

        // --- IPv4 ranges ---
        // For each range: 1 before (pass), first (fail), last (fail),
        // 1 after (pass)

        // 0.0.0.0/8 - "This network"
        // No "before" - nothing before 0.0.0.0
        fail("0.0.0.0:8080");
        fail("0.255.255.255:8080");
        pass("1.0.0.0:8080");

        // 10.0.0.0/8 - Private (RFC 1918)
        pass("9.255.255.255:8080");
        fail("10.0.0.0:8080");
        fail("10.255.255.255:8080");
        pass("11.0.0.0:8080");

        // 100.64.0.0/10 - Shared Address Space / CGNAT (RFC 6598)
        pass("100.63.255.255:8080");
        fail("100.64.0.0:8080");
        fail("100.127.255.255:8080");
        pass("100.128.0.0:8080");

        // 127.0.0.0/8 - Loopback
        pass("126.255.255.255:8080");
        fail("127.0.0.0:8080");
        fail("127.255.255.255:8080");
        pass("128.0.0.0:8080");

        // 169.254.0.0/16 - Link-local
        pass("169.253.255.255:8080");
        fail("169.254.0.0:8080");
        fail("169.254.255.255:8080");
        pass("169.255.0.0:8080");

        // 172.16.0.0/12 - Private (RFC 1918)
        pass("172.15.255.255:8080");
        fail("172.16.0.0:8080");
        fail("172.31.255.255:8080");
        pass("172.32.0.0:8080");

        // 192.0.0.0/24 - IETF Protocol Assignments (RFC 6890)
        pass("191.255.255.255:8080");
        fail("192.0.0.0:8080");
        fail("192.0.0.255:8080");
        pass("192.0.1.0:8080");

        // 192.0.2.0/24 - TEST-NET-1 (RFC 5737)
        pass("192.0.1.255:8080");
        fail("192.0.2.0:8080");
        fail("192.0.2.255:8080");
        pass("192.0.3.0:8080");

        // 192.88.99.0/24 - 6to4 Relay Anycast (RFC 7526)
        pass("192.88.98.255:8080");
        fail("192.88.99.0:8080");
        fail("192.88.99.255:8080");
        pass("192.88.100.0:8080");

        // 192.168.0.0/16 - Private (RFC 1918)
        pass("192.167.255.255:8080");
        fail("192.168.0.0:8080");
        fail("192.168.255.255:8080");
        pass("192.169.0.0:8080");

        // 198.18.0.0/15 - Benchmarking (RFC 2544)
        pass("198.17.255.255:8080");
        fail("198.18.0.0:8080");
        fail("198.19.255.255:8080");
        pass("198.20.0.0:8080");

        // 198.51.100.0/24 - TEST-NET-2 (RFC 5737)
        pass("198.51.99.255:8080");
        fail("198.51.100.0:8080");
        fail("198.51.100.255:8080");
        pass("198.51.101.0:8080");

        // 203.0.113.0/24 - TEST-NET-3 (RFC 5737)
        pass("203.0.112.255:8080");
        fail("203.0.113.0:8080");
        fail("203.0.113.255:8080");
        pass("203.0.114.0:8080");

        // 224.0.0.0/4 - Multicast
        pass("223.255.255.255:8080");
        fail("224.0.0.0:8080");
        fail("239.255.255.255:8080");
        // 240.0.0.0 (after multicast) is also blocked (reserved)

        // 240.0.0.0/4 - Reserved (RFC 1112)
        // 239.255.255.255 (before reserved) is also blocked (multicast)
        fail("240.0.0.0:8080");
        fail("255.255.255.255:8080");

        // --- IPv6 ranges ---

        // ::1 - Loopback (single address)
        fail("[::1]:8080");

        // :: - Unspecified (single address)
        fail("[::]:8080");

        // fc00::/7 - Unique Local Address (ULA)
        pass("[fb00::1]:8080");
        fail("[fc00::1]:8080");
        fail("[fdff::1]:8080");
        pass("[fe00::1]:8080");

        // fe80::/10 - Link-local
        pass("[fe7f::1]:8080");
        fail("[fe80::1]:8080");
        fail("[febf::1]:8080");
        pass("[fec0::1]:8080");

        // ff00::/8 - Multicast
        pass("[feff::1]:8080");
        fail("[ff00::1]:8080");
        fail("[ffff::1]:8080");
        // No "after" - ffff:... is the highest IPv6 range

        // 100::/64 - Discard prefix (RFC 6666)
        pass("[ff::1]:8080");
        fail("[100::]:8080");
        fail("[100::ffff:ffff:ffff:ffff]:8080");
        pass("[100:0:0:1::1]:8080");

        // 2001::/32 - IETF Protocol Assignments / Teredo (RFC 4380)
        pass("[2000:ffff::1]:8080");
        fail("[2001::]:8080");
        fail("[2001:0:ffff::1]:8080");
        pass("[2001:1::1]:8080");

        // 2001:20::/28 - ORCHIDv2 (RFC 7343)
        pass("[2001:1f::1]:8080");
        fail("[2001:20::1]:8080");
        fail("[2001:2f::1]:8080");
        pass("[2001:30::1]:8080");

        // 2001:db8::/32 - Documentation (RFC 3849)
        pass("[2001:db7::1]:8080");
        fail("[2001:db8::1]:8080");
        fail("[2001:db8:ffff::1]:8080");
        pass("[2001:db9::1]:8080");

        // 2002::/16 - 6to4 (RFC 3056, deprecated)
        pass("[2001:ffff::1]:8080");
        fail("[2002::1]:8080");
        fail("[2002:ffff::1]:8080");
        pass("[2003::1]:8080");

        // --- IPv6 v4-mapped (delegates to IPv4 checks) ---
        fail("[::ffff:10.0.0.1]:8080");
        fail("[::ffff:100.64.0.1]:8080");
        fail("[::ffff:169.254.1.1]:8080");
        fail("[::ffff:192.0.2.1]:8080");
        fail("[::ffff:198.18.0.1]:8080");
        fail("[::ffff:224.0.0.1]:8080");
        fail("[::ffff:240.0.0.1]:8080");

        // --- Valid public addresses ---
        pass("8.8.8.8:443");
        pass("65.0.0.1:8080");
        pass("[2001:4860:4860::8888]:8080");
        pass("[2606:4700:4700::1111]:8080");
    }

    void
    test_verify_endpoints()
    {
        // Helper that sets up a Logic instance, creates and activates a slot,
        // then calls on_endpoints with the given list and returns the
        // livecache size afterwards.
        auto run = [&](bool verifyEndpoints, Endpoints eps) -> std::size_t {
            TestStore store;
            TestChecker checker;
            TestStopwatch clock;
            Logic<TestChecker> logic(clock, store, checker, journal_);
            {
                Config c;
                c.autoConnect = false;
                c.listeningPort = 1024;
                c.ipLimit = 2;
                c.verifyEndpoints = verifyEndpoints;
                logic.config(c);
            }

            auto const remote = beast::IP::Endpoint::from_string("65.0.0.1:5");
            auto const local =
                beast::IP::Endpoint::from_string("65.0.0.2:1024");

            auto const [slot, r] = logic.new_outbound_slot(remote);
            BEAST_EXPECT(slot != nullptr);
            BEAST_EXPECT(r == Result::success);
            BEAST_EXPECT(logic.onConnected(slot, local));

            PublicKey const pk(randomKeyPair(KeyType::secp256k1).first);
            BEAST_EXPECT(logic.activate(slot, pk, false) == Result::success);

            logic.on_endpoints(slot, std::move(eps));

            auto const size = logic.livecache_.size();
            logic.on_closed(slot);
            return size;
        };

        {
            testcase("verify_endpoints enabled");

            // Valid public addresses
            Endpoints eps;
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("44.0.0.1:5"), 1});
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("44.0.0.2:6"), 1});
            // Invalid: private address
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("10.0.0.1:5"), 1});
            // Invalid: port 0
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("44.0.0.3:0"), 1});

            // With verification enabled, only the 2 valid endpoints survive
            BEAST_EXPECT(run(true, eps) == 2);
        }
        {
            testcase("verify_endpoints disabled");

            Endpoints eps;
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("44.0.0.1:5"), 1});
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("44.0.0.2:6"), 1});
            // Private address — kept when verification is off
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("10.0.0.1:5"), 1});
            // Port 0 — kept when verification is off
            eps.push_back(
                Endpoint{beast::IP::Endpoint::from_string("44.0.0.3:0"), 1});

            // Without verification, all 4 endpoints survive
            BEAST_EXPECT(run(false, eps) == 4);
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
            xrpl::Config c;

            testcase(test);

            std::string toLoad;
            int max = 0;
            if (maxPeers)
            {
                max = maxPeers.value();
                toLoad += "[peers_max]\n" + std::to_string(max) + "\n" + "[peers_in_max]\n" +
                    std::to_string(maxIn.value_or(0)) + "\n" + "[peers_out_max]\n" +
                    std::to_string(maxOut.value_or(0)) + "\n";
            }
            else if (maxIn && maxOut)
            {
                toLoad += "[peers_in_max]\n" + std::to_string(*maxIn) + "\n" + "[peers_out_max]\n" +
                    std::to_string(*maxOut) + "\n";
            }

            c.loadFromString(toLoad);
            BEAST_EXPECT(
                (c.PEERS_MAX == max && c.PEERS_IN_MAX == 0 && c.PEERS_OUT_MAX == 0) ||
                (c.PEERS_IN_MAX == *maxIn && c.PEERS_OUT_MAX == *maxOut));

            Config config = Config::makeConfig(c, port, false, 0, true);

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
            xrpl::Config c;
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
        run(R"xrpldConfig(
[peers_in_max]
100
)xrpldConfig");
        run(R"xrpldConfig(
[peers_out_max]
100
)xrpldConfig");
        run(R"xrpldConfig(
[peers_in_max]
100
[peers_out_max]
5
)xrpldConfig");
        run(R"xrpldConfig(
[peers_in_max]
1001
[peers_out_max]
10
)xrpldConfig");
        run(R"xrpldConfig(
[peers_in_max]
10
[peers_out_max]
1001
)xrpldConfig");
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
        test_is_valid_address();
        test_verify_endpoints();
    }
};

BEAST_DEFINE_TESTSUITE(PeerFinder, peerfinder, xrpl);

}  // namespace PeerFinder
}  // namespace xrpl
