#pragma once

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <tests/libxrpl/shamap/DeepChain.h>

#include <xrpl.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

// The chain builder needs only libxrpl, so it is shared with the gtest suites; see the header for
// why the protobuf reply builder below cannot be.
using tests::DeepChain;

// A smallest-possible leaf plus its 4-byte HashPrefix lands one byte short of the floor
// ConsensusTransSetSF::gotNode() parses at, so a chain's leaf is never taken for a transaction and
// resubmitted.
static_assert(
    sizeof(std::uint32_t) + DeepChain::kLeafItemBytes < ConsensusTransSetSF::kMinTxNodeBytesToParse,
    "a smallest-possible leaf must stay below the resubmission floor");

/**
 * A peer that records what it was charged, and is otherwise inert.
 *
 * One instance per packet keeps charges() unambiguous about which packet was
 * charged what.
 *
 * charges_ is unguarded: nothing on the retry timer's path charges a peer, so
 * every charge lands on the thread that fed the packet in.
 */
class ChargeRecordingPeer : public Peer
{
public:
    /**
     * @param hasTxSet What hasTxSet() reports, which is how an acquisition
     *        decides whether this peer is worth asking. Defaults to true, so a
     *        peer handed straight to takeNodes() needs no argument.
     */
    explicit ChargeRecordingPeer(bool hasTxSet = true) : id_(nextId()), hasTxSet_(hasTxSet)
    {
    }

    void
    charge(resource::Charge const& fee, std::string const& context = {}) override
    {
        charges_.push_back(fee);
    }

    [[nodiscard]] std::vector<resource::Charge> const&
    charges() const
    {
        return charges_;
    }

    [[nodiscard]] id_t
    id() const override
    {
        return id_;
    }

    [[nodiscard]] bool
    hasTxSet(uint256 const&) const override
    {
        return hasTxSet_;
    }

    // Nothing below is consulted by the paths under test.

    void
    send(std::shared_ptr<Message> const&) override
    {
    }
    [[nodiscard]] beast::ip::Endpoint
    getRemoteAddress() const override
    {
        return {};
    }
    [[nodiscard]] bool
    cluster() const override
    {
        return false;
    }
    [[nodiscard]] bool
    isHighLatency() const override
    {
        return false;
    }
    [[nodiscard]] int
    getScore(bool) const override
    {
        return 0;
    }
    [[nodiscard]] PublicKey const&
    getNodePublic() const override
    {
        // Shared across instances: nothing tells these peers apart by key, and deriving one
        // per instance runs a real Ed25519 keygen for every peer a case builds.
        static PublicKey const kNodePublicKey =
            derivePublicKey(KeyType::Ed25519, randomSecretKey());
        return kNodePublicKey;
    }
    json::Value
    json() override
    {
        return {};
    }
    [[nodiscard]] bool
    supportsFeature(ProtocolFeature) const override
    {
        return false;
    }
    [[nodiscard]] std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }
    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }
    [[nodiscard]] uint256
    getClosedLedgerHash() const override
    {
        static uint256 const kHash{};
        return kHash;
    }
    [[nodiscard]] bool
    hasLedger(uint256 const&, std::uint32_t) const override
    {
        return true;
    }
    void
    ledgerRange(std::uint32_t&, std::uint32_t&) const override
    {
    }
    void
    cycleStatus() override
    {
    }
    bool
    hasRange(std::uint32_t, std::uint32_t) override
    {
        return false;
    }
    [[nodiscard]] bool
    compressionEnabled() const override
    {
        return false;
    }
    void
    sendTxQueue() override
    {
    }
    void
    addTxQueue(uint256 const&) override
    {
    }
    void
    removeTxQueue(uint256 const&) override
    {
    }
    [[nodiscard]] bool
    txReduceRelayEnabled() const override
    {
        return false;
    }
    [[nodiscard]] std::string const&
    fingerprint() const override
    {
        static std::string const kFingerprint;
        return kFingerprint;
    }

private:
    /**
     * The next id to hand out, distinct per instance so a test with
     * several peers can tell from a recorded id which one an acquisition
     * picked.
     *
     * @return The id.
     */
    [[nodiscard]] static id_t
    nextId()
    {
        static std::atomic<id_t> next{1};
        return next++;
    }

    std::vector<resource::Charge> charges_;
    id_t id_;
    bool hasTxSet_;
};

/**
 * A peer set that counts the requests sent through it, which is what shows
 * whether an acquisition is still asking for nodes, and that offers peers to a
 * hasItem/onPeerAdded callback pair like the real one: hard-filtered by hasItem
 * (which only scores in the real peer set) and deduped by tracked id, same as
 * the real peer set.
 *
 * The retry timer drives addPeers() and sendRequest() from a job thread while
 * the test reads the results, so everything recorded here is guarded.
 */
class RequestCountingPeerSet : public PeerSet
{
public:
    /**
     * @param candidates The peers addPeers() may offer, in the order they are
     *        considered. Fixed at construction, so nothing can change them
     *        while an acquisition is running. Empty for a case that never lets
     *        addPeers() find anyone.
     */
    explicit RequestCountingPeerSet(std::vector<std::shared_ptr<Peer>> candidates = {})
        : candidates_(std::move(candidates))
    {
    }

    /**
     * Offer the candidates to the caller, the way the real peer set offers the
     * peers the overlay is tracking.
     *
     * @param limit The most peers to add, recorded so a test can check what was
     *        asked for.
     * @param hasItem Hard-filters the candidates worth asking. Selects rather than merely
     *        scores, unlike the real peer set's use of the same parameter.
     * @param onPeerAdded Called for each selected candidate.
     */
    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override
    {
        std::vector<std::shared_ptr<Peer>> selected;
        {
            std::scoped_lock const lock(mutex_);

            if (!firstLimit_)
                firstLimit_ = limit;

            for (auto const& candidate : candidates_)
            {
                if (selected.size() >= limit)
                    break;
                // Dedup by tracked id, like the real peer set: a candidate already selected by an
                // earlier call does not get offered - or its onPeerAdded rerun - again.
                if (hasItem(candidate) && addedPeers_.insert(candidate->id()).second)
                    selected.push_back(candidate);
            }
        }

        // Outside the lock: onPeerAdded() calls back into the acquisition, which sends a
        // request straight back through this object.
        for (auto const& peer : selected)
            onPeerAdded(peer);
    }

    void
    sendRequest(
        ::google::protobuf::Message const&,
        protocol::MessageType,
        std::shared_ptr<Peer> const& peer) override
    {
        std::scoped_lock const lock(mutex_);
        ++requests_;
    }

    /**
     * The ids of every peer addPeers() has selected, which is what an
     * acquisition takes for the peers it is tracking.
     *
     * Unguarded, like the real peer set's: every caller of this and of
     * addPeers() is an acquisition holding its own mtx_, so nothing can be
     * added while a caller iterates. The by-value accessor below is what the
     * test thread reads instead.
     *
     * A caveat for a case that wants a peer count rather than a set of ids:
     * InboundLedger::getPeerCount() resolves each id through
     * Overlay::findPeerByShortID(), which only knows peers that really
     * connected, so it still reports zero however many ids are returned here.
     *
     * @return The ids.
     */
    [[nodiscard]] std::set<Peer::id_t> const&
    getPeerIds() const override
    {
        return addedPeers_;
    }

    [[nodiscard]] int
    requests() const
    {
        std::scoped_lock const lock(mutex_);
        return requests_;
    }

    /**
     * The limit the first addPeers() call asked for.
     *
     * The first rather than the last, because onTimer() keeps calling
     * addPeers(1) for as long as an acquisition runs, which would overwrite
     * what init() asked for.
     */
    [[nodiscard]] std::optional<std::size_t>
    firstLimit() const
    {
        std::scoped_lock const lock(mutex_);
        return firstLimit_;
    }

    /**
     * The ids of every peer addPeers() selected, which is a set because
     * onTimer() keeps re-offering the same candidates.
     *
     * @return The ids of every peer addPeers() has selected so far.
     */
    [[nodiscard]] std::set<Peer::id_t>
    addedPeers() const
    {
        std::scoped_lock const lock(mutex_);
        return addedPeers_;
    }

private:
    std::vector<std::shared_ptr<Peer>> const candidates_;

    mutable std::mutex mutex_;
    int requests_{0};
    std::optional<std::size_t> firstLimit_;
    std::set<Peer::id_t> addedPeers_;
};

/**
 * The given nodes of a chain as a TMLedgerData, so a test can go through the
 * real dispatch rather than calling an acquisition directly.
 *
 * Not part of DeepChain itself: that header is shared with the gtest suites,
 * whose binary has neither the protobuf types nor anything to send them to.
 *
 * @param chain The chain the nodes came from, which names the reply by default.
 * @param data The nodes to include, each with its claimed position.
 * @param type The reply type, which selects which map the receiver applies it
 *        to.
 * @param ledgerHash The hash the reply claims to be about, defaulting to the
 *        chain root for a TX set. A ledger acquisition wants its header hash
 *        here instead, since the chain root is only that ledger's account hash.
 * @param ledgerSeq The sequence to name in the reply.
 * @return The reply packet.
 */
[[nodiscard]] inline std::shared_ptr<protocol::TMLedgerData>
packetFor(
    DeepChain const& chain,
    std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> const& data,
    protocol::TMLedgerInfoType type = protocol::liTS_CANDIDATE,
    std::optional<uint256> const& ledgerHash = std::nullopt,
    std::uint32_t ledgerSeq = 0)
{
    auto packet = std::make_shared<protocol::TMLedgerData>();
    auto const hash = ledgerHash.value_or(chain.rootHash.asUInt256());
    packet->set_ledgerhash(hash.data(), uint256::size());
    packet->set_ledgerseq(ledgerSeq);
    packet->set_type(type);

    for (auto const& [nodeID, node] : data)
    {
        Serializer s;
        node->serializeForWire(s);

        auto* const ledgerNode = packet->add_nodes();
        ledgerNode->set_nodedata(s.peekData().data(), s.peekData().size());

        // A leaf carries its own key, so the receiver rebuilds its position from
        // that plus a depth; an inner node has no key and needs the full ID. The two
        // fields are a oneof, so sending the wrong one is rejected outright.
        if (node->isLeaf())
        {
            ledgerNode->set_depth(nodeID.getDepth());
        }
        else
        {
            ledgerNode->set_id(nodeID.getRawString());
        }
    }

    return packet;
}

/**
 * Poll until the condition holds, or give up.
 *
 * An acquisition's own timer and the jobs it hands finished work to both
 * run on other threads, so a case cannot simply look once. The deadline
 * is generous, so a loaded machine does not turn a pass into a failure.
 *
 * @param condition What to wait for.
 * @param deadline The longest to wait.
 * @return Whether the condition held before the deadline.
 */
[[nodiscard]] inline bool
waitFor(
    std::function<bool()> const& condition,
    std::chrono::steady_clock::duration deadline = std::chrono::seconds{10})
{
    auto const giveUp = std::chrono::steady_clock::now() + deadline;
    while (std::chrono::steady_clock::now() < giveUp)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return condition();
}

/**
 * Whether a batch verdict carries exactly the given counts.
 *
 * The counts rather than get(): that string is a log format, not an API. It is
 * pinned once, in the SHAMapAddNode tests, and is what to pass BEAST_EXPECTS()
 * as the reason a check here failed. Same name and meaning as the gtest suites'
 * tallyIs(), which returns an AssertionResult instead.
 *
 * @param san The verdict to check.
 * @param good How many nodes the batch should have hooked in.
 * @param bad How many it should have rejected.
 * @param duplicate How many it should have already held.
 * @return Whether the verdict matches.
 */
[[nodiscard]] inline bool
tallyIs(SHAMapAddNode const& san, int good, int bad, int duplicate)
{
    return san.getGood() == good && san.getBad() == bad && san.getDuplicate() == duplicate;
}

}  // namespace xrpl::test
