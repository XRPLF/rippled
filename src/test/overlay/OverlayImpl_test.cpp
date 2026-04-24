#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/TrafficCount.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class OverlayImpl_test : public beast::unit_test::suite
{
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    class PeerTest : public PeerImp
    {
    public:
        PeerTest(
            Application& app,
            Peer::id_t id,
            std::shared_ptr<PeerFinder::Slot> const& slot,
            http_request_type&& request,
            PublicKey const& publicKey,
            ProtocolVersion protocol,
            Resource::Consumer consumer,
            std::unique_ptr<OverlayImpl_test::stream_type>&& streamPtr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  id,
                  slot,
                  std::move(request),
                  publicKey,
                  protocol,
                  consumer,
                  std::move(streamPtr),
                  overlay)
        {
        }

        void
        run() override
        {
        }

        void
        send(std::shared_ptr<Message> const& m) override
        {
            sent_.push_back(m);
        }

        void
        addTxQueue(uint256 const& hash) override
        {
            txQueue_.push_back(hash);
        }

        std::vector<std::shared_ptr<Message>> const&
        sent() const
        {
            return sent_;
        }

        std::vector<uint256> const&
        txQueue() const
        {
            return txQueue_;
        }

        void
        clear()
        {
            sent_.clear();
            txQueue_.clear();
        }

    private:
        std::vector<std::shared_ptr<Message>> sent_;
        std::vector<uint256> txQueue_;
    };

    shared_context context_{make_SSLContext("")};
    Peer::id_t nextID_{1};
    std::uint16_t nextPort_{42000};

    static std::unique_ptr<Config>
    config()
    {
        return jtx::envconfig([](std::unique_ptr<Config> cfg) {
            cfg->TX_REDUCE_RELAY_ENABLE = true;
            cfg->TX_REDUCE_RELAY_METRICS = true;
            cfg->TX_REDUCE_RELAY_MIN_PEERS = 1;
            cfg->TX_RELAY_PERCENTAGE = 0;
            return cfg;
        });
    }

    static std::string
    bytes(uint256 const& value)
    {
        return {reinterpret_cast<char const*>(value.data()), value.size()};
    }

    static uint256
    digest(std::string const& value)
    {
        return sha512Half(value);
    }

    std::shared_ptr<PeerTest>
    createPeer(jtx::Env& env, bool txReduceRelay = true)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        http_request_type request;
        request.insert("User-Agent", "OverlayImpl_test/1.0");
        request.insert("Server-Domain", "overlay.example");
        if (txReduceRelay)
        {
            request.insert("X-Protocol-Ctl", makeFeaturesRequestHeader(false, false, true, false));
        }

        auto streamPtr =
            std::make_unique<stream_type>(socket_type(env.app().getIOContext()), *context_);

        auto const port = nextPort_++;
        beast::IP::Endpoint const local(boost::asio::ip::make_address("172.17.0.1"), port);
        beast::IP::Endpoint const remote(boost::asio::ip::make_address("172.17.0.2"), port);

        PublicKey const key(std::get<0>(randomKeyPair(KeyType::ed25519)));
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, result] = overlay.peerFinder().new_inbound_slot(local, remote);
        BEAST_EXPECT(result == PeerFinder::Result::success);

        auto peer = std::make_shared<PeerTest>(
            env.app(),
            nextID_++,
            slot,
            std::move(request),
            key,
            make_protocol(2, 2),
            consumer,
            std::move(streamPtr),
            overlay);

        overlay.add_active(peer);
        return peer;
    }

    protocol::TMProposeSet
    proposal()
    {
        protocol::TMProposeSet message;
        message.set_proposeseq(1);
        message.set_currenttxhash(bytes(digest("current")));
        message.set_nodepubkey("node");
        message.set_closetime(1);
        message.set_signature("signature");
        message.set_previousledger(bytes(digest("previous")));
        return message;
    }

    protocol::TMValidation
    validation()
    {
        protocol::TMValidation message;
        message.set_validation("validation");
        return message;
    }

    void
    testActivePeerLookup()
    {
        testcase("active peer lookup");

        jtx::Env env(*this, config());
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        auto enabledSkip = createPeer(env, true);
        auto disabledSkip = createPeer(env, false);
        auto enabledActive = createPeer(env, true);

        BEAST_EXPECT(overlay.size() == 3);
        BEAST_EXPECT(overlay.getActivePeers().size() == 3);
        BEAST_EXPECT(overlay.findPeerByShortID(disabledSkip->id()) == disabledSkip);
        BEAST_EXPECT(overlay.findPeerByShortID(999999) == nullptr);
        BEAST_EXPECT(overlay.findPeerByPublicKey(enabledSkip->getNodePublic()) == enabledSkip);

        std::size_t active = 0;
        std::size_t disabled = 0;
        std::size_t enabledInSkip = 0;
        std::set<Peer::id_t> const skip{enabledSkip->id(), disabledSkip->id()};
        auto peers = overlay.getActivePeers(skip, active, disabled, enabledInSkip);

        BEAST_EXPECT(active == 3);
        BEAST_EXPECT(disabled == 1);
        BEAST_EXPECT(enabledInSkip == 1);
        BEAST_EXPECT(peers.size() == 1);
        BEAST_EXPECT(peers.front() == enabledActive);
    }

    void
    testBroadcastAndRelay()
    {
        testcase("broadcast and relay");

        jtx::Env env(*this, config());
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        auto peer1 = createPeer(env, true);
        auto peer2 = createPeer(env, true);
        PublicKey const validator(std::get<0>(randomKeyPair(KeyType::ed25519)));

        auto propose = proposal();
        overlay.broadcast(propose);
        BEAST_EXPECT(peer1->sent().size() == 1);
        BEAST_EXPECT(peer2->sent().size() == 1);

        auto valid = validation();
        overlay.broadcast(valid);
        BEAST_EXPECT(peer1->sent().size() == 2);
        BEAST_EXPECT(peer2->sent().size() == 2);

        auto skippedProposal = overlay.relay(propose, digest("proposal relay"), validator);
        BEAST_EXPECT(skippedProposal.empty());
        BEAST_EXPECT(peer1->sent().size() == 3);
        BEAST_EXPECT(peer2->sent().size() == 3);
        BEAST_EXPECT(peer1->sent().back()->getValidatorKey() == validator);

        auto skippedValidation = overlay.relay(valid, digest("validation relay"), validator);
        BEAST_EXPECT(skippedValidation.empty());
        BEAST_EXPECT(peer1->sent().size() == 4);
        BEAST_EXPECT(peer2->sent().size() == 4);
        BEAST_EXPECT(peer2->sent().back()->getValidatorKey() == validator);
    }

    void
    testTransactionHashRelay()
    {
        testcase("transaction hash relay");

        jtx::Env env(*this, config());
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        auto skipped = createPeer(env, true);
        auto enabled = createPeer(env, true);
        auto disabled = createPeer(env, false);
        auto const hash = digest("queued transaction");

        overlay.relay(hash, std::nullopt, {skipped->id()});

        BEAST_EXPECT(skipped->txQueue().empty());
        BEAST_EXPECT(enabled->txQueue().size() == 1);
        BEAST_EXPECT(disabled->txQueue().size() == 1);
        BEAST_EXPECT(enabled->txQueue().front() == hash);
        BEAST_EXPECT(disabled->txQueue().front() == hash);

        overlay.reportInboundTraffic(TrafficCount::category::base, 10);
        overlay.reportOutboundTraffic(TrafficCount::category::base, 20);
    }

    void
    run() override
    {
        testActivePeerLookup();
        testBroadcastAndRelay();
        testTransactionHashRelay();
    }
};

BEAST_DEFINE_TESTSUITE(OverlayImpl, overlay, xrpl);

}  // namespace xrpl::test
