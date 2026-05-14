#pragma once

#include <xrpld/app/consensus/RCLCxPeerPos.h>
#include <xrpld/app/ledger/detail/LedgerReplayMsgHandler.h>
#include <xrpld/overlay/Squelch.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/peerfinder/PeerfinderManager.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/resource/Fees.h>

#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <queue>
#include <utility>

namespace xrpl {

struct ValidatorBlobInfo;
class SHAMap;

/** Concrete implementation of a live peer-to-peer connection in the XRPL overlay.
 *
 *  Every remote node that connects to a running rippled instance is managed
 *  through a `PeerImp` object. It owns the SSL/TCP socket, drives the
 *  asynchronous read/write loops, dispatches every incoming protobuf message
 *  to the appropriate handler, enforces resource budgets, tracks ledger
 *  convergence, and orchestrates a careful multi-stage shutdown when the
 *  connection ends.
 *
 *  ## Inheritance
 *  - `Peer` — public interface consumed by consensus, ledger acquisition, and
 *    broadcast code outside the overlay.
 *  - `std::enable_shared_from_this<PeerImp>` — every async callback captures
 *    `shared_from_this()` to keep the object alive across I/O completions.
 *    This is why `run()` is deferred rather than called from the constructor.
 *  - `OverlayImpl::Child` — registers with the overlay manager so `stop()` can
 *    be broadcast to all children. The destructor calls `overlay_.deletePeer()`,
 *    `overlay_.onPeerDeactivate()`, and `overlay_.peerFinder().on_closed()` in
 *    sequence.
 *
 *  ## Thread-safety
 *  All connection-level mutable state is confined to `strand_`, a
 *  `boost::asio::strand` that serialises callbacks without a mutex.
 *  `recentLock_` (plain `mutex`) guards ledger-tracking fields accessed from
 *  multiple threads. `nameMutex_` (`shared_mutex`) guards the cluster name.
 *
 *  ## Shutdown state machine
 *  ```
 *  stop() / fail() / onTimer()
 *    → shutdown()            — sets shutdown_, cancels peer timer, posts 5 s safety timer
 *      → tryAsyncShutdown() — gates on !readPending_ && !writePending_
 *        → stream_.async_shutdown()
 *          → onShutdown()   — cancels safety timer, calls close()
 *            → close()      — socket_.close(), notifies overlay
 *  ```
 *  The last in-flight read or write always calls `tryAsyncShutdown()` on
 *  completion when `shutdown_` is set, so the transition is deterministic.
 *  A 5-second safety timer in `onTimer` calls `close()` directly if the SSL
 *  path stalls, preventing hanging connections.
 *
 *  @note `OverlayImpl` is declared `friend` of this class because it needs
 *      direct access for slot cleanup, traffic reporting, and squelch callbacks.
 */
class PeerImp : public Peer, public std::enable_shared_from_this<PeerImp>, public OverlayImpl::Child
{
public:
    /** Whether the peer's view of the ledger converges or diverges from ours.
     *
     *  Transitions are driven by `checkTracking()`. A peer within 24 ledgers
     *  of the locally validated sequence is `Converged`; beyond 128 ledgers it
     *  is `Diverged`. The asymmetric thresholds provide hysteresis to avoid
     *  oscillation near the boundary. Diverged peers are excluded from ledger
     *  data requests even when their advertised range matches the target.
     */
    enum class Tracking { Diverged, Unknown, Converged };

private:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using waitable_timer = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;
    using Compressed = compression::Compressed;

    Application& app_;
    id_t const id_;
    std::string fingerprint_;
    std::string prefix_;
    beast::WrappedSink sink_;
    beast::WrappedSink pSink_;
    beast::Journal const journal_;
    beast::Journal const pJournal_;
    std::unique_ptr<stream_type> streamPtr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;

    // Multi-purpose timer for peer activity monitoring and shutdown safety
    waitable_timer timer_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remoteAddress_;

    // These are up here to prevent warnings about order of initializations
    //
    OverlayImpl& overlay_;
    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion protocol_;

    std::atomic<Tracking> tracking_;
    clock_type::time_point trackingTime_;
    // Node public key of peer.
    PublicKey const publicKey_;
    std::string name_;
    std::shared_mutex mutable nameMutex_;

    // The indices of the smallest and largest ledgers this peer has available
    //
    LedgerIndex minLedger_ = 0;
    LedgerIndex maxLedger_ = 0;
    uint256 closedLedgerHash_;
    uint256 previousLedgerHash_;

    boost::circular_buffer<uint256> recentLedgers_{128};
    boost::circular_buffer<uint256> recentTxSets_{128};

    std::optional<std::chrono::milliseconds> latency_;
    std::optional<std::uint32_t> lastPingSeq_;
    clock_type::time_point lastPingTime_;
    clock_type::time_point const creationTime_;

    reduce_relay::Squelch<UptimeClock> squelch_;

    // Notes on thread locking:
    //
    // During an audit it was noted that some member variables that looked
    // like they need thread protection were not receiving it.  And, indeed,
    // that was correct.  But the multi-phase initialization of PeerImp
    // makes such an audit difficult.  A further audit suggests that the
    // locking is now protecting variables that don't need it.  We're
    // leaving that locking in place (for now) as a form of future proofing.
    //
    // Here are the variables that appear to need locking currently:
    //
    // o closedLedgerHash_
    // o previousLedgerHash_
    // o minLedger_
    // o maxLedger_
    // o recentLedgers_
    // o recentTxSets_
    // o trackingTime_
    // o latency_
    //
    // The following variables are being protected preemptively:
    //
    // o name_
    // o lastStatus_
    //
    // June 2019

    /** Accumulates the highest resource charge incurred while processing a
     *  batch of incoming messages.
     *
     *  `update()` enforces that the charge can only escalate, never decrease,
     *  reflecting a "worst case per message batch" policy. The accumulated fee
     *  is applied to `usage_` once at message-batch boundaries via `charge()`,
     *  avoiding per-fragment charge calls in the inner read loop.
     */
    struct ChargeWithContext
    {
        Resource::Charge fee = Resource::kFEE_TRIVIAL_PEER;
        std::string context{};  // NOLINT(readability-redundant-member-init)

        /** Escalate the accumulated charge if @p f is higher than the current fee.
         *
         *  @param f   New candidate charge level; must be >= the current fee.
         *  @param add Additional context string appended to the log context.
         */
        void
        update(Resource::Charge f, std::string const& add)
        {
            XRPL_ASSERT(f >= fee, "xrpl::PeerImp::ChargeWithContext::update : fee increases");
            fee = f;
            if (!context.empty())
            {
                context += " ";
            }
            context += add;
        }
    };

    std::mutex mutable recentLock_;
    protocol::TMStatusChange lastStatus_;
    Resource::Consumer usage_;
    ChargeWithContext fee_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::multi_buffer readBuffer_;
    http_request_type request_;
    http_response_type response_;
    boost::beast::http::fields const& headers_;
    std::queue<std::shared_ptr<Message>> sendQueue_;

    // Primary shutdown flag set when shutdown is requested
    bool shutdown_ = false;

    // SSL shutdown coordination flag
    bool shutdownStarted_ = false;

    // Indicates a read operation is currently pending
    bool readPending_ = false;

    // Indicates a write operation is currently pending
    bool writePending_ = false;

    int largeSendq_ = 0;
    std::unique_ptr<LoadEvent> loadEvent_;
    // The highest sequence of each PublisherList that has
    // been sent to or received from this peer.
    hash_map<PublicKey, std::size_t> publisherListSequences_;

    Compressed compressionEnabled_ = Compressed::Off;

    // Queue of transactions' hashes that have not been
    // relayed. The hashes are sent once a second to a peer
    // and the peer requests missing transactions from the node.
    hash_set<uint256> txQueue_;
    // true if tx reduce-relay feature is enabled on the peer.
    bool txReduceRelayEnabled_ = false;

    bool ledgerReplayEnabled_ = false;
    LedgerReplayMsgHandler ledgerReplayMsgHandler_;

    friend class OverlayImpl;

    /** Per-direction bandwidth tracker using a 30-bucket rolling average.
     *
     *  Buckets are one second wide. `addMessage()` advances the window at each
     *  second boundary and recomputes the rolling average. Reads from diagnostic
     *  paths are safe via a `shared_mutex`; writes run on the strand.
     *
     *  @note Not copyable or movable — the mutex makes it immovable by design.
     */
    class Metrics
    {
    public:
        Metrics() = default;
        Metrics(Metrics const&) = delete;
        Metrics&
        operator=(Metrics const&) = delete;
        Metrics(Metrics&&) = delete;
        Metrics&
        operator=(Metrics&&) = delete;

        /** Record @p bytes for the current one-second interval.
         *
         *  If more than one second has elapsed since `intervalStart_`, the
         *  accumulated count is pushed into `rollingAvg_` and the rolling
         *  average is recomputed. Thread-safe via exclusive lock.
         *
         *  @param bytes Number of bytes transferred in this message.
         */
        void
        addMessage(std::uint64_t bytes);

        /** Return the smoothed rolling-average throughput in bytes per second.
         *
         *  @return Average bytes/s over the last 30 one-second buckets.
         */
        std::uint64_t
        averageBytes() const;

        /** Return the cumulative byte count since connection start.
         *
         *  @return Total bytes transferred in this direction.
         */
        std::uint64_t
        totalBytes() const;

    private:
        std::shared_mutex mutable mutex_;
        boost::circular_buffer<std::uint64_t> rollingAvg_{30, 0ull};
        clock_type::time_point intervalStart_{clock_type::now()};
        std::uint64_t totalBytes_{0};
        std::uint64_t accumBytes_{0};
        std::uint64_t rollingAvgBytes_{0};
    };

    struct
    {
        Metrics sent;
        Metrics recv;
    } metrics_;

public:
    PeerImp(PeerImp const&) = delete;
    PeerImp&
    operator=(PeerImp const&) = delete;

    /** Construct an active inbound peer from a completed SSL upgrade.
     *
     *  Feature negotiation (compression, TX reduce-relay, ledger replay) is
     *  resolved immediately by inspecting @p request headers. After construction
     *  call `run()` to start the protocol message loop via `doAccept()`.
     *
     *  @param app        The application context.
     *  @param id         Unique numeric identifier for this peer connection.
     *  @param slot       PeerFinder slot already allocated for this connection.
     *  @param request    The completed HTTP upgrade request received from the peer.
     *  @param publicKey  The verified node public key of the remote peer.
     *  @param protocol   Negotiated XRPL wire protocol version.
     *  @param consumer   Resource consumer for fee accounting.
     *  @param streamPtr  Ownership of the established SSL stream.
     *  @param overlay    The overlay manager that owns this peer.
     */
    PeerImp(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& streamPtr,
        OverlayImpl& overlay);

    /** Construct an active outbound peer from a completed SSL handshake.
     *
     *  Feature negotiation is resolved by inspecting @p response headers.
     *  Any bytes already received during the handshake are pre-loaded into
     *  `readBuffer_` from @p buffers. After construction call `run()` to start
     *  the protocol message loop via `doProtocolStart()`.
     *
     *  @tparam Buffers   A `ConstBufferSequence` holding bytes already received.
     *  @param app        The application context.
     *  @param streamPtr  Ownership of the established SSL stream.
     *  @param buffers    Bytes already read from the stream during the handshake.
     *  @param slot       PeerFinder slot already allocated (ownership transferred).
     *  @param response   The HTTP upgrade response received from the remote peer.
     *  @param usage      Resource consumer for fee accounting.
     *  @param publicKey  The verified node public key of the remote peer.
     *  @param protocol   Negotiated XRPL wire protocol version.
     *  @param id         Unique numeric identifier for this peer connection.
     *  @param overlay    The overlay manager that owns this peer.
     */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    PeerImp(
        Application& app,
        std::unique_ptr<stream_type>&& streamPtr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        OverlayImpl& overlay);

    ~PeerImp() override;

    /** Return the protocol-level journal for this peer connection. */
    beast::Journal const&
    pJournal() const
    {
        return pJournal_;
    }

    /** Return the PeerFinder slot associated with this connection. */
    std::shared_ptr<PeerFinder::Slot> const&
    slot()
    {
        return slot_;
    }

    /** Start the protocol message loop.
     *
     *  Deferred from the constructor because `shared_from_this()` cannot be
     *  called before the object is owned by a `shared_ptr`. For inbound peers
     *  this calls `doAccept()`; for outbound peers it calls `doProtocolStart()`
     *  directly.
     */
    // Work-around for calling shared_from_this in constructors
    virtual void
    run();

    /** Initiate a graceful shutdown, called when the overlay is stopping.
     *
     *  Posts to `strand_` if not already on it, then calls `shutdown()`.
     *  Idempotent — safe to call multiple times.
     */
    void
    stop() override;

    //
    // Network
    //

    /** Enqueue a protocol message for transmission to this peer.
     *
     *  Checks the squelch table for validator-keyed messages and silently
     *  drops suppressed ones. Records outbound traffic metrics. Disconnects
     *  the peer if the send queue grows beyond the `dropSendQueue` threshold
     *  for too many consecutive timer intervals. Thread-safe — posts to
     *  `strand_` if called from another thread.
     *
     *  @param m  The pre-serialised message to send.
     */
    void
    send(std::shared_ptr<Message> const& m) override;

    /** Flush the pending TX-hash queue as a single `TMHaveTransactions` message.
     *
     *  Used by the transaction reduce-relay path. Clears `txQueue_` after
     *  sending. No-op when the queue is empty. Thread-safe — posts to
     *  `strand_`.
     */
    void
    sendTxQueue() override;

    /** Add a transaction hash to the outbound TX-hash queue.
     *
     *  Hashes accumulate in `txQueue_` and are flushed as a batch once per
     *  second via `sendTxQueue()`. If the queue reaches
     *  `reduce_relay::MAX_TX_QUEUE_SIZE` it is flushed immediately. Thread-safe
     *  — posts to `strand_`.
     *
     *  @param hash The transaction hash to advertise to this peer.
     */
    void
    addTxQueue(uint256 const& hash) override;

    /** Remove a transaction hash from the outbound TX-hash queue.
     *
     *  Called when we learn the peer already has the transaction, making the
     *  pending advertisement redundant. Thread-safe — posts to `strand_`.
     *
     *  @param hash The transaction hash to remove.
     */
    void
    removeTxQueue(uint256 const& hash) override;

    /** Encode and send a list of PeerFinder endpoints as a `TMEndpoints` v2 message.
     *
     *  @tparam FwdIt  A forward iterator whose value type is `PeerFinder::Endpoint`.
     *  @param first   Iterator to the first endpoint to include.
     *  @param last    Past-the-end iterator.
     */
    template <
        class FwdIt,
        class = typename std::enable_if_t<
            std::is_same_v<typename std::iterator_traits<FwdIt>::value_type, PeerFinder::Endpoint>>>
    void
    sendEndpoints(FwdIt first, FwdIt last);

    /** Return the remote IP address and port for this connection. */
    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remoteAddress_;
    }

    /** Apply a resource charge to this peer's account.
     *
     *  Disconnects the peer if the accumulated charge causes the resource
     *  balance to cross the disconnect threshold. Must be called on the strand.
     *
     *  @param fee      The charge level to apply.
     *  @param context  Human-readable description for log attribution.
     */
    void
    charge(Resource::Charge const& fee, std::string const& context) override;

    //
    // Identity
    //

    /** Return the unique numeric ID assigned to this peer connection. */
    Peer::id_t
    id() const override
    {
        return id_;
    }

    /** Return `true` if this peer has consented to publicly share its IP address.
     *
     *  Determined by the presence of a `Crawl: public` HTTP header during
     *  the handshake (case-insensitive comparison).
     */
    bool
    crawl() const;

    /** Return `true` if the peer's node key is in the local cluster registry. */
    bool
    cluster() const override;

    /** Update the tracking state based on a recently validated ledger sequence.
     *
     *  Compares @p validationSeq against the peer's advertised maximum ledger.
     *  Transitions `tracking_` between `Converged`, `Unknown`, and `Diverged`
     *  using the hysteresis thresholds from `Tuning.h` (24 ledgers for
     *  converged, 128 for diverged). Uses an atomic to avoid locking in the
     *  hot validation path.
     *
     *  @param validationSeq  Sequence number of the latest locally validated ledger.
     */
    void
    checkTracking(std::uint32_t validationSeq);

    /** Update tracking state given two sequence numbers to check against.
     *
     *  Checks @p seq1 and @p seq2 against the peer's maximum advertised ledger.
     *  Used internally when both the previous and current validated sequences
     *  are available.
     *
     *  @param seq1  First sequence number to test (e.g., previous validated).
     *  @param seq2  Second sequence number to test (e.g., current validated).
     */
    void
    checkTracking(std::uint32_t seq1, std::uint32_t seq2);

    /** Return the verified node public key of the remote peer. */
    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    /** Return the software version string reported by the peer, if available.
     *
     *  For inbound connections reads the `User-Agent` header; for outbound
     *  reads the `Server` header. Returns an empty string if the header is
     *  absent.
     */
    std::string
    getVersion() const;

    /** Return the elapsed time since this connection was established. */
    // Return the connection elapsed time.
    clock_type::duration
    uptime() const
    {
        return clock_type::now() - creationTime_;
    }

    /** Serialise all peer diagnostics to a JSON object.
     *
     *  Includes: public key, remote address, cluster name, domain, protocol
     *  version, network ID, load, software version, latency, uptime, ledger
     *  range, tracking state, last status change, and bandwidth metrics.
     *  Briefly acquires `recentLock_` to snapshot mutable fields.
     *
     *  @return A `json::Value` object suitable for RPC or crawl responses.
     */
    json::Value
    json() override;

    /** Return `true` if the negotiated protocol supports the given feature.
     *
     *  - `ValidatorListPropagation`: protocol >= 2.1
     *  - `ValidatorList2Propagation`: protocol >= 2.2
     *  - `LedgerReplay`: controlled by the `ledgerReplayEnabled_` flag set
     *    during handshake feature negotiation.
     *
     *  @param f  The protocol feature to query.
     *  @return `true` if the feature is supported on this link.
     */
    bool
    supportsFeature(ProtocolFeature f) const override;

    /** Return the highest validator-list sequence received from or sent to this
     *  peer for a given publisher, if any.
     *
     *  @param pubKey  The publisher's public key.
     *  @return The last known sequence number, or `std::nullopt` if not seen.
     */
    std::optional<std::size_t>
    publisherListSequence(PublicKey const& pubKey) const override
    {
        std::scoped_lock const sl(recentLock_);

        auto iter = publisherListSequences_.find(pubKey);
        if (iter != publisherListSequences_.end())
            return iter->second;
        return {};
    }

    /** Record the highest validator-list sequence sent to or received from
     *  this peer for a given publisher.
     *
     *  @param pubKey  The publisher's public key.
     *  @param seq     The sequence number to record.
     */
    void
    setPublisherListSequence(PublicKey const& pubKey, std::size_t const seq) override
    {
        std::scoped_lock const sl(recentLock_);

        publisherListSequences_[pubKey] = seq;
    }

    //
    // Ledger
    //

    /** Return the hash of the peer's current closed ledger.
     *
     *  Updated when the peer sends a `TMStatusChange` message. Not locked —
     *  callers that need a consistent snapshot should acquire `recentLock_`
     *  themselves or use `hasLedger()`.
     */
    uint256 const&
    getClosedLedgerHash() const override
    {
        return closedLedgerHash_;
    }

    /** Return `true` if the peer likely has the ledger identified by @p hash or @p seq.
     *
     *  Two independent evidence paths: the peer's advertised range covers @p seq
     *  AND the peer is `Converged`, OR the hash appears in `recentLedgers_`.
     *  Diverged peers fail the range check even when the sequence matches.
     *  Acquires `recentLock_`.
     *
     *  @param hash  The ledger hash to query.
     *  @param seq   The ledger sequence to query.
     *  @return `true` if the peer is believed to hold this ledger.
     */
    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override;

    /** Copy the peer's advertised available ledger range into the output parameters.
     *
     *  Acquires `recentLock_`.
     *
     *  @param minSeq  Receives the lowest available ledger sequence.
     *  @param maxSeq  Receives the highest available ledger sequence.
     */
    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override;

    /** Return `true` if @p hash appears in the peer's recent transaction-set cache.
     *
     *  Acquires `recentLock_`.
     *
     *  @param hash  The transaction-set hash to query.
     */
    bool
    hasTxSet(uint256 const& hash) const override;

    /** Rotate ledger hashes when the local node closes a new ledger.
     *
     *  Moves `closedLedgerHash_` into `previousLedgerHash_` and clears
     *  `closedLedgerHash_`, ready to be updated by the peer's next
     *  `TMStatusChange`. Acquires `recentLock_`.
     */
    void
    cycleStatus() override;

    /** Return `true` if the peer's advertised ledger range covers [uMin, uMax].
     *
     *  Always returns `false` for `Diverged` peers even when the range matches,
     *  because a diverged peer may be on a different fork.
     *
     *  @param uMin  Minimum ledger sequence required.
     *  @param uMax  Maximum ledger sequence required.
     */
    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override;

    /** Compute a priority score for querying this peer for a specific item.
     *
     *  Score components:
     *  - Random baseline in [0, 9999].
     *  - +10000 if the peer is believed to have the item.
     *  - -30 × latency_ms (penalises high-latency peers).
     *  - -8000 if no latency sample has been recorded yet.
     *
     *  @param haveItem  `true` if the peer is known to hold the queried item.
     *  @return Composite priority score; higher is preferred.
     */
    // Called to determine our priority for querying
    int
    getScore(bool haveItem) const override;

    /** Return `true` if the measured round-trip latency exceeds the high-latency threshold.
     *
     *  The threshold is `Tuning::kPEER_HIGH_LATENCY` (300 ms). Returns `false`
     *  if no latency sample has been recorded. Acquires `recentLock_`.
     */
    bool
    isHighLatency() const override;

    /** Return `true` if LZ4 compression was negotiated for this link. */
    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

    /** Return `true` if the transaction reduce-relay feature was negotiated for this link.
     *
     *  When `true`, this peer operates in gossip-pull mode: full transactions
     *  are not forwarded directly; instead hash announcements are batched in
     *  `txQueue_` and the peer pulls bodies on demand.
     */
    bool
    txReduceRelayEnabled() const override
    {
        return txReduceRelayEnabled_;
    }

private:
    /** Handle an I/O or protocol failure identified by an error code.
     *
     *  Logs a warning and initiates graceful shutdown. No-op if the connection
     *  is already closing. Must be called on the strand.
     *
     *  @param name  Name of the failing operation (e.g., `"read"`, `"write"`).
     *  @param ec    The error code associated with the failure.
     */
    void
    fail(std::string const& name, error_code ec);

    /** Handle a logical or protocol failure described by a reason string.
     *
     *  Logs a warning with @p reason and initiates graceful shutdown. No-op if
     *  the connection is already closing. Must be called on the strand.
     *
     *  @param reason  Human-readable description of the failure.
     */
    void
    fail(std::string const& reason);

    /** Begin the connection teardown sequence.
     *
     *  Sets `shutdown_`, cancels the peer activity timer, and posts a 5-second
     *  safety timer. Then calls `tryAsyncShutdown()` to start the SSL shutdown
     *  if no I/O is currently in flight. All subsequent I/O completions will
     *  also call `tryAsyncShutdown()`, so the safety net fires only if the
     *  graceful SSL path stalls. Must be called on the strand.
     */
    void
    shutdown();

    /** Start the SSL async_shutdown if no I/O operations are in flight.
     *
     *  Gates on `!readPending_ && !writePending_` and `!shutdownStarted_`.
     *  Sets `shutdownStarted_` before posting `async_shutdown` to prevent
     *  double-initiation. Must be called on the strand.
     */
    void
    tryAsyncShutdown();

    /** Callback invoked when the SSL async_shutdown completes.
     *
     *  Cancels the safety timer regardless of @p ec, then calls `close()`.
     *  Errors during SSL shutdown are logged but do not prevent socket closure.
     *  Runs on the strand.
     *
     *  @param ec  Result of the `async_shutdown` operation.
     */
    void
    onShutdown(error_code ec);

    /** Close the underlying TCP socket and notify the overlay.
     *
     *  Cancels pending timers and calls `socket_.close()`. Bypasses the SSL
     *  shutdown — used either as the final step after `onShutdown()` or as the
     *  forced path when the safety timer fires. Must be called on the strand.
     */
    void
    close();

    /** Arm the peer activity timer for @p interval seconds.
     *
     *  Calls `async_wait` with `onTimer` as the completion handler. Any error
     *  starting the timer triggers `fail()`. Must be called on the strand.
     *
     *  @param interval  Duration until the timer expires.
     */
    void
    setTimer(std::chrono::seconds interval);

    /** Timer expiration callback for peer activity monitoring.
     *
     *  Checks for ping timeouts, oversized send queues, and tracking divergence.
     *  If @p ec is `operation_aborted` the timer was cancelled and no action is
     *  taken. Otherwise re-arms the timer for the next interval after performing
     *  health checks. Runs on the strand.
     *
     *  @param ec  Error code from the timer wait; `operation_aborted` on cancel.
     */
    void
    onTimer(error_code const& ec);

    /** Cancel the peer activity timer without throwing.
     *
     *  Silently swallows any error from the cancellation call, since errors
     *  here are benign (timer already expired or not running).
     */
    void
    cancelTimer() noexcept;

    /** Build the log-line prefix string from a connection fingerprint.
     *
     *  @param fingerprint  Unique fingerprint for the connection.
     *  @return Log prefix of the form `"[fingerprint] "`.
     */
    static std::string
    makePrefix(std::string const& fingerprint);

    /** Complete the inbound handshake and start the protocol message loop.
     *
     *  Verifies the shared TLS value, sets the cluster name if applicable,
     *  activates the slot in the overlay, sends the HTTP 101 response, then
     *  chains to `doProtocolStart()`. Throws `std::runtime_error` on any
     *  verification failure, which is caught by the caller and results in
     *  `fail()`.
     */
    void
    doAccept();

    /** Return the cluster-assigned name for this peer, or empty string.
     *
     *  Protected by a shared lock on `nameMutex_`.
     */
    std::string
    name() const;

    /** Return the value of the `Server-Domain` HTTP header sent by the peer.
     *
     *  Returns an empty string if the header is absent.
     */
    std::string
    domain() const;

    //
    // protocol message loop
    //

    /** Start the asynchronous protocol message read loop.
     *
     *  Posts the first `async_read_some` to begin consuming wire frames.
     *  For inbound peers also sends all locally known validator lists
     *  (if the protocol version supports `ValidatorListPropagation`) and
     *  sends a manifests message. Arms the periodic ping timer.
     */
    void
    doProtocolStart();

    /** Async-read completion handler for incoming protocol message bytes.
     *
     *  Commits received bytes to `readBuffer_`, then runs the message dispatch
     *  loop until the buffer is exhausted or only a partial header remains.
     *  Logs if a single message dispatch takes more than 350 ms. Handles EOF
     *  as a graceful shutdown, `operation_aborted` as a deferred
     *  `tryAsyncShutdown()`, and all other errors as `fail()`. Posts the next
     *  `async_read_some` to continue the loop. Runs on the strand.
     *
     *  @param ec               Error code from the read operation.
     *  @param bytesTransferred Number of bytes placed in `readBuffer_`.
     */
    void
    onReadMessage(error_code ec, std::size_t bytesTransferred);

    /** Async-write completion handler for outgoing protocol message bytes.
     *
     *  Pops the completed message from `sendQueue_`. If the queue is still
     *  non-empty, posts the next `async_write`. On `operation_aborted` defers
     *  to `tryAsyncShutdown()`; on other errors calls `fail()`. After clearing
     *  `writePending_`, calls `tryAsyncShutdown()` if `shutdown_` is set.
     *  Runs on the strand.
     *
     *  @param ec               Error code from the write operation.
     *  @param bytesTransferred Number of bytes successfully sent (unused).
     */
    void
    onWriteMessage(error_code ec, std::size_t bytesTransferred);

    /** Validate and relay a single transaction received from this peer.
     *
     *  Called from both `onMessage(TMTransaction)` and
     *  `onMessage(TMTransactions)`. Rejects transactions from diverged peers
     *  and refuses `tfInnerBatchTxn`-flagged transactions regardless of
     *  amendment state (charges `kFEE_MODERATE_BURDEN_PEER`). Uses the hash
     *  router to deduplicate. Signature verification is skipped for cluster
     *  peers. Expired transactions (past `sfLastLedgerSequence`) are marked
     *  BAD in the hash router.
     *
     *  @param m             The `TMTransaction` protobuf message.
     *  @param eraseTxQueue  If `true`, remove the transaction hash from
     *                       `txQueue_` (called for single-TX path only).
     *  @param batch         If `true`, do not apply the per-transaction
     *                       resource surcharge (batch-path fee is shared).
     */
    void
    handleTransaction(
        std::shared_ptr<protocol::TMTransaction> const& m,
        bool eraseTxQueue,
        bool batch);

    /** Handle a `TMHaveTransactions` advertisement from this peer.
     *
     *  Inspects each advertised hash: if not in the local cache, it is added
     *  to a `TMGetObjectByHash` request sent back to the peer. Hashes already
     *  held locally are removed from `txQueue_` (we no longer need to announce
     *  them to this peer). Validates each hash is exactly `uint256` bytes.
     *
     *  @param m  The `TMHaveTransactions` message listing unheld hashes.
     */
    void
    handleHaveTransactions(std::shared_ptr<protocol::TMHaveTransactions> const& m);

    /** Return the TLS fingerprint string used to identify this connection in logs. */
    std::string const&
    fingerprint() const override
    {
        return fingerprint_;
    }

    /** Return the log-line prefix string for this peer. */
    std::string const&
    prefix() const
    {
        return prefix_;
    }

public:
    //--------------------------------------------------------------------------
    //
    // ProtocolStream
    //
    //--------------------------------------------------------------------------

    /** Called when a message with an unrecognised type code is received.
     *
     *  Currently a no-op placeholder; future versions may apply a resource
     *  charge or close the connection for unknown message types.
     *
     *  @param type  The numeric message type from the wire header.
     */
    void
    onMessageUnknown(std::uint16_t type);

    /** Called at the start of each incoming protobuf message for accounting.
     *
     *  Creates a load-event entry in the job-queue profiler, resets `fee_` to
     *  `kFEE_TRIVIAL_PEER`, and records inbound byte counts (total and
     *  per-category). Also forwards TX-related message sizes to the overlay's
     *  `TxMetrics` aggregator.
     *
     *  @param type             Wire message type code.
     *  @param m                The deserialized protobuf message object.
     *  @param size             Compressed (wire) byte size of the message.
     *  @param uncompressedSize Uncompressed byte size of the message.
     *  @param isCompressed     Whether the message arrived compressed.
     */
    void
    onMessageBegin(
        std::uint16_t type,
        std::shared_ptr<::google::protobuf::Message> const& m,
        std::size_t size,
        std::size_t uncompressedSize,
        bool isCompressed);

    /** Called at the end of each incoming message to finalise accounting.
     *
     *  Releases the load-event profiler entry and applies the accumulated
     *  `fee_` charge via `charge()`. If the charge exceeds the disconnect
     *  threshold the peer is disconnected inside `charge()`.
     *
     *  @param type  Wire message type code.
     *  @param m     The deserialized protobuf message object.
     */
    void
    onMessageEnd(std::uint16_t type, std::shared_ptr<::google::protobuf::Message> const& m);

    /** @name Protocol message handlers
     *  Each overload dispatches one protobuf message type received from the peer.
     *  Handlers are invoked from the protocol message loop on the strand.
     *  @{
     */
    void
    onMessage(std::shared_ptr<protocol::TMManifests> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMPing> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMCluster> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMEndpoints> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMTransaction> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetLedger> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMLedgerData> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProposeSet> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMStatusChange> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMHaveTransactionSet> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidatorList> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidatorListCollection> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMValidation> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMGetObjectByHash> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMHaveTransactions> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMTransactions> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMSquelch> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProofPathRequest> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMProofPathResponse> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMReplayDeltaRequest> const& m);
    void
    onMessage(std::shared_ptr<protocol::TMReplayDeltaResponse> const& m);
    /** @} */

private:
    //--------------------------------------------------------------------------
    // lockedRecentLock is passed as a reminder to callers that recentLock_
    // must be locked.

    /** Append @p hash to the recent-ledgers circular buffer if not already present.
     *
     *  The `lockedRecentLock` parameter is a zero-cost statically-enforced
     *  contract: callers must hold `recentLock_` before calling this function.
     *
     *  @param hash              The ledger hash to record.
     *  @param lockedRecentLock  Proof that the caller holds `recentLock_`.
     */
    void
    addLedger(uint256 const& hash, std::scoped_lock<std::mutex> const& lockedRecentLock);

    /** Serve a fetch-pack request from this peer.
     *
     *  Guards against serving when loaded, when the validated ledger is stale
     *  (> 40 s), or when too many pack jobs are already queued. Sets the
     *  resource charge to `kFEE_HEAVY_BURDEN_PEER` and dispatches the actual
     *  pack construction to a `JtPACK` job via `LedgerMaster::makeFetchPack`.
     *
     *  @param packet  The `TMGetObjectByHash` request from the peer.
     */
    void
    doFetchPack(std::shared_ptr<protocol::TMGetObjectByHash> const& packet);

    /** Process a combined validator-list message (single or collection).
     *
     *  Parses the manifest and blobs, then passes them to the validator-list
     *  subsystem for trust evaluation and relay. Charges appropriate fees on
     *  malformed or stale content.
     *
     *  @param messageType  Human-readable type label for logging.
     *  @param manifest     Serialised publisher manifest.
     *  @param version      List format version (1 or 2).
     *  @param blobs        The encoded validator-list blobs.
     */
    void
    onValidatorListMessage(
        std::string const& messageType,
        std::string const& manifest,
        std::uint32_t version,
        std::vector<ValidatorBlobInfo> const& blobs);

    /** Serve a missing-transactions request sent in response to `TMHaveTransactions`.
     *
     *  Validates that the request is within `reduce_relay::MAX_TX_QUEUE_SIZE`.
     *  For each requested hash, looks up the transaction in the master
     *  transaction cache and serialises it into a `TMTransactions` reply.
     *  Charges `kFEE_MALFORMED_REQUEST` for any hash not found in the cache.
     *
     *  @param packet  The `TMGetObjectByHash` request listing missing hashes.
     */
    void
    doTransactions(std::shared_ptr<protocol::TMGetObjectByHash> const& packet);

    /** Validate and submit a transaction to the local node, then relay it.
     *
     *  Runs on the job queue (deferred from the strand). Rejects
     *  `tfInnerBatchTxn`-flagged transactions and expired transactions
     *  (past `sfLastLedgerSequence`). Pseudo-transactions are canonicalised
     *  and relayed but not submitted locally. For regular transactions,
     *  optionally verifies the signature (bypassed for cluster peers) before
     *  calling `processTransaction()`.
     *
     *  @param flags          Hash-router flags for this transaction hash.
     *  @param checkSignature If `true`, verify the transaction signature.
     *  @param stx            The deserialized signed transaction.
     *  @param batch          If `true`, the transaction arrived in a batch
     *                        (affects resource charge computation).
     */
    void
    checkTransaction(
        HashRouterFlags flags,
        bool checkSignature,
        std::shared_ptr<STTx const> const& stx,
        bool batch);

    /** Validate and dispatch a consensus proposal from this peer.
     *
     *  Runs on the job queue. Skips the signature check for cluster peers.
     *  Calls `processTrustedProposal()` for trusted sources; forwards to
     *  `NetworkOPs` for untrusted sources if `RELAY_UNTRUSTED_PROPOSALS` is
     *  set. Relays the raw message and calls `updateSlotAndSquelch()` with
     *  the suppression set.
     *
     *  @param isTrusted  Whether the sending peer is in the trusted UNL.
     *  @param packet     The raw `TMProposeSet` protobuf message.
     *  @param peerPos    The decoded proposal position.
     */
    void
    checkPropose(
        bool isTrusted,
        std::shared_ptr<protocol::TMProposeSet> const& packet,
        RCLCxPeerPos peerPos);

    /** Validate and process a ledger validation message from this peer.
     *
     *  Runs on the job queue. Calls `STValidation::isValid()` for the
     *  cryptographic check. Passes valid validations to
     *  `NetworkOPs::recvValidation()`. Relays the raw message if accepted or
     *  if the peer is in the cluster. Calls `updateSlotAndSquelch()` with
     *  the suppression set.
     *
     *  @param val     The deserialized signed validation.
     *  @param key     The hash key identifying this validation in the router.
     *  @param packet  The raw `TMValidation` protobuf message.
     */
    void
    checkValidation(
        std::shared_ptr<STValidation> const& val,
        uint256 const& key,
        std::shared_ptr<protocol::TMValidation> const& packet);

    /** Serialise a ledger header and root nodes, then send as `TMLedgerData`.
     *
     *  Appends the state-map and transaction-map root nodes when available.
     *
     *  @param ledger      The ledger whose header to send.
     *  @param ledgerData  The `TMLedgerData` message being assembled (modified in place).
     */
    void
    sendLedgerBase(std::shared_ptr<Ledger const> const& ledger, protocol::TMLedgerData& ledgerData);

    /** Retrieve the ledger requested by a `TMGetLedger` message.
     *
     *  Lookup priority: by hash → by sequence → current closed ledger.
     *  On a miss with a `querytype` but no `requestcookie`, relays the request
     *  to the best available peer. Validates the returned sequence matches the
     *  request. Rejects ledgers older than `getEarliestFetch`.
     *
     *  @param m  The `TMGetLedger` request.
     *  @return   The matching ledger, or `nullptr` if not available.
     */
    std::shared_ptr<Ledger const>
    getLedger(std::shared_ptr<protocol::TMGetLedger> const& m);

    /** Retrieve the transaction set requested by a `TMGetLedger` message.
     *
     *  Looks up in `InboundTransactions`. On a miss with a `querytype` but no
     *  `requestcookie`, relays to the best peer.
     *
     *  @param m  The `TMGetLedger` request.
     *  @return   The matching `SHAMap`, or `nullptr` if not available.
     */
    std::shared_ptr<SHAMap const>
    getTxSet(std::shared_ptr<protocol::TMGetLedger> const& m) const;

    /** Dispatch a `TMGetLedger` request, applying backpressure and serving data.
     *
     *  Charges `kFEE_MODERATE_BURDEN_PEER` unless this is a relay response.
     *  Handles `liTS_CANDIDATE` by delegating to `getTxSet`. Applies send-queue
     *  and load backpressure checks. Uses adaptive query depth (1–2 nodes) based
     *  on peer latency. Enforces hard node-count caps from `Tuning.h`.
     *
     *  @param m  The `TMGetLedger` request to process.
     */
    void
    processLedgerRequest(std::shared_ptr<protocol::TMGetLedger> const& m);
};

//------------------------------------------------------------------------------

template <class Buffers>
PeerImp::PeerImp(
    Application& app,
    std::unique_ptr<stream_type>&& streamPtr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    OverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , fingerprint_(getFingerprint(slot->remoteEndpoint(), publicKey, to_string(id_)))
    , prefix_(makePrefix(fingerprint_))
    , sink_(app_.getJournal("Peer"), prefix_)
    , pSink_(app_.getJournal("Protocol"), prefix_)
    , journal_(sink_)
    , pJournal_(pSink_)
    , streamPtr_(std::move(streamPtr))
    , socket_(streamPtr_->next_layer().socket())
    , stream_(*streamPtr_)
    , strand_(boost::asio::make_strand(socket_.get_executor()))
    , timer_(waitable_timer{socket_.get_executor()})
    , remoteAddress_(slot->remoteEndpoint())
    , overlay_(overlay)
    , inbound_(false)
    , protocol_(std::move(protocol))
    , tracking_(Tracking::Unknown)
    , trackingTime_(clock_type::now())
    , publicKey_(publicKey)
    , lastPingTime_(clock_type::now())
    , creationTime_(clock_type::now())
    , squelch_(app_.getJournal("Squelch"))
    , usage_(usage)
    , fee_{.fee = Resource::kFEE_TRIVIAL_PEER}
    , slot_(std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
    , compressionEnabled_(
          peerFeatureEnabled(headers_, kFEATURE_COMPR, "lz4", app_.config().COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
    , txReduceRelayEnabled_(
          peerFeatureEnabled(headers_, kFEATURE_TXRR, app_.config().TX_REDUCE_RELAY_ENABLE))
    , ledgerReplayEnabled_(
          peerFeatureEnabled(headers_, kFEATURE_LEDGER_REPLAY, app_.config().LEDGER_REPLAY))
    , ledgerReplayMsgHandler_(app, app.getLedgerReplayer())
{
    readBuffer_.commit(
        boost::asio::buffer_copy(readBuffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
    JLOG(journal_.info()) << "compression enabled " << (compressionEnabled_ == Compressed::On)
                          << " vp reduce-relay base squelch enabled "
                          << peerFeatureEnabled(
                                 headers_,
                                 kFEATURE_VPRR,
                                 app_.config().VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
                          << " tx reduce-relay enabled " << txReduceRelayEnabled_ << " on "
                          << remoteAddress_ << " " << id_;
}

template <class FwdIt, class>
void
PeerImp::sendEndpoints(FwdIt first, FwdIt last)
{
    protocol::TMEndpoints tm;

    while (first != last)
    {
        auto& tme2(*tm.add_endpoints_v2());
        tme2.set_endpoint(first->address.toString());
        tme2.set_hops(first->hops);
        first++;
    }
    tm.set_version(2);

    send(std::make_shared<Message>(tm, protocol::mtENDPOINTS));
}

}  // namespace xrpl
