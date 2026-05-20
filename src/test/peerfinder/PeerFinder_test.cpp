#include <test/unit_test/SuiteJournal.h>

#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Counts.h>
#include <xrpld/peerfinder/detail/Logic.h>
#include <xrpld/peerfinder/detail/Source.h>
#include <xrpld/peerfinder/detail/Store.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::PeerFinder {

class PeerFinder_test : public beast::unit_test::Suite
{
    test::SuiteJournal journal_;

public:
    PeerFinder_test() : journal_("PeerFinder_test", *this)
    {
    }

    struct TestStore : Store
    {
        std::vector<Entry> loadEntries;
        std::vector<Entry> savedEntries;
        std::size_t saveCount = 0;

        std::size_t
        load(load_callback const& cb) override
        {
            for (auto const& entry : loadEntries)
                cb(entry.endpoint, entry.valence);
            return loadEntries.size();
        }

        void
        save(std::vector<Entry> const& entries) override
        {
            savedEntries = entries;
            ++saveCount;
        }
    };

    struct TestChecker
    {
        std::size_t checks = 0;
        boost::system::error_code error;

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
        asyncConnect(beast::IP::Endpoint const& ep, Handler&& handler)
        {
            (void)ep;
            ++checks;
            handler(error);
        }
    };

    struct TestSource : Source
    {
        std::string sourceName = "test-source";
        IPAddresses addresses;
        bool cancelled = false;
        bool fetched = false;

        std::string const&
        name() override
        {
            return sourceName;
        }

        void
        cancel() override
        {
            cancelled = true;
        }

        void
        fetch(Results& results, beast::Journal) override
        {
            fetched = true;
            results.addresses = addresses;
        }
    };

    static Store::Entry
    makeEntry(std::string const& endpoint, int valence)
    {
        Store::Entry entry;
        entry.endpoint = beast::IP::Endpoint::fromString(endpoint);
        entry.valence = valence;
        return entry;
    }

    void
    testBackoff1()
    {
        auto const seconds = 10000;
        testcase("backoff 1");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        logic.addFixedPeer("test", beast::IP::Endpoint::fromString("65.0.0.1:5"));
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
                auto const [slot, _] = logic.newOutboundSlot(list.front());
                BEAST_EXPECT(
                    logic.onConnected(slot, beast::IP::Endpoint::fromString("65.0.0.2:5")));
                logic.onClosed(slot);
                ++n;
            }
            clock.advance(std::chrono::seconds(1));
            logic.oncePerSecond();
        }
        // Less than 20 attempts
        BEAST_EXPECT(n < 20);
    }

    // with activate
    void
    testBackoff2()
    {
        auto const seconds = 10000;
        testcase("backoff 2");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        logic.addFixedPeer("test", beast::IP::Endpoint::fromString("65.0.0.1:5"));
        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            logic.config(c);
        }

        PublicKey const pk(randomKeyPair(KeyType::Secp256k1).first);
        std::size_t n = 0;

        for (std::size_t i = 0; i < seconds; ++i)
        {
            auto const list = logic.autoconnect();
            if (!list.empty())
            {
                BEAST_EXPECT(list.size() == 1);
                auto const [slot, _] = logic.newOutboundSlot(list.front());
                if (!BEAST_EXPECT(
                        logic.onConnected(slot, beast::IP::Endpoint::fromString("65.0.0.2:5"))))
                    return;
                if (!BEAST_EXPECT(logic.activate(slot, pk, false) == PeerFinder::Result::Success))
                    return;
                logic.onClosed(slot);
                ++n;
            }
            clock.advance(std::chrono::seconds(1));
            logic.oncePerSecond();
        }
        // No more often than once per minute
        BEAST_EXPECT(n <= (seconds + 59) / 60);
    }

    // test accepting an incoming slot for an already existing outgoing slot
    void
    testDuplicateOutIn()
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

        auto const remote = beast::IP::Endpoint::fromString("65.0.0.1:5");
        auto const [slot1, r] = logic.newOutboundSlot(remote);
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r == Result::Success);
        BEAST_EXPECT(logic.connectedAddresses.count(remote.address()) == 1);

        auto const local = beast::IP::Endpoint::fromString("65.0.0.2:1024");
        auto const [slot2, r2] = logic.newInboundSlot(local, remote);
        BEAST_EXPECT(logic.connectedAddresses.count(remote.address()) == 1);
        BEAST_EXPECT(r2 == Result::DuplicatePeer);

        if (!BEAST_EXPECT(slot2 == nullptr))
            logic.onClosed(slot2);

        logic.onClosed(slot1);
    }

    // test establishing outgoing slot for an already existing incoming slot
    void
    testDuplicateInOut()
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

        auto const remote = beast::IP::Endpoint::fromString("65.0.0.1:5");
        auto const local = beast::IP::Endpoint::fromString("65.0.0.2:1024");

        auto const [slot1, r] = logic.newInboundSlot(local, remote);
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r == Result::Success);
        BEAST_EXPECT(logic.connectedAddresses.count(remote.address()) == 1);

        auto const [slot2, r2] = logic.newOutboundSlot(remote);
        BEAST_EXPECT(r2 == Result::DuplicatePeer);
        BEAST_EXPECT(logic.connectedAddresses.count(remote.address()) == 1);
        if (!BEAST_EXPECT(slot2 == nullptr))
            logic.onClosed(slot2);
        logic.onClosed(slot1);
    }

    void
    testPeerLimitExceeded()
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

        auto const local = beast::IP::Endpoint::fromString("65.0.0.2:1024");
        auto const [slot, r] =
            logic.newInboundSlot(local, beast::IP::Endpoint::fromString("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(r == Result::Success);

        auto const [slot1, r1] =
            logic.newInboundSlot(local, beast::IP::Endpoint::fromString("55.104.0.2:1026"));
        BEAST_EXPECT(slot1 != nullptr);
        BEAST_EXPECT(r1 == Result::Success);

        auto const [slot2, r2] =
            logic.newInboundSlot(local, beast::IP::Endpoint::fromString("55.104.0.2:1027"));
        BEAST_EXPECT(r2 == Result::IpLimitExceeded);

        if (!BEAST_EXPECT(slot2 == nullptr))
            logic.onClosed(slot2);
        logic.onClosed(slot1);
        logic.onClosed(slot);
    }

    void
    testActivateDuplicatePeer()
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

        auto const local = beast::IP::Endpoint::fromString("65.0.0.2:1024");

        PublicKey const pk1(randomKeyPair(KeyType::Secp256k1).first);

        auto const [slot, rSlot] =
            logic.newOutboundSlot(beast::IP::Endpoint::fromString("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(rSlot == Result::Success);

        auto const [slot2, r2Slot] =
            logic.newOutboundSlot(beast::IP::Endpoint::fromString("55.104.0.2:1026"));
        BEAST_EXPECT(slot2 != nullptr);
        BEAST_EXPECT(r2Slot == Result::Success);

        BEAST_EXPECT(logic.onConnected(slot, local));
        BEAST_EXPECT(logic.onConnected(slot2, local));

        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::Success);

        // activating a different slot with the same node ID (pk) must fail
        BEAST_EXPECT(logic.activate(slot2, pk1, false) == Result::DuplicatePeer);

        logic.onClosed(slot);

        // accept the same key for a new slot after removing the old slot
        BEAST_EXPECT(logic.activate(slot2, pk1, false) == Result::Success);
        logic.onClosed(slot2);
    }

    void
    testActivateInboundDisabled()
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

        PublicKey const pk1(randomKeyPair(KeyType::Secp256k1).first);
        auto const local = beast::IP::Endpoint::fromString("65.0.0.2:1024");

        auto const [slot, rSlot] =
            logic.newInboundSlot(local, beast::IP::Endpoint::fromString("55.104.0.2:1025"));
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(rSlot == Result::Success);

        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::InboundDisabled);

        {
            Config c;
            c.autoConnect = false;
            c.listeningPort = 1024;
            c.ipLimit = 2;
            c.inPeers = 1;
            logic.config(c);
        }
        // new inbound slot must succeed when inbound connections are enabled
        BEAST_EXPECT(logic.activate(slot, pk1, false) == Result::Success);

        // creating a new inbound slot must succeed as IP Limit is not exceeded
        auto const [slot2, r2Slot] =
            logic.newInboundSlot(local, beast::IP::Endpoint::fromString("55.104.0.2:1026"));
        BEAST_EXPECT(slot2 != nullptr);
        BEAST_EXPECT(r2Slot == Result::Success);

        PublicKey const pk2(randomKeyPair(KeyType::Secp256k1).first);

        // an inbound slot exceeding inPeers limit must fail
        BEAST_EXPECT(logic.activate(slot2, pk2, false) == Result::Full);

        logic.onClosed(slot2);
        logic.onClosed(slot);
    }

    void
    testAddFixedPeerNoPort()
    {
        testcase("test addFixedPeer no port");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        try
        {
            logic.addFixedPeer("test", beast::IP::Endpoint::fromString("65.0.0.2"));
            fail("invalid endpoint successfully added");
        }
        catch (std::runtime_error const& e)
        {
            pass();
        }
    }

    void
    testOnConnectedSelfConnection()
    {
        testcase("test onConnected self connection");
        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);

        auto const local = beast::IP::Endpoint::fromString("65.0.0.2:1234");
        auto const [slot, r] = logic.newOutboundSlot(local);
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(r == Result::Success);

        // Must fail when a slot is to our own IP address
        BEAST_EXPECT(!logic.onConnected(slot, local));
        logic.onClosed(slot);
    }

    void
    testPreprocessAndEndpointHandling()
    {
        testcase("preprocess and endpoint handling");

        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = true;
            c.wantIncoming = true;
            c.listeningPort = 51235;
            c.outPeers = 2;
            c.inPeers = 2;
            c.ipLimit = 2;
            logic.config(c);
        }

        auto const local = beast::IP::Endpoint::fromString("198.51.100.2:51235");
        auto const remote = beast::IP::Endpoint::fromString("198.51.100.1:1234");
        auto const [slot, result] = logic.newInboundSlot(local, remote);
        BEAST_EXPECT(slot != nullptr);
        BEAST_EXPECT(result == Result::Success);
        BEAST_EXPECT(
            logic.activate(slot, randomKeyPair(KeyType::Secp256k1).first, false) ==
            Result::Success);

        Endpoints endpoints{
            {beast::IP::Endpoint::fromString("0.0.0.0:51235"), 0},
            {beast::IP::Endpoint::fromString("198.51.100.3:51235"), 2},
            {beast::IP::Endpoint::fromString("198.51.100.3:51235"), 2},
            {beast::IP::Endpoint::fromString("10.0.0.1:51235"), 2},
            {beast::IP::Endpoint::fromString("198.51.100.4:51235"), Tuning::kMAX_HOPS + 1},
        };

        logic.preprocess(slot, endpoints);

        BEAST_EXPECT(endpoints.size() == 2);
        BEAST_EXPECT(endpoints[0].address == beast::IP::Endpoint::fromString("198.51.100.1:51235"));
        BEAST_EXPECT(endpoints[0].hops == 1);
        BEAST_EXPECT(endpoints[1].address == beast::IP::Endpoint::fromString("198.51.100.3:51235"));
        BEAST_EXPECT(endpoints[1].hops == 3);

        logic.onEndpoints(
            slot,
            {{beast::IP::Endpoint::fromString("198.51.100.5:51235"), 2},
             {beast::IP::Endpoint::fromString("198.51.100.6:51235"), 2}});

        auto const broadcast = logic.buildEndpointsForPeers();
        BEAST_EXPECT(broadcast.size() == 1);
        BEAST_EXPECT(broadcast.front().first == slot);
        BEAST_EXPECT(!broadcast.front().second.empty());
        BEAST_EXPECT(logic.buildEndpointsForPeers().empty());

        auto const redirects = logic.redirect(slot);
        BEAST_EXPECT(!redirects.empty());

        logic.onClosed(slot);
    }

    void
    testRedirectsSourcesStopAndState()
    {
        testcase("redirects sources stop and state");

        TestStore store;
        TestChecker checker;
        TestStopwatch clock;
        Logic<TestChecker> logic(clock, store, checker, journal_);
        {
            Config c;
            c.autoConnect = true;
            c.wantIncoming = true;
            c.listeningPort = 51235;
            c.outPeers = 2;
            c.inPeers = 2;
            c.ipLimit = 2;
            logic.config(c);
        }

        std::vector<boost::asio::ip::tcp::endpoint> redirects{
            {boost::asio::ip::make_address("198.51.100.41"), 51235},
            {boost::asio::ip::make_address("198.51.100.42"), 51235},
        };
        logic.onRedirects(
            redirects.begin(),
            redirects.end(),
            {boost::asio::ip::make_address("198.51.100.139"), 51235});

        auto const staticCount = logic.addBootcacheAddresses(
            {beast::IP::Endpoint::fromString("198.51.100.43:51235"),
             beast::IP::Endpoint::fromString("198.51.100.43:51235"),
             beast::IP::Endpoint::fromString("198.51.100.44:51235")});
        BEAST_EXPECT(staticCount == 2);
        BEAST_EXPECT(!logic.autoconnect().empty());

        auto source = std::make_shared<TestSource>();
        source->addresses = {beast::IP::Endpoint::fromString("198.51.100.45:51235")};
        logic.fetchSource = source;
        logic.stop();
        BEAST_EXPECT(logic.stopping);
        BEAST_EXPECT(source->cancelled);

        auto blockedSource = std::make_shared<TestSource>();
        logic.addStaticSource(blockedSource);
        BEAST_EXPECT(!blockedSource->fetched);

        BEAST_EXPECT(Logic<TestChecker>::stateString(Slot::State::Accept) == "accept");
        BEAST_EXPECT(Logic<TestChecker>::stateString(Slot::State::Connect) == "connect");
        BEAST_EXPECT(Logic<TestChecker>::stateString(Slot::State::Connected) == "connected");
        BEAST_EXPECT(Logic<TestChecker>::stateString(Slot::State::Active) == "active");
        BEAST_EXPECT(Logic<TestChecker>::stateString(Slot::State::Closing) == "closing");
        BEAST_EXPECT(Logic<TestChecker>::stateString(static_cast<Slot::State>(99)) == "?");
    }

    void
    testHandouts()
    {
        testcase("handouts");

        TestStopwatch clock;
        auto const slot = std::make_shared<SlotImp>(
            beast::IP::Endpoint::fromString("198.51.100.81:51235"),
            beast::IP::Endpoint::fromString("198.51.100.82:51235"),
            false,
            clock);

        SlotHandouts slotHandouts(slot);
        BEAST_EXPECT(
            !slotHandouts.tryInsert({beast::IP::Endpoint::fromString("198.51.100.82:60000"), 1}));
        BEAST_EXPECT(
            slotHandouts.tryInsert({beast::IP::Endpoint::fromString("198.51.100.83:51235"), 1}));
        BEAST_EXPECT(
            !slotHandouts.tryInsert({beast::IP::Endpoint::fromString("198.51.100.83:60000"), 2}));
        BEAST_EXPECT(!slotHandouts.tryInsert(
            {beast::IP::Endpoint::fromString("198.51.100.84:51235"), Tuning::kMAX_HOPS + 1}));

        slot->recent.insert(beast::IP::Endpoint::fromString("198.51.100.85:51235"), 1);
        BEAST_EXPECT(
            !slotHandouts.tryInsert({beast::IP::Endpoint::fromString("198.51.100.85:51235"), 1}));

        RedirectHandouts redirectHandouts(slot);
        BEAST_EXPECT(!redirectHandouts.tryInsert(
            {beast::IP::Endpoint::fromString("198.51.100.86:51235"), 0}));
        BEAST_EXPECT(!redirectHandouts.tryInsert(
            {beast::IP::Endpoint::fromString("198.51.100.82:51235"), 1}));
        BEAST_EXPECT(redirectHandouts.tryInsert(
            {beast::IP::Endpoint::fromString("198.51.100.86:51235"), 1}));
        BEAST_EXPECT(!redirectHandouts.tryInsert(
            {beast::IP::Endpoint::fromString("198.51.100.86:60000"), 2}));
        BEAST_EXPECT(!redirectHandouts.tryInsert(
            {beast::IP::Endpoint::fromString("198.51.100.87:51235"), Tuning::kMAX_HOPS + 1}));

        ConnectHandouts::Squelches squelches(clock);
        ConnectHandouts connectHandouts(2, squelches);
        BEAST_EXPECT(
            connectHandouts.tryInsert(beast::IP::Endpoint::fromString("198.51.100.88:51235")));
        BEAST_EXPECT(
            !connectHandouts.tryInsert(beast::IP::Endpoint::fromString("198.51.100.88:60000")));
        BEAST_EXPECT(
            connectHandouts.tryInsert(beast::IP::Endpoint::fromString("198.51.100.89:51235")));
        BEAST_EXPECT(
            !connectHandouts.tryInsert(beast::IP::Endpoint::fromString("198.51.100.90:51235")));

        ConnectHandouts squelchedHandouts(1, squelches);
        BEAST_EXPECT(
            !squelchedHandouts.tryInsert(beast::IP::Endpoint::fromString("198.51.100.88:51235")));
    }

    void
    testBootcache()
    {
        testcase("bootcache");

        TestStore store;
        TestStopwatch clock;
        store.loadEntries = {
            makeEntry("198.51.100.121:51235", -1),
            makeEntry("198.51.100.122:51235", 2),
        };

        {
            Bootcache cache(store, clock, journal_);
            cache.load();
            BEAST_EXPECT(cache.size() == 2);
            BEAST_EXPECT(*cache.begin() == beast::IP::Endpoint::fromString("198.51.100.122:51235"));

            BEAST_EXPECT(
                cache.insertStatic(beast::IP::Endpoint::fromString("198.51.100.121:51235")));
            BEAST_EXPECT(*cache.begin() == beast::IP::Endpoint::fromString("198.51.100.121:51235"));

            cache.onSuccess(beast::IP::Endpoint::fromString("198.51.100.123:51235"));
            cache.onSuccess(beast::IP::Endpoint::fromString("198.51.100.123:51235"));
            cache.onFailure(beast::IP::Endpoint::fromString("198.51.100.124:51235"));

            for (std::size_t i = 0; i <= Tuning::kBOOTCACHE_SIZE + 5; ++i)
            {
                auto const address = "198.51.100." + std::to_string((i % 250) + 1) + ":" +
                    std::to_string(51235 + (i / 250));
                cache.insert(beast::IP::Endpoint::fromString(address));
            }
            BEAST_EXPECT(cache.size() <= Tuning::kBOOTCACHE_SIZE);

            clock.advance(Tuning::kBOOTCACHE_COOLDOWN_TIME + std::chrono::seconds(1));
            cache.periodicActivity();
            BEAST_EXPECT(store.saveCount == 1);
            BEAST_EXPECT(!store.savedEntries.empty());
        }

        BEAST_EXPECT(store.saveCount == 1);
    }

    void
    testConfig()
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

            Config const config = Config::makeConfig(c, port, false, 0, true);

            Counts counts;
            counts.onConfig(config);
            BEAST_EXPECT(
                counts.outMax() == expectOut && counts.inMax() == expectIn &&
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
    testInvalidConfig()
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
        testBackoff1();
        testBackoff2();
        testDuplicateOutIn();
        testDuplicateInOut();
        testConfig();
        testInvalidConfig();
        testPeerLimitExceeded();
        testActivateDuplicatePeer();
        testActivateInboundDisabled();
        testAddFixedPeerNoPort();
        testOnConnectedSelfConnection();
        testPreprocessAndEndpointHandling();
        testRedirectsSourcesStopAndState();
        testHandouts();
        testBootcache();
    }
};

BEAST_DEFINE_TESTSUITE(PeerFinder, peerfinder, xrpl);

}  // namespace xrpl::PeerFinder
