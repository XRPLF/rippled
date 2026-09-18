#include <test/jtx/Env.h>
#include <test/jtx/noop.h>
#include <test/overlay/CapturePeer.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/server/Handoff.h>

#include <xrpl.pb.h>

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

    /**
     * Counts the transaction hashes queued for this peer.
     *
     * `send` is inherited, so relayed messages are counted through `sent()`.
     */
    class TxReducePeer : public CapturePeer
    {
    public:
        using CapturePeer::CapturePeer;

        void
        addTxQueue(uint256 const&) override
        {
            ++queued_;
        }

        /**
         * @return The number of transaction hashes queued for this peer.
         */
        std::size_t
        queued() const
        {
            return queued_;
        }

    private:
        std::size_t queued_{0};
    };

    /**
     * Build one peer and register it with the overlay.
     *
     * The first `nDisabled` peers are built without an `X-Protocol-Ctl`
     * header, which is what leaves tx reduce-relay disabled on them. Because
     * they are built first, they occupy the lowest connection ids, which is
     * what makes them overlap the skipped peers in `testRelay`.
     *
     * @param env        The environment owning the overlay.
     * @param builder    Supplies the connection id and remote address.
     * @param peers      Receives the peer; the overlay only holds a weak
     *                   pointer, so the caller has to keep it alive.
     * @param nDisabled  How many more peers to leave reduce-relay disabled;
     *                   decremented for each one built.
     */
    void
    addPeer(
        jtx::Env& env,
        CapturePeerBuilder& builder,
        std::vector<std::shared_ptr<TxReducePeer>>& peers,
        std::uint16_t& nDisabled)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        PublicKey const key(std::get<0>(randomKeyPair(KeyType::Ed25519)));

        bool const disabled = nDisabled > 0;
        if (disabled)
            --nDisabled;

        http_request_type request;
        if (!disabled)
            request.insert("X-Protocol-Ctl", makeFeaturesRequestHeader(false, false, true, false));

        BEAST_EXPECT(overlay.findPeerByPublicKey(key) == std::shared_ptr<PeerImp>{});
        auto const peer = builder.build<TxReducePeer>(env, key, std::move(request));
        BEAST_EXPECT(overlay.findPeerByPublicKey(key) == peer);
        peers.emplace_back(peer);
    }

    /**
     * Relay one transaction to `nPeers` peers and check the split.
     *
     * @param test       The testcase name.
     * @param txRREnabled  The `tx_enable` config value.
     * @param nPeers     How many peers to attach to the overlay.
     * @param nDisabled  How many of those peers have reduce-relay disabled.
     * @param minPeers   The `tx_min_peers` config value.
     * @param relayPercentage  The `tx_relay_percentage` config value.
     * @param expectRelay  The expected number of peers relayed to.
     * @param expectQueue  The expected number of peers queued for.
     * @param nSkip      How many of the first-built peers to skip.
     */
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
        std::size_t nSkip = 0)
    {
        testcase(test);
        jtx::Env env(*this);
        CapturePeerBuilder builder;
        std::vector<std::shared_ptr<TxReducePeer>> peers;
        // Set before building any peer: `PeerImp` decides
        // `txReduceRelayEnabled()` in its constructor, from the config and the
        // handshake header together.
        env.app().config().txReduceRelayEnable = txRREnabled;
        env.app().config().txReduceRelayMinPeers = minPeers;
        env.app().config().txRelayPercentage = relayPercentage;
        for (int i = 0; i < nPeers; i++)
            addPeer(env, builder, peers, nDisabled);

        // Bail out rather than fall through: an under-filled skip set would
        // fail the relay counts below too, for a reason that looks unrelated.
        if (!BEAST_EXPECT(nSkip <= peers.size()))
            return;

        // Skip the peers built first, so the skipped set overlaps the
        // reduce-relay-disabled peers the way the expected counts assume.
        std::set<Peer::id_t> toSkip;
        for (std::size_t i = 0; i < nSkip; ++i)
            toSkip.insert(peers[i]->id());

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

            std::size_t sendTx = 0;
            std::size_t queueTx = 0;
            for (auto const& peer : peers)
            {
                sendTx += peer->sent().size();
                queueTx += peer->queued();
            }
            BEAST_EXPECT(sendTx == expectRelay && queueTx == expectQueue);
        }
    }

    void
    run() override
    {
        bool const log = false;
        testConfig(log);
        // relay to all peers, no hash queue
        testRelay("feature disabled", false, 10, 0, 10, 25, 10, 0);
        // relay to nPeers - skip (10-5=5)
        testRelay("feature disabled & skip", false, 10, 0, 10, 25, 5, 0, 5);
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
        testRelay("skip", true, 60, 0, 20, 25, 25, 30, 5);
        // relay to minPeers + disabled + 25% of (nPeers - minPeers - disabled)
        // (20+10+0.25*(70-20-10)=40), queue the rest (30)
        testRelay("disabled", true, 70, 10, 20, 25, 40, 30);
        // relay to minPeers + disabled-not-in-skip + 25% of (nPeers - minPeers
        // - disabled) (20+5+0.25*(70-20-10)=35), queue the rest, skip counts
        // towards relayed (70-35-5=30))
        testRelay("disabled & skip", true, 70, 10, 20, 25, 35, 30, 5);
        // relay to minPeers + disabled + 25% of (nPeers - minPeers - disabled)
        // - skip (10+5+0.25*(15-10-5)-10=5), queue the rest, skip counts
        // towards relayed (15-5-10=0)
        testRelay("disabled & skip, no queue", true, 15, 5, 10, 25, 5, 0, 10);
        // relay to minPeers + disabled + 25% of (nPeers - minPeers - disabled)
        // - skip (10+2+0.25*(20-10-2)-14=0), queue the rest, skip counts
        // towards relayed (20-14=6)
        testRelay("disabled & skip, no relay", true, 20, 2, 10, 25, 0, 6, 14);
    }
};

BEAST_DEFINE_TESTSUITE(tx_reduce_relay, overlay, xrpl);
}  // namespace xrpl::test
