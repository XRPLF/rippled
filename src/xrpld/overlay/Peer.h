#pragma once

#include <xrpld/overlay/Message.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace xrpl {

namespace resource {
class Charge;
}  // namespace resource

enum class ProtocolFeature {
    LedgerReplay,
    LedgerNodeDepth,
};

/**
 * Validations one peer has delivered since it connected, split by whether the
 * signer is trusted. Peer::validationCounts() says which ones count.
 *
 * @code
 *   PeerImp::onMessage(TMValidation) --counts--> Peer::validationCounts()
 *                                                  --> peer_validation_load gauge
 * @endcode
 */
struct PeerValidationCounts
{
    /**
     * Validations whose signer is on this node's UNL.
     */
    std::uint64_t trusted = 0;

    /**
     * Validations from every other signer.
     */
    std::uint64_t untrusted = 0;
};

/**
 * Represents a peer connection in the overlay.
 */
class Peer
{
public:
    using Ptr = std::shared_ptr<Peer>;

    /**
     * Uniquely identifies a peer.
     * This can be stored in tables to find the peer later. Callers
     * can discover if the peer is no longer connected and make
     * adjustments as needed.
     */
    using ID = std::uint32_t;

    virtual ~Peer() = default;

    //
    // Network
    //

    virtual void
    send(std::shared_ptr<Message> const& m) = 0;

    [[nodiscard]] virtual beast::ip::Endpoint
    getRemoteAddress() const = 0;

    /**
     * Send aggregated transactions' hashes.
     */
    virtual void
    sendTxQueue() = 0;

    /**
     * Aggregate transaction's hash.
     */
    virtual void
    addTxQueue(UInt256 const&) = 0;

    /**
     * Remove hash from the transactions' hashes queue.
     */
    virtual void
    removeTxQueue(UInt256 const&) = 0;

    /**
     * Adjust this peer's load balance based on the type of load imposed.
     */
    virtual void
    charge(resource::Charge const& fee, std::string const& context) = 0;

    //
    // Identity
    //

    [[nodiscard]] virtual ID
    id() const = 0;

    /**
     * Returns `true` if this connection is a member of the cluster.
     */
    [[nodiscard]] virtual bool
    cluster() const = 0;

    [[nodiscard]] virtual bool
    isHighLatency() const = 0;

    [[nodiscard]] virtual int
    getScore(bool) const = 0;

    [[nodiscard]] virtual PublicKey const&
    getNodePublic() const = 0;

    virtual json::Value
    json() = 0;

    [[nodiscard]] virtual bool
    supportsFeature(ProtocolFeature f) const = 0;

    [[nodiscard]] virtual std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const = 0;

    virtual void
    setPublisherListSequence(PublicKey const&, std::size_t const) = 0;

    [[nodiscard]] virtual std::string const&
    fingerprint() const = 0;
    //
    // Ledger
    //

    [[nodiscard]] virtual UInt256
    getClosedLedgerHash() const = 0;
    [[nodiscard]] virtual bool
    hasLedger(UInt256 const& hash, std::uint32_t seq) const = 0;
    virtual void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const = 0;
    [[nodiscard]] virtual bool
    hasTxSet(UInt256 const& hash) const = 0;
    virtual void
    cycleStatus() = 0;
    virtual bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) = 0;

    [[nodiscard]] virtual bool
    compressionEnabled() const = 0;

    [[nodiscard]] virtual bool
    txReduceRelayEnabled() const = 0;

    //
    // Statistics
    //

    /**
     * Validations this peer has delivered since it connected.
     *
     * Counted once the signer's trust is known, so duplicates and untrusted
     * validations this node then drops still count.
     *
     * @return The counts. Zero for a peer type that does not count them.
     */
    [[nodiscard]] virtual PeerValidationCounts
    validationCounts() const
    {
        return {};
    }
};

}  // namespace xrpl
