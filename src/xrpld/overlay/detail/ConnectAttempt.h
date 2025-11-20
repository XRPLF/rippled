#ifndef XRPL_OVERLAY_CONNECTATTEMPT_H_INCLUDED
#define XRPL_OVERLAY_CONNECTATTEMPT_H_INCLUDED

#include <xrpld/overlay/detail/OverlayImpl.h>

#include <chrono>

namespace ripple {

/**
 * @class ConnectAttempt
 * @brief Manages outbound peer connection attempts with comprehensive timeout
 * handling
 *
 * The ConnectAttempt class handles the complete lifecycle of establishing an
 * outbound connection to a peer in the XRPL network. It implements a
 * sophisticated dual-timer system that provides both global timeout protection
 * and per-step timeout diagnostics.
 *
 * The connection establishment follows these steps:
 * 1. **TCP Connect**: Establish basic network connection
 * 2. **TLS Handshake**: Negotiate SSL/TLS encryption
 * 3. **HTTP Write**: Send peer handshake request
 * 4. **HTTP Read**: Receive and validate peer response
 * 5. **Complete**: Connection successfully established
 *
 * Uses a hybrid timeout approach:
 * - **Global Timer**: Hard limit (20s) for entire connection process
 * - **Step Timers**: Individual timeouts for each connection phase
 *
 * - All errors result in connection termination
 *
 * All operations are serialized using boost::asio::strand to ensure thread
 * safety. The class is designed to be used exclusively within the ASIO event
 * loop.
 *
 * @note This class should not be used directly. It is managed by OverlayImpl
 *       as part of the peer discovery and connection management system.
 *
 */
class ConnectAttempt : public OverlayImpl::Child,
                       public std::enable_shared_from_this<ConnectAttempt>
{
private:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using request_type =
        boost::beast::http::request<boost::beast::http::empty_body>;
    using response_type =
        boost::beast::http::response<boost::beast::http::dynamic_body>;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;

    /**
     * @enum ConnectionStep
     * @brief Represents the current phase of the connection establishment
     * process
     *
     * Used for tracking progress and providing detailed timeout diagnostics.
     * Each step has its own timeout value defined in StepTimeouts.
     */
    enum class ConnectionStep {
        Init,            // Initial state, nothing started
        TcpConnect,      // Establishing TCP connection to remote peer
        TlsHandshake,    // Performing SSL/TLS handshake
        HttpWrite,       // Sending HTTP upgrade request
        HttpRead,        // Reading HTTP upgrade response
        Complete,        // Connection successfully established
        ShutdownStarted  // Connection shutdown has started
    };

    // A timeout for connection process, greater than all step timeouts
    static constexpr std::chrono::seconds connectTimeout{25};

    /**
     * @struct StepTimeouts
     * @brief Defines timeout values for each connection step
     *
     * These timeouts are designed to detect slow individual phases while
     * allowing the global timeout to enforce the overall time limit.
     */
    struct StepTimeouts
    {
        // TCP connection timeout
        static constexpr std::chrono::seconds tcpConnect{8};
        // SSL handshake timeout
        static constexpr std::chrono::seconds tlsHandshake{8};
        // HTTP write timeout
        static constexpr std::chrono::seconds httpWrite{3};
        // HTTP read timeout
        static constexpr std::chrono::seconds httpRead{3};
        // SSL shutdown timeout
        static constexpr std::chrono::seconds tlsShutdown{2};
    };

    // Core application and networking components
    Application& app_;
    Peer::id_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    endpoint_type remote_endpoint_;
    Resource::Consumer usage_;

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> stepTimer_;

    std::unique_ptr<stream_type> stream_ptr_;  // SSL stream (owned)
    socket_type& socket_;
    stream_type& stream_;
    boost::beast::multi_buffer read_buf_;

    response_type response_;
    std::shared_ptr<PeerFinder::Slot> slot_;
    request_type req_;

    bool shutdown_ = false;   // Shutdown has been initiated
    bool ioPending_ = false;  // Async I/O operation in progress
    ConnectionStep currentStep_ = ConnectionStep::Init;

public:
    /**
     * @brief Construct a new ConnectAttempt object
     *
     * @param app Application context providing configuration and services
     * @param io_context ASIO I/O context for async operations
     * @param remote_endpoint Target peer endpoint to connect to
     * @param usage Resource usage tracker for rate limiting
     * @param context Shared SSL context for encryption
     * @param id Unique peer identifier for this connection attempt
     * @param slot PeerFinder slot representing this connection
     * @param journal Logging interface for diagnostics
     * @param overlay Parent overlay manager
     *
     * @note The constructor only initializes the object. Call run() to begin
     *       the actual connection attempt.
     */
    ConnectAttempt(
        Application& app,
        boost::asio::io_context& io_context,
        endpoint_type const& remote_endpoint,
        Resource::Consumer usage,
        shared_context const& context,
        Peer::id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        beast::Journal journal,
        OverlayImpl& overlay);

    ~ConnectAttempt();

    /**
     * @brief Stop the connection attempt
     *
     * This method is thread-safe and can be called from any thread.
     */
    void
    stop() override;

    /**
     * @brief Begin the connection attempt
     *
     * This method is thread-safe and posts to the strand if needed.
     */
    void
    run();

private:
    /**
     * @brief Set timers for the specified connection step
     *
     * @param step The connection step to set timers for
     *
     * Sets both the step-specific timer and the global timer (if not already
     * set).
     */
    void
    setTimer(ConnectionStep step);

    /**
     * @brief Cancel both global and step timers
     *
     * Used during cleanup and when connection completes successfully.
     * Exceptions from timer cancellation are safely ignored.
     */
    void
    cancelTimer();

    /**
     * @brief Handle timer expiration events
     *
     * @param ec Error code from timer operation
     *
     * Determines which timer expired (global vs step) and logs appropriate
     * diagnostic information before terminating the connection.
     */
    void
    onTimer(error_code ec);

    // Connection phase handlers
    void
    onConnect(error_code ec);  // TCP connection completion handler
    void
    onHandshake(error_code ec);  // TLS handshake completion handler
    void
    onWrite(error_code ec);  // HTTP write completion handler
    void
    onRead(error_code ec);  // HTTP read completion handler

    // Error and cleanup handlers
    void
    fail(std::string const& reason);  // Fail with custom reason
    void
    fail(std::string const& name, error_code ec);  // Fail with system error
    void
    shutdown();  // Initiate graceful shutdown
    void
    tryAsyncShutdown();  // Attempt async SSL shutdown
    void
    onShutdown(error_code ec);  // SSL shutdown completion handler
    void
    close();  // Force close socket

    /**
     * @brief Process the HTTP upgrade response from peer
     *
     * Validates the peer's response, extracts protocol information,
     * verifies handshake, and either creates a PeerImp or handles
     * redirect responses.
     */
    void
    processResponse();

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

    template <class = void>
    static boost::asio::ip::tcp::endpoint
    parse_endpoint(std::string const& s, boost::system::error_code& ec)
    {
        beast::IP::Endpoint bep;
        std::istringstream is(s);
        is >> bep;
        if (is.fail())
        {
            ec = boost::system::errc::make_error_code(
                boost::system::errc::invalid_argument);
            return boost::asio::ip::tcp::endpoint{};
        }

        return beast::IPAddressConversion::to_asio_endpoint(bep);
    }
};

}  // namespace ripple

#endif
