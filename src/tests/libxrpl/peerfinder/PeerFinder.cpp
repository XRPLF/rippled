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
#include <xrpl/peerfinder/detail/Handouts.h>
#include <xrpl/peerfinder/detail/Logic.h>
#include <xrpl/peerfinder/detail/SlotImp.h>
#include <xrpl/peerfinder/detail/Source.h>
#include <xrpl/peerfinder/detail/Store.h>
#include <xrpl/peerfinder/detail/Tuning.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
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

    boost::system::error_code nextError;
    bool completeAsync = true;
    std::vector<beast::IP::Endpoint> asyncConnects;

    template <class Handler>
    void
    asyncConnect(beast::IP::Endpoint const& ep, Handler&& handler)
    {
        asyncConnects.push_back(ep);
        recordAsyncConnect(ep);
        if (completeAsync)
            std::forward<Handler>(handler)(nextError);
    }
};

class TestSource : public Source
{
public:
    explicit TestSource(std::string name) : name_(std::move(name))
    {
    }

    std::string const&
    name() override
    {
        return name_;
    }

    void
    cancel() override
    {
        ++cancelCount;
    }

    void
    fetch(Results& results, beast::Journal) override
    {
        ++fetchCount;
        results = resultsToFetch;
    }

    Results resultsToFetch;
    int fetchCount = 0;
    int cancelCount = 0;

private:
    std::string name_;
};

class DefaultCancelSource : public Source
{
public:
    std::string const&
    name() override
    {
        return name_;
    }

    void
    fetch(Results& results, beast::Journal) override
    {
        results = resultsToFetch;
    }

    Results resultsToFetch;

private:
    std::string name_{"default"};
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

TEST(PeerFinderResult, converts_all_result_values_to_strings)
{
    EXPECT_EQ(to_string(Result::InboundDisabled), "inbound disabled");
    EXPECT_EQ(to_string(Result::DuplicatePeer), "peer already connected");
    EXPECT_EQ(to_string(Result::IpLimitExceeded), "ip limit exceeded");
    EXPECT_EQ(to_string(Result::Full), "slots full");
    EXPECT_EQ(to_string(Result::Success), "success");
    EXPECT_EQ(to_string(static_cast<Result>(-1)), "unknown");
}

TEST(PeerFinderEndpoint, orders_by_address)
{
    Endpoint const high{endpoint("65.0.0.2:10002"), 1};
    Endpoint const low{endpoint("65.0.0.1:10001"), 2};
    std::vector<Endpoint> endpoints{high, low};

    std::ranges::sort(
        endpoints, [](Endpoint const& lhs, Endpoint const& rhs) { return lhs < rhs; });

    EXPECT_EQ(endpoints.front().address, low.address);
    EXPECT_EQ(endpoints.back().address, high.address);
}

TEST(PeerFinderCounts, tracks_slot_states_and_capacity)
{
    TestStopwatch clock;
    Counts counts;
    Config config;
    config.outPeers = 1;
    config.inPeers = 1;
    config.wantIncoming = true;
    counts.onConfig(config);

    EXPECT_EQ(counts.outMax(), 1);
    EXPECT_EQ(counts.inMax(), 1);
    EXPECT_EQ(counts.inboundSlotsFree(), 1);
    EXPECT_EQ(counts.outboundSlotsFree(), 1);
    EXPECT_EQ(counts.totalActive(), 0);
    EXPECT_FALSE(counts.isConnectedToNetwork());
    EXPECT_EQ(counts.attemptsNeeded(), Tuning::kMaxConnectAttempts);
    EXPECT_EQ(counts.stateString(), "0/1 out, 0/1 in, 0 connecting, 0 closing");

    SlotImp inbound(endpoint("65.0.0.1:10001"), endpoint("65.0.0.2:10002"), false, clock);
    counts.add(inbound);
    EXPECT_EQ(counts.acceptCount(), 1);
    EXPECT_TRUE(counts.canActivate(inbound));
    counts.remove(inbound);
    EXPECT_EQ(counts.acceptCount(), 0);

    inbound.activate(clock.now());
    counts.add(inbound);
    EXPECT_EQ(counts.inboundActive(), 1);
    EXPECT_EQ(counts.totalActive(), 1);
    EXPECT_EQ(counts.inboundSlotsFree(), 0);

    SlotImp const extraInbound(
        endpoint("65.0.0.3:10003"), endpoint("65.0.0.4:10004"), false, clock);
    EXPECT_FALSE(counts.canActivate(extraInbound));
    counts.remove(inbound);

    SlotImp outbound(endpoint("65.0.0.5:10005"), false, clock);
    counts.add(outbound);
    EXPECT_EQ(counts.attempts(), 1);
    EXPECT_EQ(counts.connectCount(), 1);
    EXPECT_EQ(counts.attemptsNeeded(), Tuning::kMaxConnectAttempts - 1);
    counts.remove(outbound);

    outbound.state(Slot::State::Connected);
    EXPECT_TRUE(counts.canActivate(outbound));
    outbound.activate(clock.now());
    counts.add(outbound);
    EXPECT_EQ(counts.outActive(), 1);
    EXPECT_EQ(counts.outboundSlotsFree(), 0);

    SlotImp extraOutbound(endpoint("65.0.0.6:10006"), false, clock);
    extraOutbound.state(Slot::State::Connected);
    EXPECT_FALSE(counts.canActivate(extraOutbound));

    SlotImp fixedOutbound(endpoint("65.0.0.7:10007"), true, clock);
    fixedOutbound.state(Slot::State::Connected);
    EXPECT_TRUE(counts.canActivate(fixedOutbound));
    fixedOutbound.activate(clock.now());
    counts.add(fixedOutbound);
    EXPECT_EQ(counts.fixed(), 1u);
    EXPECT_EQ(counts.fixedActive(), 1u);
    counts.remove(fixedOutbound);

    SlotImp reservedOutbound(endpoint("65.0.0.8:10008"), false, clock);
    reservedOutbound.reserved(true);
    reservedOutbound.state(Slot::State::Connected);
    EXPECT_TRUE(counts.canActivate(reservedOutbound));
    reservedOutbound.activate(clock.now());
    counts.add(reservedOutbound);

    JsonPropertyStream stream;
    {
        beast::PropertyStream::Map map(stream);
        counts.onWrite(map);
    }
    EXPECT_TRUE(stream.top().isMember("accept"));
    EXPECT_TRUE(stream.top().isMember("connect"));
    EXPECT_TRUE(stream.top().isMember("close"));
    EXPECT_TRUE(stream.top().isMember("reserved"));
    EXPECT_TRUE(stream.top().isMember("total"));
    counts.remove(reservedOutbound);
    counts.remove(outbound);

    SlotImp closing(endpoint("65.0.0.9:10009"), endpoint("65.0.0.10:10010"), false, clock);
    closing.state(Slot::State::Closing);
    counts.add(closing);
    EXPECT_EQ(counts.closingCount(), 1);
    counts.remove(closing);

    Counts saturatedAttempts;
    saturatedAttempts.onConfig(config);
    std::vector<std::unique_ptr<SlotImp>> attempts;
    for (int i = 0; i < Tuning::kMaxConnectAttempts; ++i)
    {
        attempts.push_back(
            std::make_unique<SlotImp>(
                endpoint("65.1.0." + std::to_string(i + 1) + ":" + std::to_string(11000 + i)),
                false,
                clock));
        saturatedAttempts.add(*attempts.back());
    }
    EXPECT_EQ(saturatedAttempts.attempts(), Tuning::kMaxConnectAttempts);
    EXPECT_EQ(saturatedAttempts.attemptsNeeded(), 0u);

    Config disconnected;
    disconnected.outPeers = 0;
    counts.onConfig(disconnected);
    EXPECT_TRUE(counts.isConnectedToNetwork());
}

TEST(PeerFinderHandouts, filters_redirect_slot_and_connect_targets)
{
    TestStopwatch clock;
    auto const remote = endpoint("65.0.0.2:10002");
    auto const slot = std::make_shared<SlotImp>(endpoint("65.0.0.1:10001"), remote, false, clock);

    RedirectHandouts redirects(slot);
    EXPECT_EQ(redirects.slot(), slot);
    EXPECT_TRUE(redirects.list().empty());
    EXPECT_FALSE(redirects.full());
    EXPECT_FALSE(redirects.tryInsert(Endpoint{endpoint("65.0.0.3:10003"), Tuning::kMaxHops + 1}));
    EXPECT_FALSE(redirects.tryInsert(Endpoint{endpoint("65.0.0.3:10003"), 0}));
    EXPECT_FALSE(redirects.tryInsert(Endpoint{remote.atPort(12000), 1}));
    EXPECT_TRUE(redirects.tryInsert(Endpoint{endpoint("65.0.0.3:10003"), 1}));
    EXPECT_FALSE(redirects.tryInsert(Endpoint{endpoint("65.0.0.3:12000"), 1}));
    EXPECT_EQ(redirects.list().size(), 1u);

    SlotHandouts slotHandouts(slot);
    EXPECT_EQ(slotHandouts.slot(), slot);
    EXPECT_FALSE(slotHandouts.full());
    EXPECT_FALSE(
        slotHandouts.tryInsert(Endpoint{endpoint("65.0.0.4:10004"), Tuning::kMaxHops + 1}));
    EXPECT_FALSE(slotHandouts.tryInsert(Endpoint{remote.atPort(12001), 1}));

    auto const recent = endpoint("65.0.0.5:10005");
    slot->recent.insert(recent, 2);
    EXPECT_FALSE(slotHandouts.tryInsert(Endpoint{recent, 2}));
    EXPECT_TRUE(slotHandouts.tryInsert(Endpoint{endpoint("65.0.0.6:10006"), 2}));
    EXPECT_FALSE(slotHandouts.tryInsert(Endpoint{endpoint("65.0.0.6:12000"), 2}));
    slotHandouts.insert(Endpoint{endpoint("65.0.0.7:10007"), 1});
    EXPECT_EQ(slotHandouts.list().size(), 2u);

    ConnectHandouts::Squelches squelches(clock);
    ConnectHandouts connects(2, squelches);
    EXPECT_TRUE(connects.empty());
    EXPECT_TRUE(connects.tryInsert(endpoint("65.0.0.8:10008")));
    EXPECT_FALSE(connects.empty());
    EXPECT_FALSE(connects.tryInsert(endpoint("65.0.0.8:12000")));
    EXPECT_TRUE(connects.tryInsert(Endpoint{endpoint("65.0.0.9:10009"), 1}));
    EXPECT_TRUE(connects.full());
    EXPECT_FALSE(connects.tryInsert(endpoint("65.0.0.10:10010")));
    EXPECT_EQ(connects.list().size(), 2u);

    ConnectHandouts squelched(1, squelches);
    EXPECT_FALSE(squelched.tryInsert(endpoint("65.0.0.9:12000")));
}

TEST(PeerFinderHandouts, distributes_livecache_entries)
{
    TestStopwatch clock;
    Livecache<> cache(clock, journal());
    cache.insert(Endpoint{endpoint("65.0.0.10:10010"), 1});
    cache.insert(Endpoint{endpoint("65.0.0.11:10011"), 2});

    auto const slot1 = std::make_shared<SlotImp>(
        endpoint("65.0.0.1:10001"), endpoint("65.0.0.2:10002"), false, clock);
    auto const slot2 = std::make_shared<SlotImp>(
        endpoint("65.0.0.3:10003"), endpoint("65.0.0.4:10004"), false, clock);
    std::vector<SlotHandouts> targets;
    targets.emplace_back(slot1);
    targets.emplace_back(slot2);

    handout(targets.begin(), targets.end(), cache.hops.begin(), cache.hops.end());

    EXPECT_FALSE(targets.front().list().empty());
    EXPECT_FALSE(targets.back().list().empty());

    for (std::uint32_t i = 0; i < Tuning::kNumberOfEndpoints; ++i)
        targets.front().insert(Endpoint{endpoint("65.1.0." + std::to_string(i + 1) + ":12000"), 1});

    handout(targets.begin(), targets.begin() + 1, cache.hops.begin(), cache.hops.end());
    EXPECT_TRUE(targets.front().full());
}

TEST_F(PeerFinderTest, preprocess_filters_invalid_duplicate_and_extra_self_endpoints)
{
    auto const local = endpoint("65.0.0.1:10001");
    auto const remote = endpoint("65.0.0.2:10002");
    auto const slot = std::make_shared<SlotImp>(local, remote, false, clock_);
    Endpoints endpoints{
        Endpoint{endpoint("65.0.0.3:10003"), Tuning::kMaxHops + 1},
        Endpoint{endpoint("0.0.0.0:2459"), 0},
        Endpoint{endpoint("0.0.0.0:2460"), 0},
        Endpoint{endpoint("10.0.0.1:10004"), 1},
        Endpoint{endpoint("65.0.0.5"), 1},
        Endpoint{endpoint("65.0.0.6:10006"), 1},
        Endpoint{endpoint("65.0.0.6:10006"), 2}};

    logic_.preprocess(slot, endpoints);

    ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_EQ(endpoints.front().address, remote.atPort(2459));
    EXPECT_EQ(endpoints.front().hops, 1u);
    EXPECT_EQ(endpoints.back().address, endpoint("65.0.0.6:10006"));
    EXPECT_EQ(endpoints.back().hops, 2u);
}

TEST_F(PeerFinderTest, on_endpoints_checks_neighbor_before_caching_it)
{
    Config config;
    config.autoConnect = false;
    config.listeningPort = 1024;
    config.ipLimit = 2;
    config.inPeers = 1;
    logic_.config(config);

    auto const local = endpoint("65.0.0.1:10001");
    auto const remote = endpoint("55.104.0.2:1025");
    auto const [slot, result] = logic_.newInboundSlot(local, remote);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);
    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);
    ASSERT_EQ(logic_.activate(slot, publicKey, false), Result::Success);

    Endpoints const advertised{Endpoint{endpoint("0.0.0.0:2459"), 0}};
    logic_.onEndpoints(slot, advertised);

    ASSERT_EQ(checker_.asyncConnects.size(), 1u);
    EXPECT_EQ(checker_.asyncConnects.front(), remote.atPort(2459));
    EXPECT_EQ(slot->listeningPort(), std::optional<std::uint16_t>{2459});
    EXPECT_TRUE(slot->checked);
    EXPECT_TRUE(slot->canAccept);
    EXPECT_TRUE(logic_.livecache.empty());

    clock_.advance(Tuning::kSecondsPerMessage);
    logic_.onEndpoints(slot, advertised);
    EXPECT_EQ(logic_.livecache.size(), 1u);
    EXPECT_EQ(logic_.bootcache.size(), 1u);

    logic_.onEndpoints(slot, Endpoints{Endpoint{endpoint("65.0.0.9:10009"), 1}});
    EXPECT_EQ(logic_.livecache.size(), 1u);

    logic_.onClosed(slot);
}

TEST_F(PeerFinderTest, on_endpoints_skips_failed_neighbor_connectivity_checks)
{
    Config config;
    config.autoConnect = false;
    config.listeningPort = 1024;
    config.ipLimit = 2;
    config.inPeers = 1;
    logic_.config(config);

    checker_.nextError = boost::asio::error::host_unreachable;
    auto const local = endpoint("65.0.0.1:10001");
    auto const remote = endpoint("55.104.0.3:1025");
    auto const [slot, result] = logic_.newInboundSlot(local, remote);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);
    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);
    ASSERT_EQ(logic_.activate(slot, publicKey, false), Result::Success);

    Endpoints const advertised{Endpoint{endpoint("0.0.0.0:2459"), 0}};
    logic_.onEndpoints(slot, advertised);
    EXPECT_TRUE(slot->checked);
    EXPECT_FALSE(slot->canAccept);

    clock_.advance(Tuning::kSecondsPerMessage);
    logic_.onEndpoints(slot, advertised);
    EXPECT_TRUE(logic_.livecache.empty());

    logic_.onClosed(slot);
}

TEST_F(PeerFinderTest, on_endpoints_waits_for_pending_connectivity_check)
{
    Config config;
    config.autoConnect = false;
    config.listeningPort = 1024;
    config.ipLimit = 2;
    config.inPeers = 1;
    logic_.config(config);

    checker_.completeAsync = false;
    auto const local = endpoint("65.0.0.1:10001");
    auto const remote = endpoint("55.104.0.4:1025");
    auto const [slot, result] = logic_.newInboundSlot(local, remote);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);
    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);
    ASSERT_EQ(logic_.activate(slot, publicKey, false), Result::Success);

    Endpoints const advertised{Endpoint{endpoint("0.0.0.0:2459"), 0}};
    logic_.onEndpoints(slot, advertised);
    EXPECT_TRUE(slot->connectivityCheckInProgress);

    clock_.advance(Tuning::kSecondsPerMessage);
    logic_.onEndpoints(slot, advertised);
    EXPECT_EQ(checker_.asyncConnects.size(), 1u);
    EXPECT_TRUE(logic_.livecache.empty());

    checker_.completeAsync = true;
    logic_.checkComplete(remote, remote.atPort(2459), boost::asio::error::operation_aborted);
    slot->connectivityCheckInProgress = false;
    logic_.onClosed(slot);
}

TEST_F(PeerFinderTest, builds_endpoint_messages_and_redirects_from_livecache)
{
    Config config;
    config.autoConnect = false;
    config.wantIncoming = true;
    config.listeningPort = 2459;
    config.inPeers = 2;
    config.outPeers = 2;
    config.ipLimit = 2;
    logic_.config(config);

    auto const remote = endpoint("55.104.0.5:1025");
    auto const live = endpoint("65.0.0.10:10010");
    logic_.livecache.insert(Endpoint{live, 1});

    auto const [slot, result] = logic_.newOutboundSlot(remote);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);
    ASSERT_TRUE(logic_.onConnected(slot, endpoint("65.0.0.1:10001")));
    PublicKey const publicKey(randomKeyPair(KeyType::Secp256k1).first);
    ASSERT_EQ(logic_.activate(slot, publicKey, false), Result::Success);

    auto const messages = logic_.buildEndpointsForPeers();
    ASSERT_EQ(messages.size(), 1u);
    auto const& sent = messages.front().second;
    EXPECT_TRUE(std::ranges::any_of(sent, [](Endpoint const& ep) { return ep.hops == 0; }));
    EXPECT_TRUE(
        std::ranges::any_of(sent, [&live](Endpoint const& ep) { return ep.address == live; }));
    EXPECT_TRUE(logic_.buildEndpointsForPeers().empty());

    auto const redirects = logic_.redirect(slot);
    EXPECT_FALSE(redirects.empty());

    logic_.onClosed(slot);
}

TEST_F(PeerFinderTest, autoconnect_uses_livecache_then_bootcache)
{
    Config config;
    config.autoConnect = true;
    config.wantIncoming = false;
    config.outPeers = 1;
    config.inPeers = 0;
    config.ipLimit = 1;
    logic_.config(config);

    auto const live = endpoint("65.0.0.11:10011");
    logic_.livecache.insert(Endpoint{live, 1});
    auto const liveAddresses = logic_.autoconnect();
    ASSERT_EQ(liveAddresses.size(), 1u);
    EXPECT_EQ(liveAddresses.front(), live);

    auto const boot = endpoint("65.0.0.12:10012");
    EXPECT_TRUE(logic_.bootcache.insertStatic(boot));
    auto const bootAddresses = logic_.autoconnect();
    ASSERT_EQ(bootAddresses.size(), 1u);
    EXPECT_EQ(bootAddresses.front(), boot);
}

TEST_F(PeerFinderTest, sources_redirects_status_and_validation_paths_are_exercised)
{
    auto const source = std::make_shared<TestSource>("static");
    source->resultsToFetch.addresses = {endpoint("65.0.0.13:10013")};
    logic_.addStaticSource(source);
    EXPECT_EQ(source->fetchCount, 1);
    EXPECT_EQ(logic_.bootcache.size(), 1u);

    auto const failing = std::make_shared<TestSource>("failing");
    failing->resultsToFetch.error = boost::asio::error::host_unreachable;
    logic_.fetch(failing);
    EXPECT_EQ(failing->fetchCount, 1);

    auto const dynamic = std::make_shared<TestSource>("dynamic");
    logic_.addSource(dynamic);
    ASSERT_EQ(logic_.sources.size(), 1u);
    EXPECT_EQ(logic_.sources.front(), dynamic);

    std::vector<boost::asio::ip::tcp::endpoint> redirects{
        {boost::asio::ip::make_address("65.0.0.14"), 10014},
        {boost::asio::ip::make_address("65.0.0.15"), 10015}};
    logic_.onRedirects(redirects.begin(), redirects.end(), redirects.front());
    EXPECT_EQ(logic_.bootcache.size(), 3u);

    EXPECT_FALSE(logic_.isValidAddress(endpoint("0.0.0.0:10016")));
    EXPECT_FALSE(logic_.isValidAddress(endpoint("10.0.0.1:10017")));
    EXPECT_FALSE(logic_.isValidAddress(endpoint("65.0.0.16")));
    EXPECT_TRUE(logic_.isValidAddress(endpoint("65.0.0.16:10016")));

    JsonPropertyStream stream;
    {
        beast::PropertyStream::Map map(stream);
        logic_.onWrite(map);
    }
    EXPECT_TRUE(stream.top().isMember("peers"));
    EXPECT_TRUE(stream.top().isMember("counts"));
    EXPECT_TRUE(stream.top().isMember("config"));
    EXPECT_TRUE(stream.top().isMember("livecache"));
    EXPECT_TRUE(stream.top().isMember("bootcache"));

    DefaultCancelSource defaultCancel;
    Source::Results results;
    EXPECT_TRUE(results.addresses.empty());
    defaultCancel.cancel();
    defaultCancel.fetch(results, journal());

    logic_.fetchSource = dynamic;
    logic_.stop();
    EXPECT_TRUE(logic_.stopping);
    EXPECT_EQ(dynamic->cancelCount, 1);

    auto const ignored = std::make_shared<TestSource>("ignored");
    logic_.fetch(ignored);
    EXPECT_EQ(ignored->fetchCount, 0);

    logic_.checkComplete(
        endpoint("65.0.0.18:10018"), endpoint("65.0.0.19:10019"), boost::system::error_code{});
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

TEST(PeerFinderConfig, calculates_outbound_peers_and_clamps_ip_limits)
{
    Config config;
    config.maxPeers = 1;
    EXPECT_EQ(config.calcOutPeers(), Tuning::kMinOutCount);

    config.maxPeers = 100;
    EXPECT_EQ(config.calcOutPeers(), 15u);

    config.inPeers = 1;
    config.ipLimit = 0;
    config.applyTuning();
    EXPECT_EQ(config.ipLimit, 1);

    Config explicitLimit;
    explicitLimit.inPeers = 8;
    explicitLimit.ipLimit = 99;
    explicitLimit.applyTuning();
    EXPECT_EQ(explicitLimit.ipLimit, 4);

    Config largeInbound;
    largeInbound.inPeers = 200;
    largeInbound.ipLimit = 0;
    largeInbound.applyTuning();
    EXPECT_EQ(largeInbound.ipLimit, 7);
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
