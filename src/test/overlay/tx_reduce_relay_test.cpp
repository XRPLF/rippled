//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2020 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/peerfinder/impl/SlotImp.h>
#include <test/jtx/Env.h>

namespace ripple {

namespace test {

class tx_reduce_relay_test : public beast::unit_test::suite
{
public:
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

private:
    void
    doTest(const std::string& msg, bool log, std::function<void(bool)> f)
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

                    BEAST_EXPECT(c.TX_REDUCE_RELAY_ENABLE == enable);
                    BEAST_EXPECT(c.TX_REDUCE_RELAY_METRICS == metrics);
                    BEAST_EXPECT(c.TX_REDUCE_RELAY_MIN_PEERS == min);
                    BEAST_EXPECT(c.TX_RELAY_PERCENTAGE == pct);
                    if (success)
                        pass();
                    else
                        fail();
                }
                catch (...)
                {
                    if (success)
                        fail();
                    else
                        pass();
                }
            };

            test(true, true, 20, 25);
            test(false, false, 20, 25);
            test(false, false, 20, 0, false);
            test(false, false, 20, 101, false);
            test(false, false, 20, 10, false);
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
            std::unique_ptr<tx_reduce_relay_test::stream_type>&& stream_ptr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  sid_,
                  slot,
                  std::move(request),
                  publicKey,
                  protocol,
                  consumer,
                  std::move(stream_ptr),
                  overlay)
        {
            sid_++;
        }
        ~PeerTest() = default;

        void
        run() override
        {
        }
        void
        send(std::shared_ptr<Message> const&) override
        {
            sendTx_++;
        }
        void
        addTxQueue(const uint256& hash) override
        {
            queueTx_++;
        }
        static void
        init()
        {
            queueTx_ = 0;
            sendTx_ = 0;
            sid_ = 0;
        }
        inline static std::size_t sid_ = 0;
        inline static std::uint16_t queueTx_ = 0;
        inline static std::uint16_t sendTx_ = 0;
    };

    std::uint16_t lid_{0};
    std::uint16_t rid_{1};
    shared_context context_;
    ProtocolVersion protocolVersion_;
    boost::beast::multi_buffer read_buf_;

public:
    tx_reduce_relay_test()
        : context_(make_SSLContext("")), protocolVersion_{1, 7}
    {
    }

private:
    void
    addPeer(
        Env& env,
        std::vector<std::shared_ptr<PeerTest>>& peers,
        std::uint16_t& nDisabled)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().overlay());
        boost::beast::http::request<boost::beast::http::dynamic_body> request;
        (nDisabled == 0) ? (void)request.insert("X-Offer-Reduce-Relay", "2")
                         : (void)nDisabled--;
        auto stream_ptr = std::make_unique<stream_type>(
            socket_type(std::forward<boost::asio::io_service&>(
                env.app().getIOService())),
            *context_);
        beast::IP::Endpoint local(
            beast::IP::Address::from_string("172.1.1." + std::to_string(lid_)));
        beast::IP::Endpoint remote(
            beast::IP::Address::from_string("172.1.1." + std::to_string(rid_)));
        PublicKey key(std::get<0>(randomKeyPair(KeyType::ed25519)));
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto slot = overlay.peerFinder().new_inbound_slot(local, remote);
        auto const peer = std::make_shared<PeerTest>(
            env.app(),
            slot,
            std::move(request),
            key,
            protocolVersion_,
            consumer,
            std::move(stream_ptr),
            overlay);
        overlay.add_active(peer);
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
        Env env(*this);
        std::vector<std::shared_ptr<PeerTest>> peers;
        env.app().config().TX_REDUCE_RELAY_ENABLE = txRREnabled;
        env.app().config().TX_REDUCE_RELAY_MIN_PEERS = minPeers;
        env.app().config().TX_RELAY_PERCENTAGE = relayPercentage;
        PeerTest::init();
        lid_ = 0;
        rid_ = 0;
        for (int i = 0; i < nPeers; i++)
            addPeer(env, peers, nDisabled);
        protocol::TMTransaction m;
        m.set_rawtransaction("transaction");
        m.set_deferred(false);
        m.set_status(protocol::TransactionStatus::tsNEW);
        env.app().overlay().relay(uint256{0}, m, toSkip);
        auto peerTest = [](std::uint16_t expectRelay,
                           std::uint16_t expectQueue) {
            return PeerTest::sendTx_ == expectRelay &&
                PeerTest::queueTx_ == expectQueue;
        };
        BEAST_EXPECT(
            (!txRREnabled && peerTest(expectRelay, expectQueue)) ||
            (txRREnabled && peerTest(expectRelay, expectQueue)));
    }

    void
    run() override
    {
        bool log = false;
        std::set<Peer::id_t> skip = {0, 1, 2, 3, 4};
        testConfig(log);
        // relay to all peers, no hash queue
        testRelay("disabled", false, 10, 0, 10, 25, 10, 0);
        // relay to all peers because min is higher than nPeers
        testRelay("enabled, low min", true, 10, 0, 20, 25, 10, 0);
        // relay to 25% of peers (0.25*60=15), queue the rest (45)
        testRelay("enabled", true, 60, 0, 20, 25, 15, 45);
        // relay to 25% of nPeers - nSkip (0.25*60-5)=10), queue the rest less
        // skip (45)
        testRelay("enabled, skip peers", true, 60, 0, 20, 25, 10, 45, skip);
        // relay to 25% of (nPeers - disalbed) + disabled, (0.25*(70-10)+10=25),
        // queue the rest (45)
        testRelay("enabled, disabled peers", true, 70, 10, 20, 25, 25, 45);
        // relay to 25% of (nPeers - disabled) - (skip - disabledInSkip) +
        // disabledNotSkip) (0.25*(70-10)-(5-5)+5=20), queue the rest less skip
        // (45)
        testRelay(
            "enabled, disabled & skip peers",
            true,
            70,
            10,
            20,
            25,
            20,
            45,
            skip);
        // no relay because toRelay is equal to skip (0.25*20-5=0), queue all
        // less skip (15)
        testRelay(
            "enabled, skip peers, no relay", true, 20, 0, 10, 25, 0, 15, skip);
    }
};

BEAST_DEFINE_TESTSUITE(tx_reduce_relay, ripple_data, ripple);
}  // namespace test
}  // namespace ripple