/** @file
 *  Defines the pure abstract interface for a single authenticated peer
 *  connection in the XRPL overlay network.
 *
 *  `PeerImp` (in `detail/`) provides the concrete async I/O implementation.
 *  All other subsystems interact exclusively through this interface, keeping
 *  them decoupled from the SSL shutdown state machine, circular buffers, and
 *  protobuf handling inside `PeerImp`.
 */

#pragma once

#include <xrpld/overlay/Message.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>

namespace xrpl {

namespace Resource {
class Charge;
}  // namespace Resource

/** Opt-in protocol capabilities that a peer may have negotiated at handshake.
 *
 *  Used by `Peer::supportsFeature` to gate message types that older peers
 *  cannot handle, ensuring backward compatibility across protocol versions.
 */
enum class ProtocolFeature {
    ValidatorListPropagation,   /**< Peer accepts v1 validator list messages. */
    ValidatorList2Propagation,  /**< Peer accepts v2 validator list messages. */
    LedgerReplay,               /**< Peer supports targeted ledger replay requests. */
};

/** Abstract interface for a single authenticated, handshake-complete peer
 *  connection in the XRPL overlay network.
 *
 *  `Overlay` manages a collection of `Peer` objects; `predicates.h` provides
 *  composable functors that filter and iterate over them. The concrete
 *  implementation `PeerImp` is never exposed outside `detail/` — callers
 *  route messages, query ledger state, and apply resource charges entirely
 *  through this interface.
 *
 *  Shared ownership via `Peer::ptr` (`std::shared_ptr<Peer>`) is the
 *  canonical handle. For long-lived references that must not extend a peer's
 *  lifetime, store a `Peer::id_t` and resolve it later with
 *  `Overlay::findPeerByShortID()`.
 */
class Peer
{
public:
    /** Shared-ownership handle; the canonical way to hold a reference to a peer. */
    using ptr = std::shared_ptr<Peer>;

    /** Stable numeric identifier for a peer connection.
     *
     *  Safe to store in tables and to outlive the `Peer` object itself.
     *  Callers can discover whether the peer is still connected by attempting
     *  a lookup via `Overlay::findPeerByShortID()`.
     */
    using id_t = std::uint32_t;

    virtual ~Peer() = default;

    //
    // Network
    //

    /** Enqueue a protocol message for delivery to this peer.
     *
     *  @param m The wire-encoded message to send.
     */
    virtual void
    send(std::shared_ptr<Message> const& m) = 0;

    /** Return the remote IP endpoint of this peer connection.
     *
     *  @return The peer's remote address and port.
     */
    [[nodiscard]] virtual beast::IP::Endpoint
    getRemoteAddress() const = 0;

    /** Flush the accumulated transaction-hash queue to this peer as a single
     *  `TMHaveTransactions` batch.
     *
     *  Only called when `txReduceRelayEnabled()` is true. Sending the queue
     *  in a batch amortizes per-message overhead compared to flooding full
     *  transactions to every peer individually.
     *
     *  @see addTxQueue
     */
    virtual void
    sendTxQueue() = 0;

    /** Accumulate a transaction hash in this peer's reduce-relay queue.
     *
     *  Instead of forwarding the full transaction immediately, the hash is
     *  batched and delivered via `sendTxQueue()`. The queue is bounded by
     *  `MAX_TX_QUEUE_SIZE` (10,000 hashes) to stay within the 64 MiB wire
     *  limit at high TPS.
     *
     *  Only meaningful when `txReduceRelayEnabled()` is true.
     *
     *  @param hash The 256-bit transaction hash to enqueue.
     *  @see sendTxQueue, removeTxQueue
     */
    virtual void
    addTxQueue(uint256 const&) = 0;

    /** Remove a transaction hash from this peer's reduce-relay queue.
     *
     *  Called when a transaction is retracted or no longer needs to be
     *  announced to this peer.
     *
     *  @param hash The 256-bit transaction hash to remove.
     *  @see addTxQueue
     */
    virtual void
    removeTxQueue(uint256 const&) = 0;

    /** Apply a resource charge against this peer's load-balance score.
     *
     *  Called when the peer imposes cost on the local node — for example, by
     *  sending malformed messages, triggering expensive validation, or causing
     *  job-queue overflow. If the accumulated charge exceeds the Drop
     *  threshold, the peer is disconnected and
     *  `Overlay::incPeerDisconnectCharges()` is incremented.
     *
     *  @param fee     The charge type and magnitude to apply.
     *  @param context Diagnostic label identifying what triggered the charge;
     *      flows into disconnect accounting logs.
     */
    virtual void
    charge(Resource::Charge const& fee, std::string const& context) = 0;

    //
    // Identity
    //

    /** Return this peer's stable numeric identifier.
     *
     *  @return The `id_t` assigned at connection activation. Safe to store
     *      in tables; see `Peer::id_t`.
     */
    [[nodiscard]] virtual id_t
    id() const = 0;

    /** Return `true` if this connection is a member of the trusted cluster.
     *
     *  Cluster peers bypass slot limits and receive preferential routing.
     */
    [[nodiscard]] virtual bool
    cluster() const = 0;

    /** Return `true` if the measured round-trip latency exceeds the high-latency
     *  threshold.
     *
     *  Used by the ledger acquisition logic to prefer lower-latency peers when
     *  selecting candidates for data requests.
     */
    [[nodiscard]] virtual bool
    isHighLatency() const = 0;

    /** Compute a priority score for use in peer-selection during data acquisition.
     *
     *  The score combines a random tiebreaker, a bonus when the peer is known
     *  to have the requested item, a penalty proportional to measured latency,
     *  and a large penalty when latency is unknown.  Higher is better.
     *
     *  @param haveItem `true` if the peer is believed to have the item being
     *      requested (e.g., via `hasLedger` or `hasTxSet`).
     *  @return A signed integer score; higher values indicate a more
     *      preferable peer.
     */
    [[nodiscard]] virtual int
    getScore(bool haveItem) const = 0;

    /** Return the node public key that authenticated this peer during the handshake.
     *
     *  @return A reference to the peer's Ed25519 or secp256k1 public key.
     */
    [[nodiscard]] virtual PublicKey const&
    getNodePublic() const = 0;

    /** Return a JSON snapshot of this peer's current state for diagnostics.
     *
     *  Used by the `/crawl` endpoint and the `peers` RPC command.
     *
     *  @return A `json::Value` object containing identity, latency, ledger
     *      range, and protocol version fields.
     */
    virtual json::Value
    json() = 0;

    /** Return `true` if this peer negotiated the given protocol feature at handshake.
     *
     *  Use this before sending a message type the peer may not understand, to
     *  maintain backward compatibility across protocol versions.
     *
     *  @param f The capability to test.
     *  @return `true` if the feature was mutually negotiated; `false` otherwise.
     *  @see ProtocolFeature
     */
    [[nodiscard]] virtual bool
    supportsFeature(ProtocolFeature f) const = 0;

    /** Return the last-seen sequence number for a validator list publisher, if any.
     *
     *  The squelch system uses this to avoid re-sending a validator list version
     *  that this peer has already propagated. Each publisher is tracked
     *  independently by its `PublicKey`.
     *
     *  @param publisherKey The validator list publisher's public key.
     *  @return The tracked sequence number, or `std::nullopt` if none has
     *      been recorded for this publisher.
     *  @see setPublisherListSequence
     */
    [[nodiscard]] virtual std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const = 0;

    /** Record the sequence number of a validator list propagated to this peer.
     *
     *  Called after successfully forwarding a validator list so that future
     *  redundant sends can be suppressed.
     *
     *  @param publisherKey The validator list publisher's public key.
     *  @param seq          The sequence number of the list that was sent.
     *  @see publisherListSequence
     */
    virtual void
    setPublisherListSequence(PublicKey const&, std::size_t const) = 0;

    /** Return a human-readable fingerprint string identifying this peer.
     *
     *  Derived from the remote endpoint, node public key, and numeric ID.
     *  Used as a log prefix so all messages from a given peer are easily
     *  correlated in log output.
     *
     *  @return A stable string for the lifetime of this connection.
     */
    [[nodiscard]] virtual std::string const&
    fingerprint() const = 0;

    //
    // Ledger
    //

    /** Return the hash of the most recent closed ledger this peer has advertised.
     *
     *  @return The peer's last reported closed-ledger hash.
     */
    [[nodiscard]] virtual uint256 const&
    getClosedLedgerHash() const = 0;

    /** Return `true` if this peer is known to have the specified ledger.
     *
     *  A hit is recorded if `seq` falls within the peer's advertised ledger
     *  range while the peer is converged, OR if `hash` appears in the recent
     *  ledger cache.
     *
     *  @param hash The ledger hash to test for.
     *  @param seq  The ledger sequence number; pass 0 to skip the range check
     *      and rely solely on the hash cache.
     *  @return `true` if the peer is believed to hold this ledger.
     */
    [[nodiscard]] virtual bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const = 0;

    /** Retrieve the ledger sequence range this peer has advertised.
     *
     *  @param minSeq Output parameter set to the peer's minimum available
     *      ledger sequence.
     *  @param maxSeq Output parameter set to the peer's maximum available
     *      ledger sequence.
     */
    virtual void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const = 0;

    /** Return `true` if this peer is known to have a specific transaction set.
     *
     *  Checked during consensus when fetching a missing SHAMap transaction set
     *  from the network.
     *
     *  @param hash The root hash of the transaction set (SHAMap).
     *  @return `true` if the hash appears in the peer's recent transaction-set
     *      cache.
     */
    [[nodiscard]] virtual bool
    hasTxSet(uint256 const& hash) const = 0;

    /** Advance the peer's closed-ledger tracking to the next consensus round.
     *
     *  Moves `closedLedgerHash_` to `previousLedgerHash_` and zeroes the
     *  current hash, signalling that a new round of status updates is expected
     *  from this peer.
     */
    virtual void
    cycleStatus() = 0;

    /** Return `true` if this peer can serve ledgers across the requested range.
     *
     *  Checks that the peer is not in the `Diverged` tracking state and that
     *  the requested range falls entirely within its advertised ledger range.
     *
     *  @param uMin The minimum ledger sequence of the requested range.
     *  @param uMax The maximum ledger sequence of the requested range.
     *  @return `true` if the peer is converged and covers `[uMin, uMax]`.
     */
    virtual bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) = 0;

    /** Return `true` if LZ4 payload compression was negotiated with this peer.
     *
     *  When `true`, `Message::getBuffer(Compressed::On)` is used so the
     *  compressed form is computed once and shared across all eligible peers.
     */
    [[nodiscard]] virtual bool
    compressionEnabled() const = 0;

    /** Return `true` if the TX reduce-relay feature was negotiated with this peer.
     *
     *  When `true`, full transactions are sent only to a quota of peers while
     *  the remainder receive hash-only announcements via `addTxQueue` /
     *  `sendTxQueue`. Falls back to flooding when `false` for compatibility
     *  with older peers.
     */
    [[nodiscard]] virtual bool
    txReduceRelayEnabled() const = 0;
};

}  // namespace xrpl
