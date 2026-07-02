#include <test/jtx/Env.h>
#include <test/jtx/noop.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class tx_reduce_relay_test : public beast::unit_test::Suite
{
public:
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

private:
    void
    doTest(std::string const& msg, bool log, std::function<void(bool)> f)
    {
        testcase(msg);
        f(log);
    }

    void
    testConfig(bool log)
    {
        doTest("Config Test", log, [&](bool log) {
            auto test = [&](bool enable,
                            bool metrics,
                            std::uint16_t min,
                            std::uint16_t pct,
                            bool success = true) {
                std::stringstream str("[reduce_relay]");
                str << "[reduce_relay]\n"
                    << "tx_enable=" << static_cast<int>(enable) << "\n"
                    << "tx_metrics=" << static_cast<int>(metrics) << "\n"
                    << "tx_min_peers=" << min << "\n"
                    << "tx_relay_percentage=" << pct << "\n";
                Config c;
                try
                {
                    c.loadFromString(str.str());

                    BEAST_EXPECT(c.txReduceRelayEnable == enable);
                    BEAST_EXPECT(c.txReduceRelayMetrics == metrics);
                    BEAST_EXPECT(c.txReduceRelayMinPeers == min);
                    BEAST_EXPECT(c.txRelayPercentage == pct);
                    if (success)
                    {
                        pass();
                    }
                    else
                    {
                        fail();
                    }
                }
                catch (...)
                {
                    if (success)
                    {
                        fail();
                    }
                    else
                    {
                        pass();
                    }
                }
            };

            test(true, true, 20, 25);
            test(false, false, 20, 25);
            test(false, false, 20, 0, false);
            test(false, false, 20, 101, false);
            test(false, false, 9, 10, false);
            test(false, false, 10, 9, false);
        });
    }

    class PeerTest : public PeerImp
    {
    public:
        PeerTest(
            Application& app,
            std::shared_ptr<PeerFinder::Slot> const& slot,
            http_request_type&& request,
            PublicKey const& publicKey,
            ProtocolVersion protocol,
            Resource::Consumer consumer,
            std::unique_ptr<tx_reduce_relay_test::stream_type>&& streamPtr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  sid,
                  slot,
                  std::move(request),
                  publicKey,
                  protocol,
                  consumer,
                  std::move(streamPtr),
                  overlay)
        {
            sid++;
        }
        ~PeerTest() override = default;

        void
        run() override
        {
        }
        void
        send(std::shared_ptr<Message> const&) override
        {
            sendTx++;
        }
        void
        addTxQueue(uint256 const& hash) override
        {
            queueTx++;
        }
        static void
        init()
        {
            queueTx = 0;
            sendTx = 0;
            sid = 0;
        }
        inline static std::size_t sid = 0;
        inline static std::uint16_t queueTx = 0;
        inline static std::uint16_t sendTx = 0;
    };

    std::uint16_t lid_{0};
    std::uint16_t rid_{1};
    shared_context context_;
    ProtocolVersion protocolVersion_;
    boost::beast::multi_buffer readBuf_;

public:
    tx_reduce_relay_test() : context_(makeSslContext("")), protocolVersion_{1, 7}
    {
    }

private:
    void
    addPeer(jtx::Env& env, std::vector<std::shared_ptr<PeerTest>>& peers, std::uint16_t& nDisabled)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        boost::beast::http::request<boost::beast::http::dynamic_body> request;
        (nDisabled == 0)
            ? request.insert("X-Protocol-Ctl", makeFeaturesRequestHeader(false, false, true, false))
            : (void)nDisabled--;
        auto streamPtr = std::make_unique<stream_type>(
            socket_type(std::forward<boost::asio::io_context&>(env.app().getIOContext())),
            *context_);
        beast::IP::Endpoint const local(
            boost::asio::ip::make_address("172.1.1." + std::to_string(lid_)));
        beast::IP::Endpoint const remote(
            boost::asio::ip::make_address("172.1.1." + std::to_string(rid_)));
        PublicKey const key(std::get<0>(randomKeyPair(KeyType::Ed25519)));
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);
        auto const peer = std::make_shared<PeerTest>(
            env.app(),
            slot,
            std::move(request),
            key,
            protocolVersion_,
            consumer,
            std::move(streamPtr),
            overlay);
        BEAST_EXPECT(overlay.findPeerByPublicKey(key) == std::shared_ptr<PeerImp>{});
        overlay.addActive(peer);
        BEAST_EXPECT(overlay.findPeerByPublicKey(key) == peer);
        peers.emplace_back(peer);  // overlay stores week ptr to PeerImp
        lid_ += 2;
        rid_ += 2;
        assert(lid_ <= 254);
    }

    void
    testRelay(
        std::string const& test,
        bool txRREnabled,
        std::uint16_t nPeers,
        std::uint16_t nDisabled,
        std::uint16_t minPeers,
        std::uint16_t relayPercentage,
        std::uint16_t expectRelay,
        std::uint16_t expectQueue,
        std::set<Peer::id_t> const& toSkip = {})
    {
        testcase(test);
        jtx::Env env(*this);
        std::vector<std::shared_ptr<PeerTest>> peers;
        env.app().config().txReduceRelayEnable = txRREnabled;
        env.app().config().txReduceRelayMinPeers = minPeers;
        env.app().config().txRelayPercentage = relayPercentage;
        PeerTest::init();
        lid_ = 0;
        rid_ = 0;
        for (int i = 0; i < nPeers; i++)
            addPeer(env, peers, nDisabled);

        auto const jtx = env.jt(noop(env.master));
        if (BEAST_EXPECT(jtx.stx))
        {
            protocol::TMTransaction m;
            Serializer s;
            jtx.stx->add(s);
            m.set_rawtransaction(s.data(), s.size());
            m.set_deferred(false);
            m.set_status(protocol::TransactionStatus::tsNEW);
            env.app().getOverlay().relay(uint256{0}, m, toSkip);
            BEAST_EXPECT(PeerTest::sendTx == expectRelay && PeerTest::queueTx == expectQueue);
        }
    }

    void
    run() override
    {
        bool const log = false;
        std::set<Peer::id_t> skip = {0, 1, 2, 3, 4};
        testConfig(log);
        // relay to all peers, no hash queue
        testRelay("feature disabled", false, 10, 0, 10, 25, 10, 0);
        // relay to nPeers - skip (10-5=5)
        testRelay("feature disabled & skip", false, 10, 0, 10, 25, 5, 0, skip);
        // relay to all peers because min is greater than nPeers
        testRelay("relay all 1", true, 10, 0, 20, 25, 10, 0);
        // relay to all peers because min + disabled is greater thant nPeers
        testRelay("relay all 2", true, 20, 15, 10, 25, 20, 0);
        // relay to minPeers + 25% of nPeers-minPeers (20+0.25*(60-20)=30),
        // queue the rest (30)
        testRelay("relay & queue", true, 60, 0, 20, 25, 30, 30);
        // relay to minPeers + 25% of (nPeers - nPeers) - skip
        // (20+0.25*(60-20)-5=25), queue the rest, skip counts towards relayed
        // (60-25-5=30)
        testRelay("skip", true, 60, 0, 20, 25, 25, 30, skip);
        // relay to minPeers + disabled + 25% of (nPeers - minPeers - disabled)
        // (20+10+0.25*(70-20-10)=40), queue the rest (30)
        testRelay("disabled", true, 70, 10, 20, 25, 40, 30);
        // relay to minPeers + disabled-not-in-skip + 25% of (nPeers - minPeers
        // - disabled) (20+5+0.25*(70-20-10)=35), queue the rest, skip counts
        // towards relayed (70-35-5=30))
        testRelay("disabled & skip", true, 70, 10, 20, 25, 35, 30, skip);
        // relay to minPeers + disabled + 25% of (nPeers - minPeers - disabled)
        // - skip (10+5+0.25*(15-10-5)-10=5), queue the rest, skip counts
        // towards relayed (15-5-10=0)
        skip = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        testRelay("disabled & skip, no queue", true, 15, 5, 10, 25, 5, 0, skip);
        // relay to minPeers + disabled + 25% of (nPeers - minPeers - disabled)
        // - skip (10+2+0.25*(20-10-2)-14=0), queue the rest, skip counts
        // towards relayed (20-14=6)
        skip = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
        testRelay("disabled & skip, no relay", true, 20, 2, 10, 25, 0, 6, skip);
    }
};

BEAST_DEFINE_TESTSUITE(tx_reduce_relay, overlay, xrpl);
}  // namespace xrpl::test
