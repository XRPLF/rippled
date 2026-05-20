#include <xrpl/basics/chrono.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/peerfinder/Config.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Bootcache.h>
#include <xrpl/peerfinder/detail/Counts.h>
#include <xrpl/peerfinder/detail/Logic.h>
#include <xrpl/peerfinder/detail/SlotImp.h>
#include <xrpl/peerfinder/detail/Store.h>
#include <xrpl/peerfinder/detail/Tuning.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::PeerFinder {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

beast::Journal
journal()
{
    return beast::Journal{TestSink::instance()};
}

beast::IP::Endpoint
endpoint(std::string const& value)
{
    return beast::IP::Endpoint::fromString(value);
}

class MockStore : public Store
{
public:
    MOCK_METHOD(std::size_t, load, (Store::load_callback const& cb), (override));
    MOCK_METHOD(void, save, (std::vector<Store::Entry> const& entries), (override));
};

class CapturingStore : public Store
{
public:
    std::vector<Store::Entry> entriesToLoad;
    std::vector<std::vector<Store::Entry>> saves;

    std::size_t
    load(Store::load_callback const& cb) override
    {
        for (auto const& entry : entriesToLoad)
            cb(entry.endpoint, entry.valence);
        return entriesToLoad.size();
    }

    void
    save(std::vector<Store::Entry> const& entries) override
    {
        saves.push_back(entries);
    }
};

Store::Entry
storeEntry(beast::IP::Endpoint const& endpoint, int valence)
{
    Store::Entry entry;
    entry.endpoint = endpoint;
    entry.valence = valence;
    return entry;
}

void
allowEmptyStore(MockStore& store)
{
    ON_CALL(store, load(_)).WillByDefault(Return(0));
    ON_CALL(store, save(_)).WillByDefault([](std::vector<Store::Entry> const&) {});
}

class MockChecker
{
public:
    MOCK_METHOD(void, stop, ());
    MOCK_METHOD(void, wait, ());
    MOCK_METHOD(void, recordAsyncConnect, (beast::IP::Endpoint const& ep));

    template <class Handler>
    void
    asyncConnect(beast::IP::Endpoint const& ep, Handler&& handler)
    {
        recordAsyncConnect(ep);
        std::forward<Handler>(handler)(boost::system::error_code{});
    }
};

class PeerFinderTest : public ::testing::Test
{
public:
    PeerFinderTest()
    {
        allowEmptyStore(store_);
    }

protected:
    void
    configure(std::size_t ipLimit = 2)
    {
        Config config;
        config.autoConnect = false;
        config.listeningPort = 1024;
        config.ipLimit = static_cast<int>(ipLimit);
        logic_.config(config);
    }

    NiceMock<MockStore> store_;
    NiceMock<MockChecker> checker_;
    TestStopwatch clock_;
    Logic<NiceMock<MockChecker>> logic_{clock_, store_, checker_, journal()};
};

int
savedValence(std::vector<Store::Entry> const& entries, beast::IP::Endpoint const& endpoint)
{
    for (auto const& entry : entries)
    {
        if (entry.endpoint == endpoint)
            return entry.valence;
    }

    ADD_FAILURE() << "missing saved endpoint " << endpoint.toString();
    return 0;
}

TEST_F(PeerFinderTest, backoff_limits_repeated_connection_attempts)
{
    auto constexpr kSECONDS = 10000;

    logic_.addFixedPeer("test", endpoint("65.0.0.1:5"));
    configure();

    std::size_t attempts = 0;
    for (std::size_t i = 0; i < kSECONDS; ++i)
    {
        auto const list = logic_.autoconnect();
        if (!list.empty())
        {
            ASSERT_EQ(list.size(), 1u);
            auto const [slot, result] = logic_.newOutboundSlot(list.front());
            ASSERT_NE(slot, nullptr);
            ASSERT_EQ(result, Result::Success);
            EXPECT_TRUE(logic_.onConnected(slot, endpoint("65.0.0.2:5")));
            logic_.onClosed(slot);
            ++attempts;
        }
        clock_.advance(std::chrono::seconds(1));
        logic_.oncePerSecond();
    }

    EXPECT_LT(attempts, 20u);
}

TEST_F(PeerFinderTest, activated_peer_backoff_allows_at_most_one_attempt_per_minute)
{
    auto constexpr kSECONDS = 10000;

    logic_.addFixedPeer("test", endpoint("65.0.0.1:5"));
    configure();

    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);

    std::size_t attempts = 0;
    for (std::size_t i = 0; i < kSECONDS; ++i)
    {
        auto const list = logic_.autoconnect();
        if (!list.empty())
        {
            ASSERT_EQ(list.size(), 1u);
            auto const [slot, result] = logic_.newOutboundSlot(list.front());
            ASSERT_NE(slot, nullptr);
            ASSERT_EQ(result, Result::Success);
            ASSERT_TRUE(logic_.onConnected(slot, endpoint("65.0.0.2:5")));
            ASSERT_EQ(logic_.activate(slot, publicKey, false), Result::Success);
            logic_.onClosed(slot);
            ++attempts;
        }
        clock_.advance(std::chrono::seconds(1));
        logic_.oncePerSecond();
    }

    EXPECT_LE(attempts, (kSECONDS + 59u) / 60u);
}

TEST_F(PeerFinderTest, duplicate_inbound_slot_is_rejected_for_existing_outbound_slot)
{
    configure();

    auto const remote = endpoint("65.0.0.1:5");
    auto const [slot1, result1] = logic_.newOutboundSlot(remote);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(result1, Result::Success);
    EXPECT_EQ(logic_.connectedAddresses.count(remote.address()), 1u);

    auto const local = endpoint("65.0.0.2:1024");
    auto const [slot2, result2] = logic_.newInboundSlot(local, remote);
    EXPECT_EQ(logic_.connectedAddresses.count(remote.address()), 1u);
    EXPECT_EQ(result2, Result::DuplicatePeer);
    EXPECT_EQ(slot2, nullptr);

    if (slot2)
        logic_.onClosed(slot2);
    logic_.onClosed(slot1);
}

TEST_F(PeerFinderTest, duplicate_outbound_slot_is_rejected_for_existing_inbound_slot)
{
    configure();

    auto const remote = endpoint("65.0.0.1:5");
    auto const local = endpoint("65.0.0.2:1024");

    auto const [slot1, result1] = logic_.newInboundSlot(local, remote);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(result1, Result::Success);
    EXPECT_EQ(logic_.connectedAddresses.count(remote.address()), 1u);

    auto const [slot2, result2] = logic_.newOutboundSlot(remote);
    EXPECT_EQ(result2, Result::DuplicatePeer);
    EXPECT_EQ(logic_.connectedAddresses.count(remote.address()), 1u);
    EXPECT_EQ(slot2, nullptr);

    if (slot2)
        logic_.onClosed(slot2);
    logic_.onClosed(slot1);
}

TEST_F(PeerFinderTest, peer_limit_exceeded_rejects_additional_inbound_slot)
{
    configure();

    auto const local = endpoint("65.0.0.2:1024");
    auto const [slot, result] = logic_.newInboundSlot(local, endpoint("55.104.0.2:1025"));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);

    auto const [slot1, result1] = logic_.newInboundSlot(local, endpoint("55.104.0.2:1026"));
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(result1, Result::Success);

    auto const [slot2, result2] = logic_.newInboundSlot(local, endpoint("55.104.0.2:1027"));
    EXPECT_EQ(result2, Result::IpLimitExceeded);
    EXPECT_EQ(slot2, nullptr);

    if (slot2)
        logic_.onClosed(slot2);
    logic_.onClosed(slot1);
    logic_.onClosed(slot);
}

TEST_F(PeerFinderTest, activate_rejects_duplicate_public_key)
{
    configure();

    auto const local = endpoint("65.0.0.2:1024");
    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);

    auto const [slot, result] = logic_.newOutboundSlot(endpoint("55.104.0.2:1025"));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);

    auto const [slot2, result2] = logic_.newOutboundSlot(endpoint("55.104.0.2:1026"));
    ASSERT_NE(slot2, nullptr);
    EXPECT_EQ(result2, Result::Success);

    EXPECT_TRUE(logic_.onConnected(slot, local));
    EXPECT_TRUE(logic_.onConnected(slot2, local));

    EXPECT_EQ(logic_.activate(slot, publicKey, false), Result::Success);
    EXPECT_EQ(logic_.activate(slot2, publicKey, false), Result::DuplicatePeer);

    logic_.onClosed(slot);

    EXPECT_EQ(logic_.activate(slot2, publicKey, false), Result::Success);
    logic_.onClosed(slot2);
}

TEST_F(PeerFinderTest, activate_rejects_inbound_when_inbound_connections_are_disabled)
{
    configure();

    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);
    auto const local = endpoint("65.0.0.2:1024");

    auto const [slot, result] = logic_.newInboundSlot(local, endpoint("55.104.0.2:1025"));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);

    EXPECT_EQ(logic_.activate(slot, publicKey, false), Result::InboundDisabled);

    {
        Config config;
        config.autoConnect = false;
        config.listeningPort = 1024;
        config.ipLimit = 2;
        config.inPeers = 1;
        logic_.config(config);
    }

    EXPECT_EQ(logic_.activate(slot, publicKey, false), Result::Success);

    auto const [slot2, result2] = logic_.newInboundSlot(local, endpoint("55.104.0.2:1026"));
    ASSERT_NE(slot2, nullptr);
    EXPECT_EQ(result2, Result::Success);

    PublicKey const publicKey2(randomKeyPair(KeyType::Secp256k1).first);
    EXPECT_EQ(logic_.activate(slot2, publicKey2, false), Result::Full);

    logic_.onClosed(slot2);
    logic_.onClosed(slot);
}

TEST_F(PeerFinderTest, add_fixed_peer_rejects_endpoint_without_port)
{
    EXPECT_THROW(logic_.addFixedPeer("test", endpoint("65.0.0.2")), std::runtime_error);
}

TEST_F(PeerFinderTest, on_connected_rejects_self_connection)
{
    auto const local = endpoint("65.0.0.2:1234");
    auto const [slot, result] = logic_.newOutboundSlot(local);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);

    EXPECT_FALSE(logic_.onConnected(slot, local));
    logic_.onClosed(slot);
}

TEST(PeerFinderBootcache, loads_unique_entries_and_clears_cache)
{
    CapturingStore store;
    TestStopwatch clock;
    auto const ep1 = endpoint("65.0.0.1:10001");
    auto const ep2 = endpoint("65.0.0.2:10002");
    store.entriesToLoad = {storeEntry(ep1, 3), storeEntry(ep2, -2), storeEntry(ep1, 4)};

    Bootcache cache(store, clock, journal());
    cache.load();

    EXPECT_FALSE(cache.empty());
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(*cache.begin(), ep1);
    EXPECT_EQ(*cache.cbegin(), ep1);
    EXPECT_NE(cache.begin(), cache.end());
    EXPECT_NE(cache.cbegin(), cache.cend());

    cache.clear();
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.begin(), cache.end());
}

TEST(PeerFinderBootcache, records_connection_outcomes_and_persists_pending_updates)
{
    CapturingStore store;
    TestStopwatch clock;
    auto const ep1 = endpoint("65.0.0.1:10001");
    auto const ep2 = endpoint("65.0.0.2:10002");
    auto const ep3 = endpoint("65.0.0.3:10003");
    auto const ep4 = endpoint("65.0.0.4:10004");

    {
        Bootcache cache(store, clock, journal());

        EXPECT_TRUE(cache.insert(ep1));
        EXPECT_FALSE(cache.insert(ep1));

        cache.onSuccess(ep1);
        EXPECT_TRUE(cache.insertStatic(ep1));
        EXPECT_FALSE(cache.insertStatic(ep1));

        EXPECT_TRUE(cache.insertStatic(ep2));
        cache.onSuccess(ep3);
        cache.onFailure(ep3);
        cache.onFailure(ep4);

        EXPECT_EQ(cache.size(), 4u);

        JsonPropertyStream stream;
        {
            beast::PropertyStream::Map map(stream);
            cache.onWrite(map);
        }
        EXPECT_TRUE(stream.top().isMember("entries"));
        EXPECT_EQ(stream.top()["entries"].size(), 4u);
    }

    ASSERT_EQ(store.saves.size(), 1u);
    auto const& saved = store.saves.front();
    ASSERT_EQ(saved.size(), 4u);
    EXPECT_EQ(savedValence(saved, ep1), Bootcache::kStaticValence);
    EXPECT_EQ(savedValence(saved, ep2), Bootcache::kStaticValence);
    EXPECT_EQ(savedValence(saved, ep3), -1);
    EXPECT_EQ(savedValence(saved, ep4), -1);
}

TEST(PeerFinderBootcache, periodic_activity_saves_after_cooldown)
{
    using namespace std::chrono_literals;

    CapturingStore store;
    TestStopwatch clock;

    {
        Bootcache cache(store, clock, journal());
        EXPECT_TRUE(cache.insert(endpoint("65.0.0.1:10001")));

        cache.periodicActivity();
        EXPECT_TRUE(store.saves.empty());

        clock.advance(Tuning::kBootcacheCooldownTime + 1s);
        cache.periodicActivity();
        ASSERT_EQ(store.saves.size(), 1u);

        cache.periodicActivity();
        EXPECT_EQ(store.saves.size(), 1u);
    }

    EXPECT_EQ(store.saves.size(), 1u);
}

TEST(PeerFinderBootcache, prunes_when_cache_exceeds_limit)
{
    CapturingStore store;
    TestStopwatch clock;
    Bootcache cache(store, clock, journal());

    for (std::uint16_t i = 0; i <= Tuning::kBootcacheSize; ++i)
    {
        EXPECT_TRUE(cache.insert(endpoint(
            "65.0." + std::to_string((i / 256) % 256) + "." + std::to_string(i % 256) + ":" +
            std::to_string(10000 + i))));
    }

    EXPECT_LE(cache.size(), Tuning::kBootcacheSize);
}

TEST(PeerFinderEndpoint, clamps_hops_to_overflow_bucket)
{
    auto const address = endpoint("65.0.0.1:10001");
    Endpoint const ep(address, Tuning::kMaxHops + 10);

    EXPECT_EQ(ep.address, address);
    EXPECT_EQ(ep.hops, Tuning::kMaxHops + 1);
}

TEST(PeerFinderSlotImp, tracks_state_and_recent_endpoints)
{
    using State = Slot::State;
    using namespace std::chrono_literals;

    TestStopwatch clock;
    auto const local = endpoint("65.0.0.1:10000");
    auto const remote = endpoint("65.0.0.2:10001");
    SlotImp inbound(local, remote, true, clock);

    EXPECT_TRUE(inbound.inbound());
    EXPECT_TRUE(inbound.fixed());
    EXPECT_FALSE(inbound.reserved());
    EXPECT_EQ(inbound.state(), State::Accept);
    EXPECT_EQ(inbound.remoteEndpoint(), remote);
    EXPECT_EQ(inbound.localEndpoint(), std::optional<beast::IP::Endpoint>{local});
    EXPECT_FALSE(inbound.publicKey());
    EXPECT_FALSE(inbound.listeningPort());
    EXPECT_FALSE(inbound.checked);
    EXPECT_FALSE(inbound.canAccept);
    EXPECT_FALSE(inbound.connectivityCheckInProgress);

    auto const newLocal = endpoint("65.0.0.3:10002");
    auto const newRemote = endpoint("65.0.0.4:10003");
    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);

    inbound.localEndpoint(newLocal);
    inbound.remoteEndpoint(newRemote);
    inbound.publicKey(publicKey);
    inbound.reserved(true);
    inbound.setListeningPort(2459);

    EXPECT_EQ(inbound.localEndpoint(), std::optional<beast::IP::Endpoint>{newLocal});
    EXPECT_EQ(inbound.remoteEndpoint(), newRemote);
    EXPECT_EQ(inbound.publicKey(), std::optional<PublicKey>{publicKey});
    EXPECT_TRUE(inbound.reserved());
    EXPECT_EQ(inbound.listeningPort(), std::optional<std::uint16_t>{2459});
    EXPECT_FALSE(inbound.prefix().empty());

    inbound.state(State::Closing);
    EXPECT_EQ(inbound.state(), State::Closing);

    SlotImp outbound(remote, false, clock);
    EXPECT_FALSE(outbound.inbound());
    EXPECT_FALSE(outbound.fixed());
    EXPECT_EQ(outbound.state(), State::Connect);
    EXPECT_TRUE(outbound.checked);
    EXPECT_TRUE(outbound.canAccept);

    outbound.state(State::Connected);
    outbound.activate(clock.now());
    EXPECT_EQ(outbound.state(), State::Active);
    EXPECT_EQ(outbound.whenAcceptEndpoints, clock.now());

    auto const recent = endpoint("65.0.0.5:10004");
    EXPECT_FALSE(outbound.recent.filter(recent, 2));

    outbound.recent.insert(recent, 2);
    EXPECT_TRUE(outbound.recent.filter(recent, 2));
    EXPECT_TRUE(outbound.recent.filter(recent, 3));
    EXPECT_FALSE(outbound.recent.filter(recent, 1));

    outbound.recent.insert(recent, 4);
    EXPECT_FALSE(outbound.recent.filter(recent, 1));

    outbound.recent.insert(recent, 1);
    EXPECT_TRUE(outbound.recent.filter(recent, 1));
    EXPECT_FALSE(outbound.recent.filter(recent, 0));

    clock.advance(Tuning::kLiveCacheSecondsToLive + 1s);
    outbound.expire();
    EXPECT_FALSE(outbound.recent.filter(recent, 1));
}

TEST(PeerFinderConfig, writes_property_stream_and_compares_verify_endpoints)
{
    Config config;
    config.maxPeers = 42;
    config.outPeers = 12;
    config.inPeers = 30;
    config.peerPrivate = false;
    config.wantIncoming = true;
    config.autoConnect = false;
    config.listeningPort = 2459;
    config.features = "feature";
    config.ipLimit = 4;
    config.verifyEndpoints = false;

    JsonPropertyStream stream;
    {
        beast::PropertyStream::Map map(stream);
        config.onWrite(map);
    }

    auto const& json = stream.top();
    EXPECT_EQ(json["max_peers"].asUInt(), config.maxPeers);
    EXPECT_EQ(json["out_peers"].asUInt(), config.outPeers);
    EXPECT_TRUE(json.isMember("want_incoming"));
    EXPECT_TRUE(json.isMember("auto_connect"));
    EXPECT_EQ(json["port"].asUInt(), config.listeningPort);
    EXPECT_EQ(json["features"].asString(), config.features);
    EXPECT_EQ(json["ip_limit"].asInt(), config.ipLimit);
    EXPECT_TRUE(json.isMember("verify_endpoints"));

    Config same = config;
    EXPECT_EQ(config, same);
    same.verifyEndpoints = true;
    EXPECT_NE(config, same);
}

TEST(PeerFinderConfig, validator_and_standalone_settings_disable_auto_connect)
{
    PeerLimitConfig const limits{.maxPeers = 50, .inPeers = {}, .outPeers = {}};

    Config const config = Config::makeConfig(false, true, limits, 2459, true, 7, false);

    EXPECT_TRUE(config.peerPrivate);
    EXPECT_FALSE(config.autoConnect);
    EXPECT_FALSE(config.verifyEndpoints);
    EXPECT_EQ(config.ipLimit, 7);
}

TEST(PeerFinderConfig, applies_legacy_and_explicit_peer_limits)
{
    struct ConfigCase
    {
        std::string name;
        std::optional<std::uint16_t> maxPeers;
        std::optional<std::uint16_t> maxIn;
        std::optional<std::uint16_t> maxOut;
        std::uint16_t port;
        std::uint16_t expectedOut;
        std::uint16_t expectedIn;
        std::uint16_t expectedIpLimit;
    };

    std::vector<ConfigCase> const cases{
        {.name = "legacy no config",
         .maxPeers = {},
         .maxIn = {},
         .maxOut = {},
         .port = 4000,
         .expectedOut = 10,
         .expectedIn = 11,
         .expectedIpLimit = 2},
        {.name = "legacy max_peers 0",
         .maxPeers = 0,
         .maxIn = 100,
         .maxOut = 10,
         .port = 4000,
         .expectedOut = 10,
         .expectedIn = 11,
         .expectedIpLimit = 2},
        {.name = "legacy max_peers 5",
         .maxPeers = 5,
         .maxIn = 100,
         .maxOut = 10,
         .port = 4000,
         .expectedOut = 10,
         .expectedIn = 0,
         .expectedIpLimit = 1},
        {.name = "legacy max_peers 20",
         .maxPeers = 20,
         .maxIn = 100,
         .maxOut = 10,
         .port = 4000,
         .expectedOut = 10,
         .expectedIn = 10,
         .expectedIpLimit = 2},
        {.name = "legacy max_peers 100",
         .maxPeers = 100,
         .maxIn = 100,
         .maxOut = 10,
         .port = 4000,
         .expectedOut = 15,
         .expectedIn = 85,
         .expectedIpLimit = 6},
        {.name = "legacy max_peers 20, private",
         .maxPeers = 20,
         .maxIn = 100,
         .maxOut = 10,
         .port = 0,
         .expectedOut = 20,
         .expectedIn = 0,
         .expectedIpLimit = 1},
        {.name = "new in 100/out 10",
         .maxPeers = {},
         .maxIn = 100,
         .maxOut = 10,
         .port = 4000,
         .expectedOut = 10,
         .expectedIn = 100,
         .expectedIpLimit = 6},
        {.name = "new in 0/out 10",
         .maxPeers = {},
         .maxIn = 0,
         .maxOut = 10,
         .port = 4000,
         .expectedOut = 10,
         .expectedIn = 0,
         .expectedIpLimit = 1},
        {.name = "new in 100/out 10, private",
         .maxPeers = {},
         .maxIn = 100,
         .maxOut = 10,
         .port = 0,
         .expectedOut = 10,
         .expectedIn = 0,
         .expectedIpLimit = 6}};

    for (auto const& testCase : cases)
    {
        SCOPED_TRACE(testCase.name);

        PeerLimitConfig const limits{
            .maxPeers = testCase.maxPeers, .inPeers = testCase.maxIn, .outPeers = testCase.maxOut};

        Config const config =
            Config::makeConfig(false, false, limits, testCase.port, false, 0, true);

        Counts counts;
        counts.onConfig(config);
        EXPECT_EQ(counts.outMax(), testCase.expectedOut);
        EXPECT_EQ(counts.inMax(), testCase.expectedIn);
        EXPECT_EQ(config.ipLimit, testCase.expectedIpLimit);

        NiceMock<MockStore> store;
        allowEmptyStore(store);
        NiceMock<MockChecker> checker;
        TestStopwatch clock;
        Logic<NiceMock<MockChecker>> logic(clock, store, checker, journal());
        logic.config(config);

        EXPECT_EQ(logic.config(), config);
    }
}

TEST(PeerFinderConfig, rejects_incomplete_or_out_of_range_peer_limits)
{
    std::vector<PeerLimitConfig> const configs{
        {.maxPeers = {}, .inPeers = 100, .outPeers = {}},
        {.maxPeers = {}, .inPeers = {}, .outPeers = 100},
        {.maxPeers = {}, .inPeers = 100, .outPeers = 5},
        {.maxPeers = {}, .inPeers = 1001, .outPeers = 10},
        {.maxPeers = {}, .inPeers = 10, .outPeers = 1001}};

    for (auto const& limits : configs)
    {
        EXPECT_THROW(
            Config::makeConfig(false, false, limits, 4000, false, 0, true), std::exception);
    }
}

}  // namespace
}  // namespace xrpl::PeerFinder
