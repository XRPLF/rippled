/** @file
 *  Declares ServerHandler, the central gateway between the TCP/TLS acceptor
 *  layer and the RPC / WebSocket processing pipeline.
 *
 *  ServerHandler owns the Beast-backed Server, enforces per-port connection
 *  limits, routes accepted connections to either the overlay peer protocol or
 *  the JSON-RPC subsystem, and dispatches all request work to the JobQueue as
 *  coroutines.
 */

#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/CollectorManager.h>
#include <xrpld/rpc/detail/WSInfoSub.h>

#include <xrpl/core/JobQueue.h>
#include <xrpl/json/Output.h>
#include <xrpl/server/Server.h>
#include <xrpl/server/Session.h>
#include <xrpl/server/WSSession.h>

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/utility/string_view.hpp>

#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

namespace xrpl {

/** Order Port objects by name, enabling use of Port references as map keys.
 *
 *  This ordering is required so that `std::reference_wrapper<Port const>`
 *  can serve as the key type in `ServerHandler::count_`.
 *
 *  @param lhs Left-hand Port.
 *  @param rhs Right-hand Port.
 *  @return True if `lhs.name` is lexicographically less than `rhs.name`.
 */
inline bool
operator<(Port const& lhs, Port const& rhs)
{
    return lhs.name < rhs.name;
}

/** Central gateway between the low-level TCP/TLS acceptor and the RPC pipeline.
 *
 *  ServerHandler owns the Beast-backed Server object, wires it to the
 *  JobQueue, and fulfils the duck-typed handler concept required by
 *  `make_Server<Handler>()`. It handles:
 *
 *  - Per-port connection-count limiting (onAccept / onClose).
 *  - Routing: WebSocket upgrades, overlay peer connections, and HTTP RPC
 *    (onHandoff / onRequest / onWSMessage).
 *  - Dispatching work as `jtCLIENT_RPC` / `jtCLIENT_WEBSOCKET` coroutines.
 *  - Blocking two-phase shutdown via stop() / onStopped().
 *  - Metrics: request count, payload size, and processing duration.
 *
 *  @note Instances must be created through makeServerHandler(); the constructor
 *      is public only so `std::make_unique` can call it, but the
 *      `ServerHandlerCreator` token argument cannot be satisfied by any code
 *      outside the factory.
 */
class ServerHandler
{
public:
    /** Configuration loaded from `rippled.cfg` for the server and its clients.
     *
     *  Populated by setupServerHandler() and passed to setup(). Call
     *  makeContexts() after parsing to instantiate SSL contexts for any port
     *  advertising `https`, `wss`, or similar protocols.
     */
    struct Setup
    {
        explicit Setup() = default;

        /** Ordered list of listening port descriptors parsed from config. */
        std::vector<Port> ports;

        // Memberspace
        /** Outgoing-RPC client credentials used when the process calls itself
         *  or another node.
         */
        struct ClientT
        {
            explicit ClientT() = default;

            /** True if the outgoing connection should use TLS. */
            bool secure = false;
            /** Target IP address for outgoing RPC calls. */
            std::string ip;
            /** Target port for outgoing RPC calls; 0 = auto-resolved. */
            std::uint16_t port = 0;
            /** HTTP Basic-Auth username for outgoing calls. */
            std::string user;
            /** HTTP Basic-Auth password for outgoing calls. */
            std::string password;
            /** Admin username forwarded in outgoing calls. */
            std::string admin_user;
            /** Admin password forwarded in outgoing calls. */
            std::string admin_password;
        };

        /** Configuration when acting in client role */
        ClientT client;

        /** Bind endpoint for the overlay peer listener. */
        boost::asio::ip::tcp::endpoint overlay;

        /** Instantiate SSL contexts for all ports that advertise a TLS protocol.
         *
         *  Must be called once after the Setup is fully populated and before
         *  it is passed to ServerHandler::setup().
         */
        void
        makeContexts();
    };

private:
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;

    Application& app_;
    Resource::Manager& resourceManager_;
    beast::Journal journal_;
    NetworkOPs& networkOPs_;
    std::unique_ptr<Server> server_;
    Setup setup_;
    Endpoints endpoints_;
    JobQueue& jobQueue_;
    beast::insight::Counter rpc_requests_;
    beast::insight::Event rpc_size_;
    beast::insight::Event rpc_time_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stopped_{false};
    std::map<std::reference_wrapper<Port const>, int> count_;

    // A private type used to restrict access to the ServerHandler constructor.
    struct ServerHandlerCreator
    {
        explicit ServerHandlerCreator() = default;
    };

    // Friend declaration that allows make_ServerHandler to access the
    // private type that restricts access to the ServerHandler ctor.
    friend std::unique_ptr<ServerHandler>
    makeServerHandler(
        Application& app,
        boost::asio::io_context&,
        JobQueue&,
        NetworkOPs&,
        Resource::Manager&,
        CollectorManager& cm);

public:
    /** Construct a ServerHandler via the capability-key idiom.
     *
     *  The `ServerHandlerCreator` parameter is an unpublished type whose only
     *  constructor is private; it can only be created inside makeServerHandler(),
     *  which is declared as a friend. This pattern keeps the constructor `public`
     *  (required for `std::make_unique`) while preventing arbitrary callers from
     *  instantiating the class directly.
     *
     *  Three metrics are registered with the `"rpc"` group of `cm`:
     *  `requests` (counter), `size` (event), and `time` (event).
     *
     *  @param creator  Capability token — only obtainable inside makeServerHandler().
     *  @param app      The running Application instance.
     *  @param ioContext Boost.Asio I/O context for the underlying Beast Server.
     *  @param jobQueue Job queue for dispatching RPC coroutines.
     *  @param networkOPs Network operations interface.
     *  @param resourceManager Resource charge and disconnect tracking.
     *  @param cm       CollectorManager used to register RPC metrics.
     */
    ServerHandler(
        ServerHandlerCreator const&,
        Application& app,
        boost::asio::io_context& ioContext,
        JobQueue& jobQueue,
        NetworkOPs& networkOPs,
        Resource::Manager& resourceManager,
        CollectorManager& cm);

    ~ServerHandler();

    using Output = json::Output;

    /** Apply port configuration and start listening.
     *
     *  Stores the setup, calls `server_->ports()` to obtain actual bound
     *  endpoints, and back-fills any auto-assigned port numbers (port == 0)
     *  into `setup_.ports`, `setup_.client.port`, and `setup_.overlay`.
     *
     *  @param setup   Port descriptors and client/overlay config.
     *  @param journal Journal used for subsequent server-level logging.
     */
    void
    setup(Setup const& setup, beast::Journal journal);

    /** Return the active port configuration.
     *
     *  @return Reference to the Setup stored by the last call to setup().
     */
    [[nodiscard]] Setup const&
    setup() const
    {
        return setup_;
    }

    /** Return the resolved listen endpoints keyed by port name.
     *
     *  @return Reference to the Endpoints map populated by setup().
     */
    [[nodiscard]] Endpoints const&
    endpoints() const
    {
        return endpoints_;
    }

    /** Begin shutdown and block until all sessions have closed.
     *
     *  Calls `server_->close()` to initiate asynchronous teardown, then waits
     *  on `condition_` until onStopped() sets `stopped_ = true`. Callers are
     *  guaranteed that no in-flight I/O references ServerHandler members after
     *  this function returns.
     */
    void
    stop();

    //
    // Handler
    //

    /** Accept or reject a new inbound TCP connection.
     *
     *  Atomically increments the per-port live-connection counter and rejects
     *  the connection (returns false) if the port's configured limit has been
     *  reached. The mutex is held during the increment because accepts can fire
     *  concurrently on multiple I/O threads.
     *
     *  @param session  The newly accepted session.
     *  @param endpoint Remote TCP endpoint of the connecting client.
     *  @return True to accept the connection; false to drop it immediately.
     */
    bool
    onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint);

    /** Route an HTTP connection to WebSocket, overlay peer, or HTTP RPC.
     *
     *  Decision logic:
     *  - WebSocket upgrade request → promotes the session via
     *    `session.websocketUpgrade()`, attaches a WSInfoSub to
     *    `WSSession::appDefined`, and returns a moved Handoff.
     *  - SSL bundle present and port carries `peer` protocol → forwarded to
     *    the Overlay via `app_.getOverlay().onHandoff()`.
     *  - Bare GET `/` on a WebSocket-only port → answered immediately with
     *    statusResponse() (HTTP 200 with build info).
     *  - Otherwise → empty Handoff, signalling the server to call onRequest().
     *
     *  @param session       The incoming session.
     *  @param bundle        SSL stream bundle (non-null for TLS connections).
     *  @param request       The parsed HTTP request.
     *  @param remoteAddress Remote TCP endpoint.
     *  @return A Handoff describing how the connection was routed.
     */
    Handoff
    onHandoff(
        Session& session,
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint const& remoteAddress);

    /** Overload for plain (non-TLS) connections; forwards to the TLS overload.
     *
     *  @param session       The incoming session.
     *  @param request       The parsed HTTP request.
     *  @param remoteAddress Remote TCP endpoint.
     *  @return A Handoff describing how the connection was routed.
     */
    Handoff
    onHandoff(
        Session& session,
        http_request_type&& request,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        boost::asio::ip::tcp::endpoint const& remoteAddress)
    {
        return onHandoff(session, {}, std::forward<http_request_type>(request), remoteAddress);
    }

    /** Handle an HTTP RPC request.
     *
     *  Verifies that the port has `http`/`https` in its protocol set and that
     *  the Basic-Auth header matches the port's credentials (403 otherwise).
     *  The session is then detached and a `jtCLIENT_RPC` coroutine is posted
     *  to the JobQueue. If the queue is closed (node shutting down), a 503 is
     *  sent and the session is closed immediately.
     *
     *  @param session The HTTP session carrying the RPC request body.
     */
    void
    onRequest(Session& session);

    /** Handle an incoming WebSocket text frame.
     *
     *  JSON parsing happens inline on the I/O thread. Frames that exceed
     *  `RPC::Tuning::kMAX_REQUEST_SIZE`, fail to parse, or are not JSON objects
     *  receive an immediate `jsonInvalid` error without touching the JobQueue.
     *  Valid messages are dispatched as `jtCLIENT_WEBSOCKET` coroutines. If the
     *  queue is full, the session is closed with a `going_away` frame.
     *
     *  @param session  The WebSocket session that received the frame.
     *  @param buffers  The raw frame data as a const-buffer sequence.
     */
    void
    onWSMessage(
        std::shared_ptr<WSSession> session,
        std::vector<boost::asio::const_buffer> const& buffers);

    /** Decrement the per-port connection counter when a session closes.
     *
     *  Mirrors onAccept(); both operations are performed under `mutex_`.
     *
     *  @param session The session that has closed.
     */
    void
    onClose(Session& session, boost::system::error_code const&);

    /** Signal that all sessions have closed after a server shutdown.
     *
     *  Sets `stopped_ = true` under `mutex_` and notifies `condition_` so
     *  that the thread blocked in stop() can return.
     *
     *  @param server The Server that has finished stopping (unused; required
     *      by the handler concept).
     */
    void
    onStopped(Server&);

private:
    /** Process a single WebSocket JSON-RPC request and return the response.
     *
     *  Checks the resource consumer's disconnect threshold, validates the
     *  request structure (command/method presence and API version), resolves
     *  the caller's role, and calls RPC::doCommand(). The response always
     *  carries `type: response` plus any `id`, `jsonrpc`, `ripplerpc`, and
     *  `api_version` fields echoed from the request. Sensitive fields
     *  (`passphrase`, `secret`, `seed`, `seed_hex`) are masked as `<masked>`
     *  in error response echoes.
     *
     *  @param session  The WebSocket session making the request.
     *  @param coro     The coroutine handle for cooperative yielding.
     *  @param jv       Parsed JSON request object.
     *  @return JSON response object ready to serialise and send.
     */
    json::Value
    processSession(
        std::shared_ptr<WSSession> const& session,
        std::shared_ptr<JobQueue::Coro> const& coro,
        json::Value const& jv);

    /** Process a single HTTP session request by delegating to processRequest().
     *
     *  Extracts the raw body, forwarded-for header, and X-User header, then
     *  calls processRequest(). Keeps the connection alive if the request
     *  includes a keep-alive header, otherwise closes it.
     *
     *  @param session  Detached HTTP session whose body contains the JSON request.
     *  @param coro     The coroutine handle for cooperative yielding.
     */
    void
    processSession(std::shared_ptr<Session> const&, std::shared_ptr<JobQueue::Coro> coro);

    /** HTTP JSON-RPC request engine — handles both single and batch requests.
     *
     *  A top-level object with `method == "batch"` is treated as a batch;
     *  `params` is iterated as an array of individual sub-requests and the
     *  results are accumulated into a JSON array. For each sub-request:
     *
     *  1. Resolve API version (fallback: `kAPI_VERSION_IF_UNSPECIFIED`).
     *  2. Determine required Role via RPC::roleRequired() and actual Role via
     *     requestRole() (IP ranges, admin credentials, secure-gateway headers).
     *  3. Allocate a Resource::Consumer (unlimited for admin, metered otherwise).
     *     Return 503 / per-item error if the consumer is already disconnected.
     *  4. Construct an RPC::JsonContext and call RPC::doCommand().
     *  5. Format response per `ripplerpc` version: v2+ separates success/error
     *     at the envelope level; v1 always nests under `result`.
     *  6. Mask `passphrase`, `secret`, `seed`, `seed_hex` in error echoes.
     *
     *  HTTP status: `ripplerpc >= "3.0"` maps ledger error codes to appropriate
     *  HTTP statuses; earlier versions always return 200.
     *
     *  @param port             The listening port the request arrived on.
     *  @param request          Raw request body string.
     *  @param remoteIPAddress  Caller's IP endpoint (port stripped to 0).
     *  @param output           Sink function for writing the HTTP response body.
     *  @param coro             Coroutine handle for cooperative yielding.
     *  @param forwardedFor     Value of the X-Forwarded-For header, if present.
     *  @param user             Value of the X-User header, if present.
     */
    void
    processRequest(
        Port const& port,
        std::string const& request,
        beast::IP::Endpoint const& remoteIPAddress,
        Output const&,
        std::shared_ptr<JobQueue::Coro> coro,
        std::string_view forwardedFor,
        std::string_view user);

    /** Build an HTTP 200 status response for bare GET / on a WebSocket port.
     *
     *  @param request The original HTTP request (version is preserved).
     *  @return A Handoff carrying the pre-built response writer.
     */
    [[nodiscard]] Handoff
    statusResponse(http_request_type const& request) const;
};

/** Parse `rippled.cfg` server and client sections into a ServerHandler::Setup.
 *
 *  Reads `[server]`, `[port_*]`, `[rpc_startup]`, and related sections from
 *  `c`. Parse warnings and errors are written to `log`.
 *
 *  @param c    The loaded configuration.
 *  @param log  Stream for diagnostic messages during parsing.
 *  @return A fully populated Setup ready to pass to ServerHandler::setup().
 */
ServerHandler::Setup
setupServerHandler(Config const& c, std::ostream& log);

/** Factory that constructs and returns a ServerHandler.
 *
 *  This is the only public way to create a ServerHandler. It synthesises the
 *  private `ServerHandlerCreator` token and forwards all arguments to the
 *  constructor.
 *
 *  @param app            The running Application instance.
 *  @param ioContext       Boost.Asio I/O context for the underlying Server.
 *  @param jobQueue        Job queue for dispatching RPC coroutines.
 *  @param networkOPs      Network operations interface.
 *  @param resourceManager Resource charge and disconnect tracking.
 *  @param cm              CollectorManager for registering RPC metrics.
 *  @return Owning pointer to the newly created ServerHandler.
 */
std::unique_ptr<ServerHandler>
makeServerHandler(
    Application& app,
    boost::asio::io_context&,
    JobQueue&,
    NetworkOPs&,
    Resource::Manager&,
    CollectorManager& cm);

}  // namespace xrpl
