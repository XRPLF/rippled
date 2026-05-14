/** @file
 *  Public interface for the PeerFinder subsystem.
 *
 *  This is the sole external contract for peer discovery and slot management.
 *  All other files under `src/xrpld/peerfinder/` are internal implementation
 *  details. Callers interact exclusively through `Config`, `Endpoint`, and
 *  `Manager`.
 */

#pragma once

#include <xrpld/core/Config.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/utility/PropertyStream.h>

#include <boost/asio/ip/tcp.hpp>

#include <string_view>

namespace xrpl::PeerFinder {

/** Abstract clock injected into PeerFinder for testability.
 *
 *  All time-dependent logic — Livecache TTLs, Bootcache cooldowns, and
 *  connection-retry backoff — uses this alias rather than
 *  `std::chrono::steady_clock` directly, so unit tests can substitute a
 *  controlled fake clock without touching production code paths.
 */
using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

/** A collection of peer IP addresses. */
using IPAddresses = std::vector<beast::IP::Endpoint>;

//------------------------------------------------------------------------------

/** Operational parameters for the PeerFinder subsystem.
 *
 *  Constructed with defaults and then customised either by direct field
 *  assignment or by the `makeConfig()` factory.  After all fields are set,
 *  `applyTuning()` must be called to enforce business-rule constraints (in
 *  particular the per-IP admission limit).
 *
 *  @note Fixed peers registered via `Manager::addFixedPeer` are **not**
 *      counted toward `maxPeers`; they bypass slot limits entirely.
 */
struct Config
{
    /** Maximum number of public peer slots (inbound + outbound combined).
     *
     *  Does not include fixed peers, which bypass this cap.
     *  Defaults to `Tuning::kDEFAULT_MAX_PEERS` (21).
     */
    std::size_t maxPeers{Tuning::kDEFAULT_MAX_PEERS};

    /** Target number of automatic outbound connections to maintain.
     *
     *  Outbound connections are only initiated when `autoConnect` is `true`.
     *  Initialised to `calcOutPeers()` by the default constructor; callers
     *  should re-invoke `calcOutPeers()` after changing `maxPeers`.
     */
    std::size_t outPeers;

    /** Maximum number of inbound connections to accept.
     *
     *  Inbound connections are only accepted when `wantIncoming` is `true`.
     *  Zero until explicitly set or derived by `makeConfig()`.
     */
    std::size_t inPeers{0};

    /** Whether to ask peers not to gossip this node's IP address. */
    bool peerPrivate = true;

    /** Whether this node accepts incoming peer connections. */
    bool wantIncoming{true};

    /** Whether to establish outbound connections autonomously. */
    bool autoConnect{true};

    /** TCP port on which this node listens for peer connections. */
    std::uint16_t listeningPort{0};

    /** Protocol-feature flags advertised during the peer handshake. */
    std::string features;

    /** Maximum number of simultaneous inbound connections from a single IP.
     *
     *  Zero means "unset"; `applyTuning()` will compute a value.
     *  After tuning: `max(1, min(ipLimit, inPeers / 2))` — no single IP
     *  may hold more than half of all inbound slots.
     */
    int ipLimit{0};

    //--------------------------------------------------------------------------

    /** Construct a `Config` with default values.
     *
     *  `outPeers` is initialised to `calcOutPeers()` so the outbound
     *  percentage policy is applied immediately.
     */
    Config();

    /** Compute the desired outbound peer count from `maxPeers`.
     *
     *  Applies the formula `max(maxPeers * 15% + 0.5, 10)`, where 15% is
     *  `Tuning::kOUT_PERCENT` and 10 is `Tuning::kMIN_OUT_COUNT`.  Keeping
     *  outbound connections to roughly 15% of total capacity reserves the
     *  majority of slots for inbound connections, which keeps the overlay
     *  open to new participants.
     *
     *  @return The recommended value for `outPeers`.
     */
    [[nodiscard]] std::size_t
    calcOutPeers() const;

    /** Enforce business-rule constraints on all fields.
     *
     *  If `ipLimit` is zero (unset), computes an automatic value: 2 plus
     *  up to 3 extra slots scaled by how far `inPeers` exceeds the
     *  default 21-peer maximum.  The computed value is then clamped to
     *  `max(1, min(ipLimit, inPeers / 2))` so no single IP can monopolise
     *  inbound capacity.
     *
     *  Must be called after all fields are assigned; `makeConfig()` calls
     *  it automatically.
     */
    void
    applyTuning();

    /** Serialize the configuration into a diagnostic property map. */
    void
    onWrite(beast::PropertyStream::Map& map) const;

    /** Construct a `PeerFinder::Config` from the server-level configuration.
     *
     *  Two configuration modes are supported:
     *  - **Legacy mode** (`PEERS_MAX` set, `PEERS_IN_MAX`/`PEERS_OUT_MAX`
     *    both zero): the outbound count is derived at `Tuning::kOUT_PERCENT`
     *    (15%) of `PEERS_MAX`; inbound is the remainder.
     *  - **Explicit mode** (`PEERS_IN_MAX` or `PEERS_OUT_MAX` non-zero): the
     *    two halves are taken verbatim and `maxPeers` is left at zero.
     *
     *  Two policy decisions are embedded unconditionally:
     *  - If `validationPublicKey` is `true`, `peerPrivate` is forced to
     *    `true` regardless of the operator's `PEER_PRIVATE` setting, to
     *    reduce the validator's attack surface.  This happens **after**
     *    `wantIncoming` is computed so the node still advertises inbound
     *    willingness internally ("soft" privacy).
     *  - `autoConnect` is suppressed for standalone mode and for private
     *    peers; autonomous connections would defeat the privacy goal.
     *
     *  Calls `applyTuning()` before returning.
     *
     *  @param config The server's global configuration object.
     *  @param port   The TCP port on which the server is listening for peers;
     *      zero means not listening.
     *  @param validationPublicKey `true` if a validator key is configured,
     *      which forces `peerPrivate = true`.
     *  @param ipLimit Per-IP inbound connection limit passed through to
     *      `Config::ipLimit` before `applyTuning()` clamps it.
     *  @return A fully tuned `PeerFinder::Config`.
     */
    static Config
    makeConfig(
        xrpl::Config const& config,
        std::uint16_t port,
        bool validationPublicKey,
        int ipLimit);

    friend bool
    operator==(Config const& lhs, Config const& rhs);
};

//------------------------------------------------------------------------------

/** A peer address paired with its gossip-chain hop distance.
 *
 *  `hops == 0` means the originating peer is advertising itself; each relay
 *  node increments the count before forwarding.  The constructor clamps
 *  `hops` to `Tuning::kMAX_HOPS + 1` (7) so oversized values never enter
 *  the caches.
 *
 *  `operator<` orders by `address` only, enabling deduplication in sets
 *  without regard to which relay path delivered the entry.  The Livecache
 *  uses hop depth to cycle through the overlay horizon: serving endpoints
 *  from all depths to each peer drives the network toward lower diameter.
 */
struct Endpoint
{
    Endpoint() = default;

    /** Construct an `Endpoint`, clamping `hops` to `Tuning::kMAX_HOPS + 1`.
     *
     *  @param ep   The peer's connectable IP address and port.
     *  @param hops Gossip distance from the originator; clamped to 7.
     */
    Endpoint(beast::IP::Endpoint ep, std::uint32_t hops);

    /** Gossip-chain hop distance from the originating peer. */
    std::uint32_t hops = 0;

    /** Connectable IP address and port of the peer. */
    beast::IP::Endpoint address;
};

/** Order two `Endpoint` values by their `address` field only.
 *
 *  Hop count is intentionally ignored so that sets of endpoints are
 *  deduplicated by address regardless of the relay path taken.
 */
inline bool
operator<(Endpoint const& lhs, Endpoint const& rhs)
{
    return lhs.address < rhs.address;
}

/** A collection of `Endpoint` records for address exchange. */
using Endpoints = std::vector<Endpoint>;

//------------------------------------------------------------------------------

/** Outcome of attempting to admit a connection into a slot.
 *
 *  Returned by `Manager::newInboundSlot`, `Manager::newOutboundSlot`, and
 *  `Manager::activate`.  Use `to_string()` to convert to a human-readable
 *  label for logging.
 */
enum class Result {
    /** The node is not configured to accept inbound connections. */
    InboundDisabled,

    /** A connection to this remote peer already exists. */
    DuplicatePeer,

    /** The per-IP inbound connection limit has been reached. */
    IpLimitExceeded,

    /** All peer slots are occupied. */
    Full,

    /** The connection was admitted successfully. */
    Success
};

/** Convert a `Result` to its human-readable string label.
 *
 *  Returns a `string_view` into a string literal — no heap allocation —
 *  which makes it safe to call on hot logging paths during high connection
 *  activity.
 *
 *  @param result The result code to convert.
 *  @return A non-owning view of the label string; "unknown" for any
 *      unrecognised value.
 */
inline std::string_view
to_string(Result result) noexcept
{
    switch (result)
    {
        case Result::InboundDisabled:
            return "inbound disabled";
        case Result::DuplicatePeer:
            return "peer already connected";
        case Result::IpLimitExceeded:
            return "ip limit exceeded";
        case Result::Full:
            return "slots full";
        case Result::Success:
            return "success";
    }

    return "unknown";
}

/** Manages peer discovery, slot accounting, and address-cache maintenance.
 *
 *  `Manager` is the central coordination point for the PeerFinder subsystem.
 *  It is deliberately decoupled from socket I/O: callers pass plain
 *  `beast::IP::Endpoint` values and drive the actual network calls based on
 *  the addresses the manager returns.  The concrete implementation is
 *  `ManagerImp`, instantiated by `make_Manager()`.
 *
 *  Extending `beast::PropertyStream::Source` allows the manager's internal
 *  state to be streamed into the node's diagnostic property tree without
 *  coupling to any specific monitoring backend.
 *
 *  **Lifetime**: call `start()` before issuing any other methods; call
 *  `stop()` before destruction.  The destructor aborts any pending source
 *  fetch operations; some listener callbacks may fire before it returns.
 *
 *  **Thread safety**: `setConfig()` may be called from any thread.  All
 *  other methods must be called from the single thread that owns the manager
 *  (typically the io_service thread that also drives `oncePerSecond()`).
 */
class Manager : public beast::PropertyStream::Source
{
protected:
    Manager() noexcept;

public:
    /** Destroy the manager.
     *
     *  Aborts any pending source fetch operations.  There may be some
     *  listener callbacks made before the destructor returns.
     */
    ~Manager() override = default;

    /** Apply a new configuration to the manager asynchronously.
     *
     *  The updated settings take effect on the manager's next processing
     *  cycle.  May be called from any thread at any time.
     *
     *  @param config The new configuration to apply.
     */
    virtual void
    setConfig(Config const& config) = 0;

    /** Transition the manager to the running state, synchronously. */
    virtual void
    start() = 0;

    /** Transition the manager to the stopped state, synchronously. */
    virtual void
    stop() = 0;

    /** Return the current configuration. */
    virtual Config
    config() = 0;

    /** Register a peer that must always be connected.
     *
     *  Fixed peers bypass connection limits and are attempted before the
     *  Livecache or Bootcache.  Useful for maintaining private clusters.
     *
     *  @param name      A human-readable label (from the config file) used
     *      in diagnostics.
     *  @param addresses The set of IP endpoints to try for this peer.
     */
    virtual void
    addFixedPeer(
        std::string const& name,
        std::vector<beast::IP::Endpoint> const& addresses) = 0;

    /** Register a static list of fallback addresses for bootstrapping.
     *
     *  These are used when the Livecache and Bootcache are both empty.
     *  Corresponds to the `[ips]` / `[ips_fixed]` config sections.
     *
     *  @param name    A label used in diagnostics.
     *  @param strings Raw IP-address strings; malformed entries are silently
     *      dropped.
     */
    virtual void
    addFallbackStrings(
        std::string const& name,
        std::vector<std::string> const& strings) = 0;

    /** Add a URL as a fallback location to obtain IP::Endpoint sources.
        @param name A label used for diagnostics.
    */
    /* VFALCO NOTE Unimplemented
    virtual void addFallbackURL (std::string const& name,
        std::string const& url) = 0;
    */

    //--------------------------------------------------------------------------

    /** Allocate a slot for a newly accepted inbound connection.
     *
     *  Called immediately after the TCP accept, before any handshake.
     *  The slot should subsequently be driven through `onConnected()` and
     *  `activate()`, then closed with `onClosed()` or `onFailure()`.
     *
     *  @param localEndpoint  The local address of the accepted socket.
     *  @param remoteEndpoint The remote address of the connecting peer.
     *  @return A pair of `{slot, result}`.  `slot` is `nullptr` only on a
     *      detected self-connection; otherwise it is always valid.
     *      `result` indicates whether the connection can proceed
     *      (`Success`) or why it was refused.
     */
    virtual std::pair<std::shared_ptr<Slot>, Result>
    newInboundSlot(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint const& remoteEndpoint) = 0;

    /** Allocate a slot for a newly initiated outbound connection attempt.
     *
     *  Called before the TCP connect.  The slot should subsequently be driven
     *  through `onConnected()` and `activate()`, then closed with `onClosed()`
     *  or `onFailure()`.
     *
     *  @param remoteEndpoint The address being connected to.
     *  @return A pair of `{slot, result}`.  `slot` is `nullptr` only when
     *      the connection is a duplicate; otherwise it is always valid.
     *      `result` indicates whether the attempt can proceed (`Success`) or
     *      why it was refused.
     */
    virtual std::pair<std::shared_ptr<Slot>, Result>
    newOutboundSlot(beast::IP::Endpoint const& remoteEndpoint) = 0;

    /** Process a received `mtENDPOINTS` gossip message.
     *
     *  Validates, rate-limits, and inserts the endpoints into the Livecache.
     *  Hops are incremented by 1 before storage; entries with
     *  `hops > Tuning::kMAX_HOPS` are dropped.  First-hop entries trigger
     *  an async reachability probe via the `Checker`.
     *
     *  @param slot      The slot on which the message arrived.
     *  @param endpoints The endpoint records from the message.
     */
    virtual void
    onEndpoints(
        std::shared_ptr<Slot> const& slot,
        Endpoints const& endpoints) = 0;

    /** Notify the manager that a slot's connection has been closed.
     *
     *  Must be called whenever the socket is closed, unless the socket was
     *  cancelled.  Releases the slot's resources and updates Bootcache
     *  valence.
     *
     *  @param slot The slot whose connection has ended.
     */
    virtual void
    onClosed(std::shared_ptr<Slot> const& slot) = 0;

    /** Notify the manager that an outbound connection attempt has failed.
     *
     *  Distinct from `onClosed()`: used specifically when the TCP connect
     *  or handshake phase fails rather than a graceful close of an active
     *  connection.  Penalises the peer's Bootcache valence.
     *
     *  @param slot The slot for the failed outbound attempt.
     */
    virtual void
    onFailure(std::shared_ptr<Slot> const& slot) = 0;

    /** Process redirect addresses received from a full remote peer.
     *
     *  When this node's outbound connection attempt is rejected with HTTP 503,
     *  the remote peer may supply a list of alternative addresses.  These are
     *  validated and forwarded into the Bootcache.  At most
     *  `Tuning::kMAX_REDIRECTS` (30) entries are accepted per call.
     *
     *  @param remoteAddress The peer that sent the redirect.
     *  @param eps           The alternative addresses it provided.
     */
    virtual void
    onRedirects(
        boost::asio::ip::tcp::endpoint const& remoteAddress,
        std::vector<boost::asio::ip::tcp::endpoint> const& eps) = 0;

    //--------------------------------------------------------------------------

    /** Notify the manager that an outbound TCP connect has succeeded.
     *
     *  Provides the resolved local endpoint so the manager can record which
     *  local address the connection is using.  This is called before the
     *  cryptographic handshake; the peer is not yet considered legitimate.
     *
     *  If the caller cannot retrieve the local endpoint from the socket it
     *  must call `onClosed()` instead of this method.
     *
     *  @param slot          The slot for this connection attempt.
     *  @param localEndpoint The local socket address after connect.
     *  @return `true` if the connection should proceed; `false` if the
     *      manager has determined it should be torn down (e.g. duplicate
     *      detected after connect).
     */
    virtual bool
    onConnected(
        std::shared_ptr<Slot> const& slot,
        beast::IP::Endpoint const& localEndpoint) = 0;

    /** Promote a slot to the active state after a successful handshake.
     *
     *  Called once the cryptographic handshake is complete and the peer's
     *  public key is known.  Checks for duplicate keys and reserved-peer
     *  status before marking the slot active.  Reserved and cluster
     *  connections bypass the `maxPeers` cap.
     *
     *  @param slot     The slot to activate.
     *  @param key      The peer's verified public key.
     *  @param reserved `true` if this peer appears in the reservation table,
     *      exempting it from the active-slot count.
     *  @return The admission result; `Success` on activation, or a reason
     *      code if the slot cannot be made active.
     */
    virtual Result
    activate(
        std::shared_ptr<Slot> const& slot,
        PublicKey const& key,
        bool reserved) = 0;

    /** Return addresses to offer a peer that cannot be accepted.
     *
     *  When the node is full and must reject an inbound connection, it
     *  redirects the caller to up to `Tuning::kREDIRECT_ENDPOINT_COUNT` (10)
     *  addresses from the Livecache.
     *
     *  @param slot The slot being redirected (used for deduplication).
     *  @return Up to 10 `Endpoint` records from the Livecache.
     */
    virtual std::vector<Endpoint>
    redirect(std::shared_ptr<Slot> const& slot) = 0;

    /** Return addresses that this node should attempt to connect to.
     *
     *  Consults fixed peers, the Livecache, and the Bootcache in that
     *  priority order, returning the next batch of addresses for outbound
     *  connection attempts.  Suppresses addresses recently tried via
     *  `Tuning::kRECENT_ATTEMPT_DURATION` (60 s) to avoid rapid retries.
     *
     *  @return A list of `beast::IP::Endpoint` values to connect to.
     */
    virtual std::vector<beast::IP::Endpoint>
    autoconnect() = 0;

    /** Build the endpoint batches to broadcast to all connected peers.
     *
     *  Constructs the `mtENDPOINTS` payload for each active slot using the
     *  fair `Handouts` round-robin algorithm.  The caller is responsible for
     *  transmitting the resulting messages.  Rate-limited per slot by
     *  `Tuning::kSECONDS_PER_MESSAGE` (151 s).
     *
     *  @return A vector of `{slot, endpoints}` pairs; one entry per peer
     *      that should receive an endpoint message this tick.
     */
    virtual std::vector<std::pair<std::shared_ptr<Slot>, std::vector<Endpoint>>>
    buildEndpointsForPeers() = 0;

    /** Execute all periodic maintenance tasks for one second.
     *
     *  Must be called exactly once per second by the owning timer.  Drives
     *  Livecache expiry, Bootcache write cooldown, idle-peer scanning,
     *  connection-attempt batching, and the Checker reachability loop.
     */
    virtual void
    oncePerSecond() = 0;
};

}  // namespace xrpl::PeerFinder
