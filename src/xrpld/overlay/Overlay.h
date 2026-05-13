/** @file
 *  Defines the abstract `Overlay` interface, the single authority for the
 *  XRPL peer-to-peer mesh.
 *
 *  All subsystems that need to broadcast across or query the live peer set
 *  go through this interface.  The concrete implementation lives in
 *  `detail/OverlayImpl.h` and is created exclusively via `make_Overlay()`.
 */

#pragma once

#include <xrpld/overlay/Peer.h>

#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/json_value.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <functional>
#include <optional>

namespace boost::asio::ssl {
class context;
}  // namespace boost::asio::ssl

namespace xrpl {

/** Abstract interface for the XRP Ledger peer-to-peer overlay network.
 *
 *  Every subsystem that needs to send messages to, query, or broadcast
 *  across peers does so through this interface.  Registers itself as the
 *  `"peers"` node in the diagnostics `PropertyStream` tree so monitoring
 *  tools can walk into it without extra wiring.
 *
 *  The concrete implementation is hidden behind this interface and created
 *  exclusively via the `make_Overlay()` factory in `make_Overlay.h`.
 */
class Overlay : public beast::PropertyStream::Source
{
protected:
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;

    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             PropertyStream
    Overlay() : beast::PropertyStream::Source("peers")
    {
    }

public:
    /** Controls promotion eligibility for an incoming connection slot.
     *
     *  The promotion decision is a policy of the overlay as a whole, not
     *  of the individual connection, so the enum lives here rather than
     *  on `Peer`.
     */
    enum class Promote {
        Automatic, /**< Promote based on normal slot-availability rules. */
        Never,     /**< Never promote; treat as outbound-only slot. */
        Always     /**< Always promote regardless of slot availability. */
    };

    /** Configuration that must be known at overlay construction time.
     *
     *  Populated by `setupOverlay()` (which reads `[overlay]`, `[crawl]`,
     *  `[vl]`, and `[network_id]` config sections) and passed to the
     *  `make_Overlay()` factory.  Using a value type keeps the factory
     *  signature stable as configuration grows.
     */
    struct Setup
    {
        explicit Setup() = default;

        /** Shared TLS context reused across all peer connections. */
        std::shared_ptr<boost::asio::ssl::context> context;

        /** Server's public IP address, embedded in handshake headers for
         *  NAT diagnostics and remote-address cross-checking.
         */
        beast::IP::Address publicIp;

        /** Maximum number of simultaneous connections allowed from a single
         *  IP address.  Zero means no per-IP limit.
         */
        int ipLimit = 0;

        /** Bitmask of `CrawlOptions` flags controlling what the `/crawl`
         *  endpoint exposes (overlay topology, server info, counts, UNL).
         *  Zero disables the endpoint entirely.
         */
        std::uint32_t crawlOptions = 0;

        /** Optional numeric network identifier exchanged during the peer
         *  handshake.  A mismatch causes the connection to be rejected,
         *  preventing testnet or devnet nodes from accidentally joining
         *  mainnet.  Conventional values: 0 = mainnet, 1 = testnet,
         *  2 = devnet.  Absent means no network-ID check is performed.
         */
        std::optional<std::uint32_t> networkID;

        /** Whether to serve signed validator lists via the `/vl/` endpoint.
         *  Controlled by `[vl] enabled` in the config.
         */
        bool vlEnabled = true;
    };

    /** Snapshot of currently active peers, returned by `getActivePeers()`. */
    using PeerSequence = std::vector<std::shared_ptr<Peer>>;

    ~Overlay() override = default;

    /** Start the overlay, initiating peer discovery and accepting connections. */
    virtual void
    start()
    {
    }

    /** Shut down the overlay, closing all peer connections and stopping timers. */
    virtual void
    stop()
    {
    }

    /** Handle an inbound TLS connection that may be a peer protocol upgrade.
     *
     *  Called by the HTTP server layer when it receives an upgrade request.
     *  Ownership of the TLS stream is transferred via `bundle`; if the
     *  method returns a `Handoff` that accepts the connection, the caller
     *  must not touch the stream again.  If the method declines, the caller
     *  may treat the request as a regular HTTP request.
     *
     *  @param bundle      Owning pointer to the TLS stream for this connection.
     *  @param request     The HTTP upgrade request received on the stream.
     *  @param remoteAddress  Remote TCP endpoint of the connecting client.
     *  @return A `Handoff` whose `moved` flag indicates whether the overlay
     *      accepted the connection.
     */
    virtual Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remoteAddress) = 0;

    /** Schedule an asynchronous outbound connection to the given endpoint.
     *
     *  Returns immediately; the connection attempt proceeds on the I/O
     *  strand.  Success or failure is handled internally and reflected in
     *  the active peer count.
     *
     *  @param address  The remote endpoint to connect to.
     */
    virtual void
    connect(beast::IP::Endpoint const& address) = 0;

    /** Return the configured maximum number of active peers.
     *
     *  The gap between `limit()` and `size()` represents available peer slots.
     */
    virtual int
    limit() = 0;

    /** Return the number of peers that have completed the peer-protocol handshake.
     *
     *  Peers mid-negotiation are not counted.  Use `limit()` to determine
     *  available capacity.
     */
    [[nodiscard]] virtual std::size_t
    size() const = 0;

    /** Return a JSON diagnostics object describing all active peers.
     *
     *  @deprecated Superseded by the `PropertyStream` interface; prefer
     *      walking the `"peers"` node in the diagnostics tree.
     */
    virtual json::Value
    json() = 0;

    /** Return a point-in-time snapshot of all active peers.
     *
     *  The snapshot is taken under an internal lock and then released,
     *  so the caller iterates without holding any overlay mutex.  A peer
     *  may disconnect after the snapshot is taken; callers must handle
     *  stale `shared_ptr` values gracefully.
     *
     *  @return A vector of `shared_ptr<Peer>` for each peer that has
     *      completed the handshake at the moment of the call.
     */
    [[nodiscard]] virtual PeerSequence
    getActivePeers() const = 0;

    /** Propagate a ledger sequence number to every active peer for tracking.
     *
     *  Each peer compares `index` against its own latest validated ledger
     *  to determine whether it is converged, diverged, or unknown.
     *
     *  @param index  The latest validated ledger sequence to check against.
     */
    virtual void
    checkTracking(std::uint32_t index) = 0;

    /** Find a peer by its ephemeral short connection ID.
     *
     *  The short ID (`Peer::id_t`, a `uint32_t`) is stable only for the
     *  lifetime of the connection.
     *
     *  @param id  The ephemeral connection-local identifier.
     *  @return The matching peer, or `nullptr` if no connected peer has
     *      that ID.
     */
    [[nodiscard]] virtual std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const = 0;

    /** Find a peer by its node public key.
     *
     *  Used by the consensus and validation layers, which operate in terms
     *  of validator identity rather than connection identity.
     *
     *  @param pubKey  The node public key to look up.
     *  @return The matching peer, or `nullptr` if no connected peer has
     *      that public key.
     */
    virtual std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) = 0;

    /** Send a proposal to every active peer without deduplication.
     *
     *  Use this when the local node originates the proposal.  For forwarding
     *  a received proposal, use `relay()` instead.
     *
     *  @param m  The proposal message to broadcast.
     */
    virtual void
    broadcast(protocol::TMProposeSet& m) = 0;

    /** Send a validation to every active peer without deduplication.
     *
     *  Use this when the local node originates the validation.  For
     *  forwarding a received validation, use `relay()` instead.
     *
     *  @param m  The validation message to broadcast.
     */
    virtual void
    broadcast(protocol::TMValidation& m) = 0;

    /** Forward a received proposal to peers that have not yet seen it.
     *
     *  Consults `HashRouter` to suppress duplicate relays.  Applies the
     *  squelch/reduce-relay logic to select a subset of "source" peers per
     *  validator and temporarily silence the rest via `TMSquelch`.
     *
     *  @param m          The proposal message to relay.
     *  @param uid        Deduplication key identifying this proposal.
     *  @param validator  Public key of the validator that issued the proposal.
     *  @return The set of peer IDs that already forwarded this proposal to
     *      us, i.e., peers that already have it and should not receive it
     *      again.  Empty if `HashRouter` suppressed the relay entirely.
     */
    virtual std::set<Peer::id_t>
    relay(protocol::TMProposeSet& m, uint256 const& uid, PublicKey const& validator) = 0;

    /** Forward a received validation to peers that have not yet seen it.
     *
     *  Consults `HashRouter` to suppress duplicate relays.  Applies the
     *  squelch/reduce-relay logic to select a subset of "source" peers per
     *  validator and temporarily silence the rest via `TMSquelch`.
     *
     *  @param m          The validation message to relay.
     *  @param uid        Deduplication key identifying this validation.
     *  @param validator  Public key of the validator that issued the validation.
     *  @return The set of peer IDs that already forwarded this validation to
     *      us, i.e., peers that already have it and should not receive it
     *      again.  Empty if `HashRouter` suppressed the relay entirely.
     */
    virtual std::set<Peer::id_t>
    relay(protocol::TMValidation& m, uint256 const& uid, PublicKey const& validator) = 0;

    /** Forward a transaction to peers that have not yet seen it.
     *
     *  When tx reduce-relay is active (negotiated via `FEATURE_TXRR`), a
     *  random subset of peers receives the full message immediately; the
     *  remainder receive only a hash announcement via `TMHaveTransactions`,
     *  batched by a periodic flush.  Peers that did not negotiate the
     *  feature always receive the full message for backward compatibility.
     *
     *  @param hash    SHA-512 half hash of the transaction, used for
     *      deduplication and hash-announcement queuing.
     *  @param m       The full transaction message.  May be absent when
     *      only queuing the hash for peers that will request it later.
     *  @param toSkip  Peers that have already seen this transaction and
     *      must not receive it again.
     */
    virtual void
    relay(
        uint256 const& hash,
        std::optional<std::reference_wrapper<protocol::TMTransaction>> m,
        std::set<Peer::id_t> const& toSkip) = 0;

    /** Invoke a function on every active peer.
     *
     *  Built on `getActivePeers()`: takes a snapshot first, then iterates
     *  without holding any internal lock.  Non-virtual because the snapshot
     *  semantics are defined entirely by `getActivePeers()`.
     *
     *  @tparam Function  Callable with signature
     *      `void(std::shared_ptr<Peer> const&)`.
     *  @param f  The callable to invoke for each peer.
     */
    template <class Function>
    void
    foreach(Function f) const
    {
        for (auto const& p : getActivePeers())
            f(p);
    }

    /** Record a transaction job-queue overflow event.
     *
     *  Called when a received transaction is dropped because
     *  `JobQueue::getJobCount(JtTransaction)` exceeds `MAX_TRANSACTIONS`.
     *  The accumulated count is exposed via `getJqTransOverflow()` and
     *  reported in the `server_info` RPC response as `jq_trans_overflow`.
     */
    virtual void
    incJqTransOverflow() = 0;

    /** Return the total number of transaction job-queue overflow events.
     *
     *  @return Cumulative count since process start.
     */
    [[nodiscard]] virtual std::uint64_t
    getJqTransOverflow() const = 0;

    /** Record a peer disconnection event.
     *
     *  Called whenever a peer connection is fully torn down, regardless of
     *  cause.  Exposed via `getPeerDisconnect()` in `server_info` as
     *  `peer_disconnects`.
     */
    virtual void
    incPeerDisconnect() = 0;

    /** Return the total number of peer disconnections since process start. */
    [[nodiscard]] virtual std::uint64_t
    getPeerDisconnect() const = 0;

    /** Record a peer disconnection caused by excessive resource consumption.
     *
     *  Called by the resource charging system when a peer's charge level
     *  causes a forced disconnect.  Reported separately from the general
     *  disconnect count as `peer_disconnects_resources` in `server_info`.
     */
    virtual void
    incPeerDisconnectCharges() = 0;

    /** Return the number of resource-charge-triggered peer disconnections. */
    [[nodiscard]] virtual std::uint64_t
    getPeerDisconnectCharges() const = 0;

    /** Return the numeric network ID this server is configured for, if any.
     *
     *  The ID is exchanged during the peer handshake; a mismatch causes the
     *  connection to be rejected, preventing accidental cross-network
     *  connections.  Conventional values: 0 = mainnet, 1 = testnet,
     *  2 = devnet.
     *
     *  @return The configured network ID, or an empty optional if no ID
     *      was configured (no network-ID check is performed on handshake).
     */
    [[nodiscard]] virtual std::optional<std::uint32_t>
    networkID() const = 0;

    /** Return a JSON snapshot of tx reduce-relay rolling metrics.
     *
     *  @return JSON object with per-interval averages for transaction
     *      relay counts, hash-announcement counts, and related statistics.
     */
    [[nodiscard]] virtual json::Value
    txMetrics() const = 0;
};

}  // namespace xrpl
