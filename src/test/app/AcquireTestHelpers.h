#pragma once

#include <test/jtx/PeerStub.h>

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <tests/libxrpl/shamap/DeepChain.h>
#include <tests/libxrpl/shamap/Tally.h>

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

// Both need only libxrpl, so the gtest suites share them.
using tests::DeepChain;
using tests::tallyIs;

// A smallest-possible leaf plus its 4-byte HashPrefix stays below the floor
// ConsensusTransSetSF::gotNode() parses at.
static_assert(
    sizeof(std::uint32_t) + DeepChain::kLeafItemBytes < ConsensusTransSetSF::kMinTxNodeBytesToParse,
    "a smallest-possible leaf must stay below the resubmission floor");

/**
 * A retry interval short enough that a whole timeout chain costs a fraction of
 * a second. TimeoutCounter refuses anything at or below 10ms.
 */
inline constexpr auto kFastRetry = std::chrono::milliseconds{20};

/**
 * A peer that records what it was charged. Every other method comes from
 * PeerStub. Use one instance per packet: that is what keeps charges()
 * unambiguous about which packet was charged what. charges_ is also unguarded,
 * and every charge lands on the thread that fed the packet in.
 */
class ChargeRecordingPeer : public PeerStub
{
public:
    /**
     * @param hasTxSet What hasTxSet() reports, which is how an acquisition
     *        decides whether this peer is worth asking. Defaults to true.
     */
    explicit ChargeRecordingPeer(bool hasTxSet = true) : PeerStub(nextId()), hasTxSet_(hasTxSet)
    {
    }

    void
    charge(resource::Charge const& fee, std::string const&) override
    {
        charges_.push_back(fee);
    }

    /**
     * @return Every fee this peer has been charged, in the order charged.
     */
    [[nodiscard]] std::vector<resource::Charge> const&
    charges() const
    {
        return charges_;
    }

    // PeerStub returns false for both, and an acquisition asks the peers reporting true.

    [[nodiscard]] bool
    hasTxSet(UInt256 const&) const override
    {
        return hasTxSet_;
    }

    [[nodiscard]] bool
    hasLedger(UInt256 const&, std::uint32_t) const override
    {
        return true;
    }

private:
    /**
     * The next id to hand out, distinct per instance because
     * RequestCountingPeerSet dedups by tracked id and PeerStub's own default is
     * zero for every instance.
     *
     * @return The id.
     */
    [[nodiscard]] static ID
    nextId()
    {
        static std::atomic<ID> next{1};
        return next++;
    }

    std::vector<resource::Charge> charges_;
    bool hasTxSet_;
};

/**
 * A peer set that counts the requests an acquisition makes through it. Offers
 * peers to a hasItem/onPeerAdded callback pair, hard-filtered by hasItem (which
 * only scores in the real peer set) and deduped by tracked id.
 *
 * A count is a call, not a delivery: a request naming no peer reaches nobody
 * while this set tracks none. requests() and broadcasts() are counted apart.
 *
 * Every write here is guarded, since the retry timer drives addPeers() and
 * sendRequest() from a job thread while the test reads the results.
 */
class RequestCountingPeerSet : public PeerSet
{
public:
    /**
     * @param candidates The peers addPeers() may offer, in the order they are
     *        considered. Fixed at construction. Empty offers no one.
     */
    explicit RequestCountingPeerSet(std::vector<std::shared_ptr<Peer>> candidates = {})
        : candidates_(std::move(candidates))
    {
    }

    /**
     * Offer the candidates to the caller, the way the real peer set offers the
     * peers the overlay is tracking.
     *
     * @param limit The most peers to add, recorded for firstLimit().
     * @param hasItem Hard-filters the candidates worth asking, where the real
     *        peer set only scores with it.
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
                // Dedup by tracked id, like the real peer set: onPeerAdded runs once per candidate.
                if (hasItem(candidate) && addedPeers_.insert(candidate->id()).second)
                    selected.push_back(candidate);
            }
        }

        // Outside the lock: onPeerAdded() reenters this object through sendRequest().
        for (auto const& peer : selected)
            onPeerAdded(peer);
    }

    /**
     * Record the request, and record it in the broadcast count as well when it
     * names no peer.
     *
     * @param peer The peer to ask, or null to ask every tracked peer.
     */
    void
    sendRequest(
        ::google::protobuf::Message const&,
        protocol::MessageType,
        std::shared_ptr<Peer> const& peer) override
    {
        std::scoped_lock const lock(mutex_);
        ++requests_;
        if (!peer)
            ++broadcasts_;
    }

    /**
     * The ids of every peer addPeers() has selected, which is what an
     * acquisition takes for the peers it is tracking.
     *
     * Unguarded: every caller of this and of addPeers() is an acquisition
     * holding its own mtx_, so the ids stay fixed while a caller iterates. A
     * test thread reads addedPeers() instead.
     * InboundLedger::getPeerCount() reports zero for these, since it resolves
     * ids through the overlay, while these peers live only in this harness.
     *
     * @return The ids.
     */
    [[nodiscard]] std::set<Peer::ID> const&
    getPeerIds() const override
    {
        return addedPeers_;
    }

    /**
     * @return How many requests have been sent through this peer set, counting
     *         a broadcast as one.
     */
    [[nodiscard]] int
    requests() const
    {
        std::scoped_lock const lock(mutex_);
        return requests_;
    }

    /**
     * How many of those requests carried no peer of their own.
     *
     * @return The count.
     */
    [[nodiscard]] int
    broadcasts() const
    {
        std::scoped_lock const lock(mutex_);
        return broadcasts_;
    }

    /**
     * The limit the first addPeers() call asked for, which is init()'s, since
     * onTimer() keeps calling addPeers(1) for as long as an acquisition runs.
     *
     * @return The limit, or nullopt if addPeers() has not been called.
     */
    [[nodiscard]] std::optional<std::size_t>
    firstLimit() const
    {
        std::scoped_lock const lock(mutex_);
        return firstLimit_;
    }

    /**
     * A set, because onTimer() keeps re-offering the same candidates.
     *
     * @return The ids of every peer addPeers() has selected so far.
     */
    [[nodiscard]] std::set<Peer::ID>
    addedPeers() const
    {
        std::scoped_lock const lock(mutex_);
        return addedPeers_;
    }

private:
    std::vector<std::shared_ptr<Peer>> const candidates_;

    mutable std::mutex mutex_;
    int requests_{0};
    int broadcasts_{0};
    std::optional<std::size_t> firstLimit_;
    std::set<Peer::ID> addedPeers_;
};

/**
 * The given nodes of a chain as a TMLedgerData, so a test can go through the
 * real dispatch. Not in DeepChain, since the protobuf types are xrpld and that
 * header is shared with the libxrpl-only gtest binary.
 *
 * @param chain The chain the nodes came from, which names the reply by default.
 * @param data The nodes to include, each with its claimed position.
 * @param type The reply type, which selects which map the receiver applies it
 *        to.
 * @param ledgerHash The hash the reply claims to be about, defaulting to the
 *        chain root for a TX set. A ledger acquisition wants its header hash
 *        here, since the chain root is only that ledger's account hash.
 * @param ledgerSeq The sequence to name in the reply.
 * @return The reply packet.
 */
[[nodiscard]] inline std::shared_ptr<protocol::TMLedgerData>
packetFor(
    DeepChain const& chain,
    std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> const& data,
    protocol::TMLedgerInfoType type = protocol::liTS_CANDIDATE,
    std::optional<UInt256> const& ledgerHash = std::nullopt,
    std::uint32_t ledgerSeq = 0)
{
    auto packet = std::make_shared<protocol::TMLedgerData>();
    auto const hash = ledgerHash.value_or(chain.rootHash.asUInt256());
    packet->set_ledgerhash(hash.data(), UInt256::size());
    packet->set_ledgerseq(ledgerSeq);
    packet->set_type(type);

    for (auto const& [nodeID, node] : data)
    {
        Serializer s;
        node->serializeForWire(s);

        auto* const ledgerNode = packet->add_nodes();
        ledgerNode->set_nodedata(s.peekData().data(), s.peekData().size());

        // A leaf carries its own key, so the receiver rebuilds its position from that plus a
        // depth. An inner node has no key and needs the full ID. The two fields are a oneof.
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
 * Poll until the condition holds, or give up. An acquisition's own timer and
 * the jobs it hands finished work to both run on other threads.
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

}  // namespace xrpl::test
