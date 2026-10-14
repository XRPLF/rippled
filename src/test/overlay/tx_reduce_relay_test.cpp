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
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/server/Handoff.h>

#include <xrpl.pb.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
            auto const test = [&](bool enable,
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
     * Counts queued transaction hashes. Relayed messages are counted through
     * the inherited `sent()`.
     */
    class TxReducePeer : public CapturePeer
    {
    public:
        using CapturePeer::CapturePeer;

        void
        addTxQueue(UInt256 const&) override
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
     * The first `nDisabled` peers get no `X-Protocol-Ctl` header, which leaves
     * tx reduce-relay disabled on them. Built first, they sit at the front of
     * `peers`, where `testRelay`'s skip set expects them.
     *
     * @param env        The environment owning the overlay.
     * @param peers      Receives the peer; the overlay holds only a weak
     *                   pointer, so the caller keeps it alive.
     * @param nDisabled  How many more peers to leave disabled; decremented
     *                   per peer built.
     */
    void
    addPeer(
        jtx::Env& env,
        std::vector<std::shared_ptr<TxReducePeer>>& peers,
        std::uint16_t& nDisabled)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        PublicKey const key(std::get<0>(randomKeyPair(KeyType::Ed25519)));

        bool const disabled = nDisabled > 0;
        if (disabled)
            --nDisabled;

        HttpRequestType request;
        if (!disabled)
            request.insert("X-Protocol-Ctl", makeFeaturesRequestHeader(false, false, true, false));

        BEAST_EXPECT(overlay.findPeerByPublicKey(key) == std::shared_ptr<PeerImp>{});
        auto const peer = makeCapturePeer<TxReducePeer>(env, key, std::move(request));
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
        std::vector<std::shared_ptr<TxReducePeer>> peers;
        // `PeerImp` decides `txReduceRelayEnabled()` in its constructor, from
        // the config and the handshake header, so set these first.
        env.app().config().txReduceRelayEnable = txRREnabled;
        env.app().config().txReduceRelayMinPeers = minPeers;
        env.app().config().txRelayPercentage = relayPercentage;
        for (int i = 0; i < nPeers; i++)
            addPeer(env, peers, nDisabled);

        // An under-filled skip set would also fail the relay counts below, for
        // a reason that looks unrelated.
        if (!BEAST_EXPECT(nSkip <= peers.size()))
            return;

        // Skip the peers built first, so the skip set overlaps the disabled
        // peers as the expected counts assume.
        std::set<Peer::ID> toSkip;
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
            env.app().getOverlay().relay(UInt256{0}, m, toSkip);

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

    /**
     * Exercises the branch that only queues, reached when relay() is handed no transaction to
     * send: a pseudo-transaction, or an explicit nullopt as here.
     *
     * A queued hash is announced later in a TMHaveTransactions, a type only a peer that negotiated
     * tx reduce-relay recognizes, so a peer without it must not be queued. The relay branch below
     * checks that; this one has to as well, since the check is only as good as its least-guarded
     * send path.
     *
     * @param test Name of the case.
     * @param txRREnabled Whether the server's tx reduce-relay is configured on.
     * @param nPeers How many peers to attach.
     * @param nDisabled How many of those did not negotiate the feature.
     * @param expectQueue How many peers are expected to be queued.
     */
    void
    testQueueOnly(
        std::string const& test,
        bool txRREnabled,
        std::uint16_t nPeers,
        std::uint16_t nDisabled,
        std::uint16_t expectQueue)
    {
        testcase(test);
        jtx::Env env(*this);
        std::vector<std::shared_ptr<TxReducePeer>> peers;
        // `PeerImp` decides `txReduceRelayEnabled()` in its constructor, from
        // the config and the handshake header, so set this first.
        env.app().config().txReduceRelayEnable = txRREnabled;
        for (int i = 0; i < nPeers; i++)
            addPeer(env, peers, nDisabled);

        env.app().getOverlay().relay(uint256{0}, std::nullopt, {});

        std::size_t sendTx = 0;
        std::size_t queueTx = 0;
        for (auto const& peer : peers)
        {
            sendTx += peer->sent().size();
            queueTx += peer->queued();
        }
        BEAST_EXPECT(sendTx == 0 && queueTx == expectQueue);
    }

    /**
     * Relay through the overload that takes the parsed STTx, and check that a
     * pseudo-transaction is queued, not sent, on both overloads.
     *
     * All peers negotiated the feature and the minimum exceeds the peer
     * count, so an ordinary transaction goes to every peer.
     *
     * @param test Name of the case.
     * @param pseudo Whether to relay a pseudo-transaction.
     * @param parsed Whether to use the overload that takes the parsed STTx.
     */
    void
    testParsed(std::string const& test, bool pseudo, bool parsed)
    {
        testcase(test);
        jtx::Env env(*this);
        std::vector<std::shared_ptr<TxReducePeer>> peers;
        env.app().config().txReduceRelayEnable = true;
        env.app().config().txReduceRelayMinPeers = 20;
        env.app().config().txRelayPercentage = 25;
        constexpr std::uint16_t kPeers = 10;
        std::uint16_t nDisabled = 0;
        for (int i = 0; i < kPeers; i++)
            addPeer(env, peers, nDisabled);

        auto const stx = pseudo ? std::make_shared<STTx const>(
                                      ttAMENDMENT,
                                      [](auto& obj) {
                                          obj.setAccountID(sfAccount, AccountID());
                                          obj.setFieldH256(sfAmendment, UInt256{1});
                                          obj.setFieldU32(sfLedgerSequence, 1);
                                      })
                                : env.jt(noop(env.master)).stx;
        if (!BEAST_EXPECT(stx))
            return;

        protocol::TMTransaction m;
        Serializer s;
        stx->add(s);
        m.set_rawtransaction(s.data(), s.size());
        m.set_deferred(false);
        m.set_status(protocol::TransactionStatus::tsNEW);
        if (parsed)
        {
            env.app().getOverlay().relay(UInt256{0}, m, {}, *stx);
        }
        else
        {
            env.app().getOverlay().relay(UInt256{0}, m, {});
        }

        std::size_t sendTx = 0;
        std::size_t queueTx = 0;
        for (auto const& peer : peers)
        {
            sendTx += peer->sent().size();
            queueTx += peer->queued();
        }
        if (pseudo)
        {
            BEAST_EXPECT(sendTx == 0 && queueTx == kPeers);
        }
        else
        {
            BEAST_EXPECT(sendTx == kPeers && queueTx == 0);
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
        // nothing to send, so nothing is queued either
        testQueueOnly("queue only, feature disabled", false, 10, 0, 0);
        // queue every peer, since all of them negotiated the feature
        testQueueOnly("queue only", true, 10, 0, 10);
        // queue the 6 that negotiated it and none of the 4 that did not
        testQueueOnly("queue only, some disabled", true, 10, 4, 6);
        // queue nobody, since no peer would understand the announcement
        testQueueOnly("queue only, all disabled", true, 10, 10, 0);
        testParsed("parsed tx", false, true);
        testParsed("parsed pseudo-tx", true, true);
        testParsed("unparsed pseudo-tx", true, false);
    }
};

BEAST_DEFINE_TESTSUITE(tx_reduce_relay, overlay, xrpl);
}  // namespace xrpl::test
