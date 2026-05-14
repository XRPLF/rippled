/** @file
 *  Concrete implementation of the XRPL peer-to-peer overlay manager.
 *
 *  `OverlayImpl` implements the `Overlay` interface and the
 *  `reduce_relay::SquelchHandler` callback interface.  It owns the full
 *  peer lifecycle — from TCP hand-off through TLS/HTTP handshake, protocol
 *  activation, message dispatch, and graceful teardown — as well as the
 *  validator reduce-relay squelch system, traffic accounting, manifest
 *  caching, and the HTTP crawl/health/validator-list endpoints.
 *
 *  @see Overlay.h for the public API surface.
 *  @see Slot.h for the squelch state machine.
 */

#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/Slot.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/TrafficCount.h>
#include <xrpld/overlay/detail/TxMetrics.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Resolver.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/container/flat_map.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace xrpl {

class PeerImp;
class BasicConfig;

/** Concrete implementation of the XRPL peer-to-peer overlay network.
 *
 *  Inherits the public `Overlay` interface consumed by the rest of the
 *  application and `reduce_relay::SquelchHandler`, which the squelch
 *  subsystem calls back into to dispatch `TMSquelch` protocol messages.
 *
 *  Two complementary peer maps are maintained: `peers_` (keyed by
 *  `PeerFinder::Slot`) is populated at handshake start; `ids_` (keyed by
 *  ephemeral `Peer::id_t`) is populated only after the full protocol
 *  handshake completes in `activate()`.  Both hold `weak_ptr` to avoid
 *  ownership cycles with `PeerImp`.
 *
 *  Thread-safety: `mutex_` (recursive — acknowledged tech debt) guards both
 *  maps.  Hot-path counters (`jqTransOverflow_`, `peerDisconnects_`,
 *  `peerDisconnectsCharges_`, `next_id_`) are `std::atomic` and need no lock.
 *  Squelch/metrics work is dispatched to `strand_`; stats collection is
 *  separately serialized by `statsMutex_`.
 */
class OverlayImpl : public Overlay, public reduce_relay::SquelchHandler
{
public:
    /** Base class for objects whose lifetimes are managed by `OverlayImpl`.
     *
     *  A `Child` holds a reference to the overlay and registers itself in the
     *  overlay's `list_` map on construction.  When the overlay shuts down,
     *  `stopChildren()` iterates `list_`, calls `stop()` on each live child,
     *  and waits on `cond_` until all children have destructed.  `Timer` is
     *  the only `Child` used in normal operation.
     */
    class Child
    {
    protected:
        OverlayImpl& overlay_; /**< The owning overlay; valid for the child's lifetime. */

        explicit Child(OverlayImpl& overlay);

    public:
        virtual ~Child();

        /** Begin graceful shutdown; must return quickly and not block. */
        virtual void
        stop() = 0;
    };

private:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    /** Periodic housekeeping timer for the overlay.
     *
     *  Registered as a `Child` so the overlay's shutdown sequence can stop
     *  it cleanly.  Inherits `enable_shared_from_this` so the async-wait
     *  callback can safely extend the timer's lifetime past its last
     *  external reference.
     *
     *  Each tick drives `autoConnect()`, `sendEndpoints()`, `sendTxQueue()`,
     *  and `deleteIdlePeers()`, then reschedules itself.  The `stopping`
     *  flag prevents rearming after `stop()` is called.
     */
    struct Timer : Child, std::enable_shared_from_this<Timer>
    {
        boost::asio::basic_waitable_timer<clock_type> timer; /**< Underlying Asio timer. */
        bool stopping{false}; /**< Set by `stop()` to prevent rearming after shutdown. */

        explicit Timer(OverlayImpl& overlay);

        /** Cancel the pending async wait and suppress any subsequent rearm. */
        void
        stop() override;

        /** Schedule the next async wait on the overlay's strand. */
        void
        asyncWait();

        /** Async-wait completion handler; drives periodic housekeeping.
         *
         *  @param ec  Error code; `operation_aborted` means the timer was cancelled.
         */
        void
        onTimer(error_code ec);
    };

    Application& app_;
    boost::asio::io_context& io_context_;

    /** Keeps `io_context_` alive while the overlay is running.
     *  Reset during `stop()` to let the context drain and exit.
     */
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;

    /** Strand serializing timer callbacks, squelch updates, and tx metrics. */
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    /** Guards `peers_`, `ids_`, and child `list_`.
     *  Recursive to accommodate callbacks that re-enter overlay methods
     *  during handshake processing.  @note Acknowledged tech debt: ideally
     *  `std::mutex`. */
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex

    /** Signalled during shutdown when the child list empties. */
    std::condition_variable_any cond_;

    /** Weak reference to the single running `Timer` child. */
    std::weak_ptr<Timer> timer_;

    /** Registered `Child` objects, keyed by raw pointer for O(1) removal.
     *  `flat_map` chosen for cache-friendly iteration over a small set.
     */
    boost::container::flat_map<Child*, std::weak_ptr<Child>> list_;

    Setup setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& resourceManager_;

    /** Peer-discovery and slot-accounting subsystem. */
    std::unique_ptr<PeerFinder::Manager> peerFinder_;

    /** Per-category byte and message counters; all increments are lock-free. */
    TrafficCount traffic_;

    /** Slot-keyed peer map; populated at handshake start, before activation. */
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImp>> peers_;

    /** ID-keyed peer map; populated at `activate()` after the full handshake. */
    hash_map<Peer::id_t, std::weak_ptr<PeerImp>> ids_;

    Resolver& resolver_;

    /** Monotonically increasing source for ephemeral short peer IDs. */
    std::atomic<Peer::id_t> next_id_;

    /** Tick counter used to modulo-schedule infrequent per-timer tasks. */
    int timer_count_{0};

    /** Cumulative count of transactions dropped due to job-queue overflow. */
    std::atomic<uint64_t> jqTransOverflow_{0};

    /** Cumulative count of peer disconnections from any cause. */
    std::atomic<uint64_t> peerDisconnects_{0};

    /** Cumulative count of peer disconnections triggered by resource charges. */
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    /** Per-validator peer-selection state machines for the reduce-relay system. */
    reduce_relay::Slots<UptimeClock> slots_;

    /** Rolling statistics for the transaction reduce-relay feature.
     *  Strand-confined; written only via `addTxMetrics()`.
     */
    metrics::TxMetrics txMetrics_;

    /** Cached serialized `TMManifests` message sent to newly connected peers.
     *  Invalidated when `manifestListSeq_` no longer matches the current
     *  manifest sequence.  Protected by `manifestLock_`.
     */
    std::shared_ptr<Message> manifestMessage_;

    /** Manifest sequence at which `manifestMessage_` was last built.
     *  `nullopt` forces a rebuild on the next call to `getManifestsMessage()`.
     */
    std::optional<std::uint32_t> manifestListSeq_;

    /** Protects `manifestMessage_` and `manifestListSeq_`.
     *  Separate from `mutex_` to avoid serializing manifest caching with
     *  peer-map operations.
     */
    std::mutex manifestLock_;

    //--------------------------------------------------------------------------

public:
    /** Construct the overlay manager and initialise all subsystems.
     *
     *  Builds the `PeerFinder`, traffic gauge map, and `Stats` hook.
     *  The overlay is not yet active; call `start()` to begin accepting
     *  connections and initiating outbound connections.
     *
     *  @param app             The application context.
     *  @param setup           Immutable configuration (TLS context, crawl
     *      options, network ID, etc.) produced by `setupOverlay()`.
     *  @param serverHandler   HTTP server layer that hands off upgrade requests.
     *  @param resourceManager Tracks per-peer resource consumption.
     *  @param resolver        DNS resolution service for outbound connections.
     *  @param ioContext       The shared I/O executor.
     *  @param config          Raw config used to initialise `PeerFinder`.
     *  @param collector       Metrics collector for traffic gauges and
     *      disconnect counters.
     */
    OverlayImpl(
        Application& app,
        Setup setup,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_context& ioContext,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    OverlayImpl(OverlayImpl const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl const&) = delete;

    void
    start() override;

    void
    stop() override;

    /** Return the peer-discovery and slot-accounting subsystem. */
    PeerFinder::Manager&
    peerFinder()
    {
        return *peerFinder_;
    }

    /** Return the resource manager used to track per-peer consumption. */
    Resource::Manager&
    resourceManager()
    {
        return resourceManager_;
    }

    /** Return the immutable overlay configuration. */
    Setup const&
    setup() const
    {
        return setup_;
    }

    Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        endpoint_type remoteEndpoint) override;

    void
    connect(beast::IP::Endpoint const& remoteEndpoint) override;

    int
    limit() override;

    std::size_t
    size() const override;

    json::Value
    json() override;

    PeerSequence
    getActivePeers() const override;

    /** Get active peers excluding peers in toSkip.
       @param toSkip peers to skip
       @param active a number of active peers
       @param disabled a number of peers with tx reduce-relay
           feature disabled
       @param enabledInSkip a number of peers with tx reduce-relay
           feature enabled and in toSkip
       @return active peers less peers in toSkip
     */
    PeerSequence
    getActivePeers(
        std::set<Peer::id_t> const& toSkip,
        std::size_t& active,
        std::size_t& disabled,
        std::size_t& enabledInSkip) const;

    void
    checkTracking(std::uint32_t) override;

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    void
    broadcast(protocol::TMProposeSet& m) override;

    void
    broadcast(protocol::TMValidation& m) override;

    std::set<Peer::id_t>
    relay(protocol::TMProposeSet& m, uint256 const& uid, PublicKey const& validator) override;

    std::set<Peer::id_t>
    relay(protocol::TMValidation& m, uint256 const& uid, PublicKey const& validator) override;

    void
    relay(
        uint256 const&,
        std::optional<std::reference_wrapper<protocol::TMTransaction>> m,
        std::set<Peer::id_t> const& skip) override;

    /** Return the cached serialized `TMManifests` message for new peers.
     *
     *  Rebuilds the message if the current manifest sequence differs from
     *  `manifestListSeq_`.  Protected by `manifestLock_`.
     *
     *  @return A shared `Message` ready to be enqueued on a new peer's send
     *      queue.
     */
    std::shared_ptr<Message>
    getManifestsMessage();

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    /** Register a fully-connected outbound peer in both `peers_` and `ids_`.
     *
     *  Called by `ConnectAttempt` after a successful HTTP 101 response.
     *  Assigns the peer's short ID from `next_id_`, inserts it into `ids_`,
     *  notifies `PeerFinder` of activation, and calls `peer->run()`.
     *
     *  @param peer The newly connected `PeerImp`.
     */
    void
    addActive(std::shared_ptr<PeerImp> const& peer);

    /** Remove a `PeerFinder::Slot` from the `peers_` map.
     *
     *  Called when the slot is released — either during normal teardown or
     *  when a `ConnectAttempt` fails before producing a `PeerImp`.
     *
     *  @param slot The slot to erase from `peers_`.
     */
    void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot);

    /** Transition a peer from the handshake-complete state to fully active.
     *
     *  Called after the peer protocol handshake finishes and both the remote
     *  address and node public key are known.  Assigns a short ID, inserts
     *  the peer into `ids_`, and sends it the current manifest list.
     *
     *  @param peer The `PeerImp` to activate.
     */
    void
    activate(std::shared_ptr<PeerImp> const& peer);

    /** Remove a peer from `ids_` when it disconnects.
     *
     *  Called on the peer's strand during teardown.  The matching slot
     *  removal happens separately via `remove(slot)`.
     *
     *  @param id The short ID of the peer being deactivated.
     */
    void
    onPeerDeactivate(Peer::id_t id);

    /** Invoke a callable on every activated peer without holding the mutex.
     *
     *  Acquires `mutex_` only long enough to snapshot `ids_` into a local
     *  `weak_ptr` vector, then releases it before iterating.  This prevents
     *  deadlock if the callable tries to re-enter overlay methods, and
     *  prevents iterator invalidation from concurrent peer destruction.
     *
     *  @tparam UnaryFunc  Callable with signature
     *      `void(std::shared_ptr<PeerImp>&&)`.
     *  @param f  The callable to invoke for each live peer.
     */
    template <class UnaryFunc>
    void
    forEach(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImp>> wp;
        {
            std::scoped_lock const lock(mutex_);

            wp.reserve(ids_.size());

            for (auto& x : ids_)
                wp.push_back(x.second);
        }

        for (auto& w : wp)
        {
            if (auto p = w.lock())
                f(std::move(p));
        }
    }

    /** Process a `TMManifests` message received from a peer.
     *
     *  Forwards each manifest to the manifest cache; logs and discards
     *  malformed entries.
     *
     *  @param m     The protobuf manifests message.
     *  @param from  The peer that sent the message.
     */
    void
    onManifests(
        std::shared_ptr<protocol::TMManifests> const& m,
        std::shared_ptr<PeerImp> const& from);

    /** Return true if the HTTP request is a peer-protocol upgrade request.
     *
     *  Checks for HTTP/1.1 GET with a `Connection: upgrade` header,
     *  consistent with the XRPL peer handshake protocol.
     *
     *  @param request The inbound HTTP request to inspect.
     *  @return `true` if the request should be handled as a peer upgrade.
     */
    static bool
    isPeerUpgrade(http_request_type const& request);

    /** Return true if the HTTP response indicates a successful peer upgrade.
     *
     *  @tparam Body  HTTP body type (deduced).
     *  @param response The HTTP response to inspect.
     *  @return `true` if the status is `101 Switching Protocols` and the
     *      response has the upgrade connection header.
     */
    template <class Body>
    static bool
    isPeerUpgrade(boost::beast::http::response<Body> const& response)
    {
        if (!isUpgrade(response))
            return false;
        return response.result() == boost::beast::http::status::switching_protocols;
    }

    /** Return true if an HTTP request header has the upgrade connection token.
     *
     *  Overload for request headers (HTTP method must be GET, version >= 1.1).
     *
     *  @tparam Fields HTTP fields type (deduced).
     *  @param req  The request header to inspect.
     *  @return `true` if `Connection` contains the `"upgrade"` token.
     */
    template <class Fields>
    static bool
    isUpgrade(boost::beast::http::header<true, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (req.method() != boost::beast::http::verb::get)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists("upgrade"))
            return false;
        return true;
    }

    /** Return true if an HTTP response header has the upgrade connection token.
     *
     *  Overload for response headers (no method check; version >= 1.1).
     *
     *  @tparam Fields HTTP fields type (deduced).
     *  @param req  The response header to inspect.
     *  @return `true` if `Connection` contains the `"upgrade"` token.
     */
    template <class Fields>
    static bool
    isUpgrade(boost::beast::http::header<false, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists("upgrade"))
            return false;
        return true;
    }

    /** Build a log-prefix string for a peer short ID (e.g. `"[003] "`).
     *
     *  @param id The ephemeral peer short ID.
     *  @return A fixed-width bracketed prefix string used in peer log lines.
     */
    static std::string
    makePrefix(std::uint32_t id);

    /** Record inbound bytes for a given traffic category.
     *
     *  Lock-free; safe to call from any thread.
     *
     *  @param cat   The protocol message category.
     *  @param bytes Number of bytes received.
     */
    void
    reportInboundTraffic(TrafficCount::Category cat, int bytes);

    /** Record outbound bytes for a given traffic category.
     *
     *  Lock-free; safe to call from any thread.
     *
     *  @param cat   The protocol message category.
     *  @param bytes Number of bytes sent.
     */
    void
    reportOutboundTraffic(TrafficCount::Category cat, int bytes);

    void
    incJqTransOverflow() override
    {
        ++jqTransOverflow_;
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return jqTransOverflow_;
    }

    void
    incPeerDisconnect() override
    {
        ++peerDisconnects_;
    }

    std::uint64_t
    getPeerDisconnect() const override
    {
        return peerDisconnects_;
    }

    void
    incPeerDisconnectCharges() override
    {
        ++peerDisconnectsCharges_;
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return peerDisconnectsCharges_;
    }

    std::optional<std::uint32_t>
    networkID() const override
    {
        return setup_.networkID;
    }

    /** Update the reduce-relay slot for a validator and squelch peers if needed.
     *
     *  Forwards `key`, `validator`, and all peer IDs in `peers` into the
     *  `Slots` state machine.  When enough peers have individually crossed
     *  `MAX_MESSAGE_THRESHOLD`, `Slots` selects a fixed relay subset and
     *  calls back into `squelch()` to send `TMSquelch` to the rest.
     *
     *  @param key       Unique hash identifying this protocol message instance.
     *  @param validator Public key of the validator that produced the message.
     *  @param peers     IDs of all peers that forwarded this message; ownership
     *      transferred to the state machine.
     *  @param type      Protocol message type (e.g. `mtVALIDATION`).
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        std::set<Peer::id_t>&& peers,
        protocol::MessageType type);

    /** Single-peer overload of `updateSlotAndSquelch` to avoid a heap allocation.
     *
     *  Equivalent to the set overload with a one-element set, but avoids
     *  constructing a `std::set` in the common case where only one peer
     *  forwarded the message.
     *
     *  @param key       Unique hash identifying this protocol message instance.
     *  @param validator Public key of the validator that produced the message.
     *  @param peer      ID of the single peer that forwarded the message.
     *  @param type      Protocol message type.
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t peer,
        protocol::MessageType type);

    /** Notify the reduce-relay subsystem that a peer has been destroyed.
     *
     *  If the peer was designated as a relay source for any validator, the
     *  corresponding squelched peers are unsquelched so message flow resumes
     *  and the slot returns to the counting state.
     *
     *  @param id Short ID of the peer that is being torn down.
     */
    void
    deletePeer(Peer::id_t id);

    json::Value
    txMetrics() const override
    {
        return txMetrics_.json();
    }

    /** Record transaction reduce-relay metrics, posting to the strand if needed.
     *
     *  If the caller is not already on `strand_`, the call is posted to it so
     *  that `TxMetrics` remains strand-confined without requiring a per-call
     *  mutex.
     *
     *  @tparam Args  Parameter types forwarded to `TxMetrics::addMetrics`.
     *  @param args   Metric values to record.
     */
    template <typename... Args>
    void
    addTxMetrics(Args... args)
    {
        if (!strand_.running_in_this_thread())
            return post(strand_, std::bind(&OverlayImpl::addTxMetrics<Args...>, this, args...));

        txMetrics_.addMetrics(args...);
    }

private:
    /** Send a `TMSquelch` message to a peer, instructing it to stop relaying
     *  messages from the given validator for the specified duration.
     *
     *  Implements `reduce_relay::SquelchHandler::squelch`.  Looks up the peer
     *  by `id` in `ids_` and enqueues the protocol message on its send queue.
     *
     *  @param validator      Public key of the validator whose messages should
     *      be suppressed.
     *  @param id             Short ID of the peer to squelch.
     *  @param squelchDuration Suppression window in seconds.
     */
    void
    squelch(PublicKey const& validator, Peer::id_t const id, std::uint32_t squelchDuration)
        const override;

    /** Send a `TMSquelch` clear message to a peer, lifting a prior squelch.
     *
     *  Implements `reduce_relay::SquelchHandler::unsquelch`.  Called when the
     *  previously selected relay peer disconnects, restoring relay coverage
     *  for the affected validator.
     *
     *  @param validator  Public key of the validator to unsquelch.
     *  @param id         Short ID of the peer to unsquelch.
     */
    void
    unsquelch(PublicKey const& validator, Peer::id_t id) const override;

    /** Build an HTTP redirect response directing a peer to alternative endpoints.
     *
     *  Sent with HTTP 503 when the node is at capacity.  The response body
     *  contains a JSON `peer-ips` array of alternate addresses sourced from
     *  `PeerFinder`.
     *
     *  @param slot          The PeerFinder slot for the rejected connection.
     *  @param request       The HTTP request from the connecting peer.
     *  @param remoteAddress The remote TCP address (for logging).
     *  @return A `Writer` that streams the HTTP 503 response.
     */
    std::shared_ptr<Writer>
    makeRedirectResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remoteAddress);

    /** Build a generic HTTP error response.
     *
     *  @param slot          The PeerFinder slot for the rejected connection.
     *  @param request       The HTTP request that triggered the error.
     *  @param remoteAddress The remote TCP address (for logging).
     *  @param msg           Human-readable error description included in the body.
     *  @return A `Writer` that streams the HTTP error response.
     */
    static std::shared_ptr<Writer>
    makeErrorResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remoteAddress,
        std::string msg);

    /** Handle an HTTP `/crawl` request, populating `handoff` with the response.
     *
     *  Composes JSON from `getOverlayInfo()`, `getServerInfo()`,
     *  `getServerCounts()`, and `getUnlInfo()` according to the bitmask in
     *  `setup_.crawlOptions`.  Gated by `CrawlOptions::Overlay`.
     *
     *  @param req      The inbound HTTP request.
     *  @param handoff  Output parameter populated with the HTTP response writer.
     *  @return `true` if the URL matched `/crawl` and the response was written.
     */
    bool
    processCrawl(http_request_type const& req, Handoff& handoff);

    /** Handle an HTTP `/vl/<pubkey>` or `/vl/<version>/<pubkey>` request.
     *
     *  Returns the signed validator list this node holds for the given
     *  public key, if the key is trusted.  Gated by `setup_.vlEnabled`.
     *
     *  @param req      The inbound HTTP request.
     *  @param handoff  Output parameter populated with the HTTP response writer.
     *  @return `true` if the URL matched the `/vl/` prefix and the response
     *      was written (including 404 for unknown keys).
     */
    bool
    processValidatorList(http_request_type const& req, Handoff& handoff);

    /** Handle an HTTP `/health` request, encoding node health as the HTTP status.
     *
     *  Returns 200 (healthy), 503 (degraded), or 500 (critical) so that
     *  load-balancers can act on the status code without parsing JSON.
     *
     *  @param req      The inbound HTTP request.
     *  @param handoff  Output parameter populated with the HTTP response writer.
     *  @return `true` if the URL matched `/health` and the response was written.
     */
    bool
    processHealth(http_request_type const& req, Handoff& handoff);

    /** Dispatch a non-peer-upgrade HTTP request to the appropriate handler.
     *
     *  Tries `processCrawl`, `processValidatorList`, and `processHealth` in
     *  order.  If none matches, returns `false` and the caller may treat the
     *  request as a regular HTTP request.
     *
     *  @param req      The inbound HTTP request.
     *  @param handoff  Output parameter populated if the request is handled.
     *  @return `true` if any handler claimed the request.
     */
    bool
    processRequest(http_request_type const& req, Handoff& handoff);

    /** Build the `/crawl` overlay section: peer addresses, public keys, latency.
     *
     *  Controlled by `[crawl] overlay=1` in config.
     *
     *  @return JSON array of peer descriptors.
     */
    json::Value
    getOverlayInfo() const;

    /** Build the `/crawl` server section: version, uptime, ledger state.
     *
     *  Controlled by `[crawl] server=1` in config.
     *
     *  @return JSON object with local server information.
     */
    json::Value
    getServerInfo();

    /** Build the `/crawl` counts section: traffic and performance metrics.
     *
     *  Controlled by `[crawl] counts=1` in config.
     *
     *  @return JSON object with performance counter values.
     */
    json::Value
    getServerCounts();

    /** Build the `/crawl` UNL section: the node's trusted validator keys.
     *
     *  Controlled by `[crawl] unl=1` in config.
     *
     *  @return JSON array of trusted validator public keys.
     */
    json::Value
    getUnlInfo();

    //--------------------------------------------------------------------------

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

    /** Remove a `Child` from `list_` and signal `cond_` if the list is empty.
     *
     *  Called from `Child::~Child()` on the child's destruction.
     *
     *  @param child The `Child` being destroyed.
     */
    void
    remove(Child& child);

    /** Call `stop()` on all registered children and wait for them to destruct.
     *
     *  Acquires `mutex_` to copy `list_`, releases it, calls `stop()` on
     *  each live child, then waits on `cond_` until `list_` is empty.
     */
    void
    stopChildren();

    /** Ask `PeerFinder` for new outbound connection targets and initiate them. */
    void
    autoConnect();

    /** Collect local endpoint advertisements and forward them to peers. */
    void
    sendEndpoints();

    /** Flush each peer's pending tx-hash queue as a `TMHaveTransactions` message.
     *
     *  Called once per timer tick.  Only peers that negotiated `FEATURE_TXRR`
     *  have a non-empty queue.
     */
    void
    sendTxQueue() const;

    /** Expire idle reduce-relay slots and unsquelch any affected peers.
     *
     *  Removes slots whose `lastSelected_` timestamp exceeds
     *  `MAX_UNSQUELCH_EXPIRE_DEFAULT` and resets slots where the selected peer
     *  has gone idle (no messages for `IDLED` seconds), triggering unsquelch
     *  and reverting to the Counting state.
     */
    void
    deleteIdlePeers();

private:
    /** Collector-managed byte and message gauges for one traffic category.
     *
     *  Instances are stored in `Stats::trafficGauges` keyed by
     *  `TrafficCount::Category`.  Written only under `statsMutex_` during
     *  `collectMetrics()`; the collector reads them on its own schedule.
     */
    struct TrafficGauges
    {
        /** Construct and register all four gauges with the collector.
         *
         *  @param name      Human-readable category name (e.g. `"Transaction"`).
         *  @param collector The metrics collector that owns the gauge objects.
         */
        TrafficGauges(std::string const& name, beast::insight::Collector::ptr const& collector)
            : name(name)
            , bytesIn(collector->makeGauge(name, "Bytes_In"))
            , bytesOut(collector->makeGauge(name, "Bytes_Out"))
            , messagesIn(collector->makeGauge(name, "Messages_In"))
            , messagesOut(collector->makeGauge(name, "Messages_Out"))
        {
        }
        std::string const name;         /**< Category name, used for gauge identity assertions. */
        beast::insight::Gauge bytesIn;  /**< Total inbound bytes in this category. */
        beast::insight::Gauge bytesOut; /**< Total outbound bytes in this category. */
        beast::insight::Gauge messagesIn;  /**< Total inbound messages in this category. */
        beast::insight::Gauge messagesOut; /**< Total outbound messages in this category. */
    };

    /** Collector-managed overlay statistics, including the per-category traffic gauges.
     *
     *  Owns the `beast::insight::Hook` that drives `collectMetrics()`.  The
     *  hook is installed at construction time and fires on the collector's
     *  polling interval.  All gauge writes must be serialized by `statsMutex_`
     *  because gauge objects are not thread-safe.
     */
    struct Stats
    {
        /** Construct all gauges and install the collection hook.
         *
         *  @tparam Handler   Callable type accepted by `Collector::makeHook`.
         *  @param handler    The `collectMetrics` bound callable.
         *  @param collector  Metrics collector.
         *  @param trafficGauges Pre-built map of per-category gauge sets.
         */
        template <class Handler>
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector,
            std::unordered_map<TrafficCount::Category, TrafficGauges>&& trafficGauges)
            : peerDisconnects(collector->makeGauge("Overlay", "Peer_Disconnects"))
            , trafficGauges(std::move(trafficGauges))
            , hook(collector->makeHook(handler))
        {
        }

        beast::insight::Gauge peerDisconnects; /**< Cumulative peer disconnection count gauge. */
        std::unordered_map<TrafficCount::Category, TrafficGauges> trafficGauges; /**< Per-category traffic gauges. */
        beast::insight::Hook hook; /**< Drives `collectMetrics()` on each collector poll. */
    };

    Stats stats_;

    /** Serializes writes to `stats_` gauge members during `collectMetrics()`.
     *  Kept separate from `mutex_` to avoid blocking peer-map operations
     *  during metrics collection.
     */
    std::mutex statsMutex_;

    /** Copy atomic traffic counters into the collector-managed gauge objects.
     *
     *  Called by `stats_.hook` on each collector polling interval.  Acquires
     *  `statsMutex_` (not `mutex_`) so gauge writes do not contend with the
     *  hot-path peer-map operations.  The two-mutex design keeps the
     *  collector's read path decoupled from peer lifecycle churn.
     */
    void
    collectMetrics()
    {
        auto counts = traffic_.getCounts();
        std::scoped_lock const lock(statsMutex_);
        XRPL_ASSERT(
            counts.size() == stats_.trafficGauges.size(),
            "xrpl::OverlayImpl::collect_metrics : counts size do match");

        for (auto const& [key, value] : counts)
        {
            auto it = stats_.trafficGauges.find(key);
            if (it == stats_.trafficGauges.end())
                continue;

            auto& gauge = it->second;

            XRPL_ASSERT(
                gauge.name == value.name,
                "xrpl::OverlayImpl::collect_metrics : gauge and counter "
                "match");

            gauge.bytesIn = value.bytesIn;
            gauge.bytesOut = value.bytesOut;
            gauge.messagesIn = value.messagesIn;
            gauge.messagesOut = value.messagesOut;
        }

        stats_.peerDisconnects = getPeerDisconnect();
    }
};

}  // namespace xrpl
