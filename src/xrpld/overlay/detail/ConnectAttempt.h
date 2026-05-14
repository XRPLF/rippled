#pragma once

#include <xrpld/overlay/detail/OverlayImpl.h>

#include <chrono>

namespace xrpl {

/** Manages one outbound TCP/TLS/HTTP connection attempt to a remote XRPL peer.
 *
 *  Instantiated by `OverlayImpl` when `PeerFinder` decides a new outbound
 *  connection should be made.  Drives a five-phase async pipeline — TCP
 *  connect, TLS handshake, HTTP upgrade write, HTTP upgrade read, response
 *  processing — culminating in either promoting the result to a live `PeerImp`
 *  or reporting failure and releasing the `PeerFinder` slot.
 *
 *  Inherits `OverlayImpl::Child` so that `OverlayImpl::stopChildren()` can
 *  reach every live attempt during overlay shutdown.  Inherits
 *  `enable_shared_from_this` because every async callback captures a
 *  `shared_ptr` via `shared_from_this()`, keeping the object alive for the
 *  duration of any in-flight operation.
 *
 *  All mutable state is accessed only on `strand_`.  Both `run()` and `stop()`
 *  re-post to the strand when called from outside it, so callers need no
 *  external synchronisation.
 *
 *  @note Do not use this class directly; it is constructed and managed by
 *      `OverlayImpl::connect()`.
 */
class ConnectAttempt : public OverlayImpl::Child,
                       public std::enable_shared_from_this<ConnectAttempt>
{
private:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using request_type = boost::beast::http::request<boost::beast::http::empty_body>;
    using response_type = boost::beast::http::response<boost::beast::http::dynamic_body>;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    /** Phases of the outbound connection pipeline.
     *
     *  Tracks which async operation is currently in flight.  Used by the
     *  dual-timer scheme to emit phase-specific timeout diagnostics and by
     *  `tryAsyncShutdown()` to decide whether a TLS-level shutdown is needed.
     */
    enum class ConnectionStep {
        Init,            /**< Initial state; no async operation started yet. */
        TcpConnect,      /**< Async TCP connect in progress. */
        TlsHandshake,    /**< Async TLS handshake in progress. */
        HttpWrite,       /**< Async HTTP upgrade request write in progress. */
        HttpRead,        /**< Async HTTP upgrade response read in progress. */
        Complete,        /**< HTTP response received; pipeline finished. */
        ShutdownStarted  /**< Async TLS shutdown in progress. */
    };

    /** Hard ceiling for the entire connection attempt (all phases combined). */
    static constexpr std::chrono::seconds kCONNECT_TIMEOUT{25};

    /** Per-phase timeout budgets used by the step timer.
     *
     *  The step timer is reset at each phase transition.  A phase that
     *  exceeds its individual budget is terminated even when the global
     *  `kCONNECT_TIMEOUT` has not yet elapsed, providing fine-grained
     *  diagnostics for slow individual phases.
     */
    struct StepTimeouts
    {
        static constexpr std::chrono::seconds kTCP_CONNECT{8};   /**< TCP connect budget. */
        static constexpr std::chrono::seconds kTLS_HANDSHAKE{8}; /**< TLS handshake budget. */
        static constexpr std::chrono::seconds kHTTP_WRITE{3};    /**< HTTP write budget. */
        static constexpr std::chrono::seconds kHTTP_READ{3};     /**< HTTP read budget. */
        static constexpr std::chrono::seconds kTLS_SHUTDOWN{2};  /**< TLS shutdown budget. */
    };

    // --- Core components ---

    Application& app_;
    Peer::id_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    endpoint_type remoteEndpoint_;
    Resource::Consumer usage_;

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    /** Global hard-limit timer; armed once when `run()` is first called. */
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;

    /** Per-phase timer; reset at every phase transition via `setTimer()`. */
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> stepTimer_;

    /** Owned TLS stream; moved into `PeerImp` on successful promotion. */
    std::unique_ptr<stream_type> streamPtr_;

    /** Convenience reference into `streamPtr_->next_layer().socket()`. */
    socket_type& socket_;

    /** Convenience reference into `*streamPtr_`. */
    stream_type& stream_;

    boost::beast::multi_buffer readBuf_;

    response_type response_;

    /** PeerFinder slot reserved for this outbound connection.
     *
     *  Moved into the `PeerImp` on success, leaving this member null.
     *  The destructor calls `peerFinder().onClosed(slot_)` only when
     *  `slot_` is non-null, ensuring exactly-once release.
     */
    std::shared_ptr<PeerFinder::Slot> slot_;

    request_type req_;

    /** True once `stop()` or a failure handler sets the shutdown flag. */
    bool shutdown_ = false;

    /** True while an async I/O operation is outstanding on `stream_`.
     *
     *  `tryAsyncShutdown()` defers the SSL shutdown handshake until this
     *  flag is false, because calling `stream_.async_shutdown()` while
     *  another async operation is in flight is undefined behaviour.
     */
    bool ioPending_ = false;

    ConnectionStep currentStep_ = ConnectionStep::Init;

public:
    /** Construct a `ConnectAttempt` and bind it to its `PeerFinder` slot.
     *
     *  Initializes the TLS stream from the shared SSL context and wires
     *  convenience references (`socket_`, `stream_`) into `streamPtr_`.
     *  Call `run()` to begin the async pipeline.
     *
     *  @param app            Application context providing config and services.
     *  @param ioContext      Asio I/O context for all async operations.
     *  @param remoteEndpoint TCP endpoint of the peer to dial.
     *  @param usage          Resource consumer used for rate-limiting this peer.
     *  @param context        Shared SSL context (TLS version, ciphers, etc.).
     *  @param id             Unique numeric identifier for this connection attempt.
     *  @param slot           PeerFinder slot reserved for this outbound
     *      connection; must not be null.
     *  @param journal        Journal sink for diagnostic logging.
     *  @param overlay        Parent overlay manager that owns this child.
     */
    ConnectAttempt(
        Application& app,
        boost::asio::io_context& ioContext,
        endpoint_type remoteEndpoint,
        Resource::Consumer usage,
        shared_context const& context,
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        OverlayImpl& overlay);

    /** Release the `PeerFinder` slot if the connection never succeeded.
     *
     *  On a successful connection `processResponse()` moves `slot_` into the
     *  newly created `PeerImp`, leaving it null here.  Any other outcome
     *  (failure, timeout, or external stop) leaves `slot_` non-null, so the
     *  destructor reports the closure to `PeerFinder` to keep its
     *  bookkeeping consistent.
     */
    ~ConnectAttempt() override;

    /** Abort the connection attempt from any thread.
     *
     *  If called from outside the strand, the call is re-posted so that the
     *  actual teardown always runs with strand ownership.  Idempotent if
     *  the socket is already closed.
     */
    void
    stop() override;

    /** Begin the async connection pipeline from any thread.
     *
     *  Re-posts to the strand if necessary, then arms both the global ceiling
     *  timer and the `TcpConnect` step timer before issuing the async TCP
     *  connect.  `onConnect()` continues the pipeline on success.
     */
    void
    run();

private:
    /** Advance the current phase and arm the appropriate timers.
     *
     *  The global timer (`timer_`) is armed exactly once for the lifetime of
     *  the attempt (guarded by checking against the epoch `time_point{}`).
     *  The step timer (`stepTimer_`) is reset on every call via
     *  `expires_after`, which implicitly cancels the previous step timer.
     *  Both timers share `onTimer()` as their completion handler.
     *
     *  `Init` and `Complete` steps do not arm a step timer.  On any
     *  exception from the Asio timer API, `close()` is called immediately.
     *
     *  @param step The phase being entered; updates `currentStep_`.
     */
    void
    setTimer(ConnectionStep step);

    /** Cancel both the global timer and the current step timer.
     *
     *  Any `system_error` thrown by `cancel()` is swallowed; timers that
     *  are already expired or closed produce benign errors that must not
     *  propagate.
     */
    void
    cancelTimer();

    /** Shared completion handler for both the global and step timers.
     *
     *  `operation_aborted` is returned whenever a timer is cancelled (e.g.
     *  by `expires_after` or `cancelTimer()`); this is expected and ignored.
     *  On a genuine expiry (ec == 0) the handler compares both timers'
     *  expiry times against `steady_clock::now()` to identify which fired —
     *  the global ceiling or the per-step budget — and logs accordingly
     *  before calling `close()`.
     *
     *  @param ec Error code from the Asio timer wait.
     */
    void
    onTimer(error_code ec);

    /** Handle completion of the async TCP connect.
     *
     *  On success, disables TLS certificate verification (`verify_none`) and
     *  starts the async TLS handshake.  Security derives from the node-key
     *  signature in the HTTP headers, not the cert chain.
     *
     *  @param ec Error code from the async TCP connect.
     */
    void
    onConnect(error_code ec);

    /** Handle completion of the async TLS handshake.
     *
     *  On success: confirms the connection is not a self-loop via
     *  `peerFinder().onConnected()`; derives the TLS-channel-bound shared
     *  value with `makeSharedValue()` (a degenerate zero-XOR result causes
     *  immediate shutdown); builds and sends the HTTP upgrade request with
     *  identity headers via `buildHandshake()`.
     *
     *  @param ec Error code from the async TLS handshake.
     */
    void
    onHandshake(error_code ec);

    /** Handle completion of the async HTTP upgrade request write.
     *
     *  On success, arms the `HttpRead` step timer and starts the async read
     *  of the peer's HTTP upgrade response.
     *
     *  @param ec Error code from the async HTTP write.
     */
    void
    onWrite(error_code ec);

    /** Handle completion of the async HTTP upgrade response read.
     *
     *  Cancels all timers and advances `currentStep_` to `Complete` before
     *  inspecting the error code.  An `eof` here means the peer closed the
     *  connection before sending a full response and is treated as a soft
     *  failure.  On success, delegates to `processResponse()`.
     *
     *  @param ec Error code from the async HTTP read.
     */
    void
    onRead(error_code ec);

    /** Log a failure reason and begin connection teardown.
     *
     *  @param reason Human-readable description of why the connection failed.
     */
    void
    fail(std::string const& reason);

    /** Log a system error and begin connection teardown.
     *
     *  @param name Context label prepended to the error message (e.g.
     *      `"onConnect"`).
     *  @param ec   The system error code describing the failure.
     */
    void
    fail(std::string const& name, error_code ec);

    /** Mark the connection as shutting down and begin teardown.
     *
     *  Sets `shutdown_` to prevent further protocol steps, cancels all
     *  pending async I/O on the lowest-layer socket, then delegates to
     *  `tryAsyncShutdown()`.
     *
     *  @note Must be called from the strand.  Idempotent when the socket is
     *      already closed.
     */
    void
    shutdown();

    /** Initiate the TLS shutdown handshake when it is safe to do so.
     *
     *  This method is a no-op when shutdown has not been requested, when
     *  shutdown is already in progress, or when `ioPending_` is true.  The
     *  last guard prevents calling `stream_.async_shutdown()` while a
     *  concurrent async read or write is outstanding, which would be
     *  undefined behaviour.
     *
     *  If the TLS handshake never completed (`TcpConnect` or `TlsHandshake`
     *  phase), there is no TLS session to close, so `close()` is called
     *  directly instead.
     *
     *  @note Must be called from the strand.
     */
    void
    tryAsyncShutdown();

    /** Handle completion of the async TLS shutdown handshake.
     *
     *  Several error codes are expected during teardown and suppressed to
     *  avoid log noise: `eof`, `operation_aborted`, `stream_truncated`, and
     *  the OpenSSL `"application data after close notify"` condition.  Any
     *  other error is logged at debug level.  In all cases `close()` is
     *  called to finalize socket teardown.
     *
     *  @param ec Error code from the async TLS shutdown.
     */
    void
    onShutdown(error_code ec);

    /** Cancel all timers and close the underlying TCP socket.
     *
     *  Terminal cleanup step called after TLS shutdown or when bypassing it
     *  (pre-handshake teardown).  The error code from `socket_.close()` is
     *  deliberately discarded.
     *
     *  @note Must be called from the strand.  Idempotent when the socket is
     *      already closed.
     */
    void
    close();

    /** Validate the peer's HTTP upgrade response and promote to a live peer.
     *
     *  **HTTP 101 (Switching Protocols)** — normal success path:
     *  Negotiates exactly one locally-supported protocol version from the
     *  `Upgrade` header; re-derives the TLS-channel-bound shared value;
     *  calls `verifyHandshake()` to authenticate the remote node's public key
     *  (throws `std::runtime_error` on any check failure); calls
     *  `peerFinder().activate()` to confirm slot acceptability; then moves
     *  `streamPtr_` and `slot_` into a new `PeerImp` and hands it to
     *  `overlay_.addActive()`.
     *
     *  **HTTP 503 (Service Unavailable)** — redirect path:
     *  If the body contains a valid `"peer-ips"` JSON array, the endpoints
     *  are forwarded to `peerFinder().onRedirects()` for future connection
     *  attempts.  A 503 without a valid redirect body is logged as a warning.
     *  In either case this `ConnectAttempt` terminates.
     *
     *  Any other HTTP status code is treated as a hard failure.
     *
     *  @note Once `streamPtr_` and `slot_` are moved out, the destructor's
     *      `onClosed` guard is a no-op; ownership has transferred to the
     *      new `PeerImp`.
     */
    void
    processResponse();

    /** Convert a `ConnectionStep` enumerator to a human-readable string.
     *
     *  Used in log messages emitted by `onTimer()` and `setTimer()`.
     *
     *  @param step The phase to stringify.
     *  @return A string literal naming the phase (e.g. `"TcpConnect"`).
     */
    static std::string
    stepToString(ConnectionStep step)
    {
        switch (step)
        {
            case ConnectionStep::Init:
                return "Init";
            case ConnectionStep::TcpConnect:
                return "TcpConnect";
            case ConnectionStep::TlsHandshake:
                return "TlsHandshake";
            case ConnectionStep::HttpWrite:
                return "HttpWrite";
            case ConnectionStep::HttpRead:
                return "HttpRead";
            case ConnectionStep::Complete:
                return "Complete";
            case ConnectionStep::ShutdownStarted:
                return "ShutdownStarted";
        }
        return "Unknown";
    };

    /** Parse an IP:port string into a Boost.Asio TCP endpoint.
     *
     *  Used by `processResponse()` to decode the `"peer-ips"` redirect
     *  array from a 503 response body.  Parsing is performed via
     *  `beast::IP::Endpoint` and then converted to Asio's representation.
     *
     *  @param s  The endpoint string in `"address:port"` format.
     *  @param ec Set to `invalid_argument` if the string cannot be parsed;
     *      left unchanged on success.
     *  @return The parsed endpoint, or a default-constructed endpoint if
     *      parsing fails.
     */
    template <class = void>
    static boost::asio::ip::tcp::endpoint
    parseEndpoint(std::string const& s, boost::system::error_code& ec)
    {
        beast::IP::Endpoint bep;
        std::istringstream is(s);
        is >> bep;
        if (is.fail())
        {
            ec = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
            return boost::asio::ip::tcp::endpoint{};
        }

        return beast::IPAddressConversion::toAsioEndpoint(bep);
    }
};

}  // namespace xrpl
