#pragma once

#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/shamap/SHAMap.h>

#include <xrpl.pb.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace xrpl {

class Application;

/**
 * Manages the acquisition and lifetime of transaction sets.
 */

class InboundTransactions
{
public:
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

    InboundTransactions() = default;
    InboundTransactions(InboundTransactions const&) = delete;
    InboundTransactions&
    operator=(InboundTransactions const&) = delete;

    virtual ~InboundTransactions() = 0;

    /**
     * Find and return a transaction set, or nullptr if it is missing.
     *
     * Called once per peer proposal, so the same round asks for the same set
     * many times over.
     *
     * @param setHash The transaction set ID (digest of the SHAMap root node).
     * @param acquire Whether to fetch the transaction set from the network if
     * it is missing.
     * @param roundParentHash Parent-ledger hash of the consensus round making
     * this request, recorded on the fetch's trace so a fetch can be tied to the
     * round(s) that wanted it. Ignored, and may be zero, when acquire is false:
     * that path never starts a fetch.
     * @param roundLedgerSeq Sequence of the ledger that round is building.
     * Ignored, and may be zero, on the same condition.
     * @return The transaction set with ID setHash, or nullptr if it is
     * missing.
     */
    virtual std::shared_ptr<SHAMap>
    getSet(
        uint256 const& setHash,
        bool acquire,
        uint256 const& roundParentHash,
        std::uint32_t roundLedgerSeq) = 0;

    /**
     * Add a transaction set from a LedgerData message.
     *
     * @param setHash The transaction set ID (digest of the SHAMap root node).
     * @param peer The peer that sent the message.
     * @param message The LedgerData message.
     */
    virtual void
    gotData(
        uint256 const& setHash,
        std::shared_ptr<Peer> peer,
        std::shared_ptr<protocol::TMLedgerData> message) = 0;

    /**
     * Add a transaction set.
     *
     * @param setHash The transaction set ID (should match set.getHash()).
     * @param set The transaction set.
     * @param acquired Whether this transaction set was acquired from a peer,
     * or constructed by ourself during consensus.
     */
    virtual void
    giveSet(uint256 const& setHash, std::shared_ptr<SHAMap> const& set, bool acquired) = 0;

    /**
     * Informs the container if a new consensus round
     */
    virtual void
    newRound(std::uint32_t seq) = 0;

    virtual void
    stop() = 0;
};

std::unique_ptr<InboundTransactions>
makeInboundTransactions(
    Application& app,
    beast::insight::Collector::ptr const& collector,
    std::function<void(std::shared_ptr<SHAMap> const&, bool)> gotSet);

}  // namespace xrpl
