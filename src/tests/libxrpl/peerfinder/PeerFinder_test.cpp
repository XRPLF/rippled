#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/detail/Counts.h>
#include <xrpld/peerfinder/detail/Logic.h>
#include <xrpld/peerfinder/detail/Store.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
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

TEST_F(PeerFinderTest, BackoffLimitsRepeatedConnectionAttempts)
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

TEST_F(PeerFinderTest, ActivatedPeerBackoffAllowsAtMostOneAttemptPerMinute)
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

TEST_F(PeerFinderTest, DuplicateInboundSlotIsRejectedForExistingOutboundSlot)
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

TEST_F(PeerFinderTest, DuplicateOutboundSlotIsRejectedForExistingInboundSlot)
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

TEST_F(PeerFinderTest, PeerLimitExceededRejectsAdditionalInboundSlot)
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

TEST_F(PeerFinderTest, ActivateRejectsDuplicatePublicKey)
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

TEST_F(PeerFinderTest, ActivateRejectsInboundWhenInboundConnectionsAreDisabled)
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

TEST_F(PeerFinderTest, AddFixedPeerRejectsEndpointWithoutPort)
{
    EXPECT_THROW(logic_.addFixedPeer("test", endpoint("65.0.0.2")), std::runtime_error);
}

TEST_F(PeerFinderTest, OnConnectedRejectsSelfConnection)
{
    auto const local = endpoint("65.0.0.2:1234");
    auto const [slot, result] = logic_.newOutboundSlot(local);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(result, Result::Success);

    EXPECT_FALSE(logic_.onConnected(slot, local));
    logic_.onClosed(slot);
}

TEST(PeerFinderConfig, AppliesLegacyAndExplicitPeerLimits)
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

        xrpl::Config serverConfig;
        std::string configText;
        auto max = 0;
        if (testCase.maxPeers)
        {
            max = *testCase.maxPeers;
            configText += "[peers_max]\n" + std::to_string(max) + "\n[peers_in_max]\n" +
                std::to_string(testCase.maxIn.value_or(0)) + "\n[peers_out_max]\n" +
                std::to_string(testCase.maxOut.value_or(0)) + "\n";
        }
        else if (testCase.maxIn && testCase.maxOut)
        {
            configText += "[peers_in_max]\n" + std::to_string(*testCase.maxIn) +
                "\n[peers_out_max]\n" + std::to_string(*testCase.maxOut) + "\n";
        }

        serverConfig.loadFromString(configText);
        if (testCase.maxPeers || (!testCase.maxIn && !testCase.maxOut))
        {
            EXPECT_EQ(serverConfig.PEERS_MAX, static_cast<std::size_t>(max));
            EXPECT_EQ(serverConfig.PEERS_IN_MAX, 0u);
            EXPECT_EQ(serverConfig.PEERS_OUT_MAX, 0u);
        }
        else
        {
            EXPECT_EQ(serverConfig.PEERS_IN_MAX, testCase.maxIn.value_or(0));
            EXPECT_EQ(serverConfig.PEERS_OUT_MAX, testCase.maxOut.value_or(0));
        }

        Config const config = Config::makeConfig(serverConfig, testCase.port, false, 0);

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

TEST(PeerFinderConfig, RejectsIncompleteOrOutOfRangePeerLimits)
{
    std::vector<std::string> const configs{
        R"xrpldConfig(
[peers_in_max]
100
)xrpldConfig",
        R"xrpldConfig(
[peers_out_max]
100
)xrpldConfig",
        R"xrpldConfig(
[peers_in_max]
100
[peers_out_max]
5
)xrpldConfig",
        R"xrpldConfig(
[peers_in_max]
1001
[peers_out_max]
10
)xrpldConfig",
        R"xrpldConfig(
[peers_in_max]
10
[peers_out_max]
1001
)xrpldConfig"};

    for (auto const& configText : configs)
    {
        xrpl::Config config;
        EXPECT_THROW(config.loadFromString(configText), std::exception);
    }
}

}  // namespace
}  // namespace xrpl::PeerFinder
