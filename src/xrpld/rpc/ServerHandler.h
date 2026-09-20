#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/CollectorManager.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/WSInfoSub.h>

#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/Output.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/Port.h>
#include <xrpl/server/Server.h>  // IWYU pragma: keep
#include <xrpl/server/Session.h>
#include <xrpl/server/WSSession.h>

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/utility/string_view.hpp>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl {

inline bool
operator<(Port const& lhs, Port const& rhs)
{
    return lhs.name < rhs.name;
}

class ServerHandler
{
public:
    struct Setup
    {
        explicit Setup() = default;

        std::vector<Port> ports;

        // Memberspace
        struct ClientT
        {
            explicit ClientT() = default;

            bool secure = false;
            std::string ip;
            std::uint16_t port = 0;
            std::string user;
            std::string password;
            std::string adminUser;
            std::string adminPassword;
        };

        // Configuration when acting in client role
        ClientT client;

        // Configuration for the Overlay
        boost::asio::ip::tcp::endpoint overlay;

        void
        makeContexts();
    };

private:
    using SocketType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<SocketType>;

    Application& app_;
    resource::Manager& resourceManager_;
    beast::Journal journal_;
    NetworkOPs& networkOPs_;
    std::unique_ptr<Server> server_;
    Setup setup_;
    Endpoints endpoints_;
    JobQueue& jobQueue_;
    beast::insight::Counter rpcRequests_;
    beast::insight::Event rpcSize_;
    beast::insight::Event rpcTime_;
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
        resource::Manager&,
        CollectorManager& cm);

public:
    // Must be public so make_unique can call it.
    ServerHandler(
        ServerHandlerCreator const&,
        Application& app,
        boost::asio::io_context& ioContext,
        JobQueue& jobQueue,
        NetworkOPs& networkOPs,
        resource::Manager& resourceManager,
        CollectorManager& cm);

    ~ServerHandler();

    using Output = json::Output;

    void
    setup(Setup const& setup, beast::Journal journal);

    [[nodiscard]] Setup const&
    setup() const
    {
        return setup_;
    }

    [[nodiscard]] Endpoints const&
    endpoints() const
    {
        return endpoints_;
    }

    void
    stop();

    //
    // Handler
    //

    bool
    onAccept(Session& session, boost::asio::ip::tcp::endpoint endpoint);

    Handoff
    onHandoff(
        Session& session,
        std::unique_ptr<StreamType>&& bundle,
        HttpRequestType&& request,
        boost::asio::ip::tcp::endpoint const& remoteAddress);

    Handoff
    onHandoff(
        Session& session,
        HttpRequestType&& request,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        boost::asio::ip::tcp::endpoint const& remoteAddress)
    {
        return onHandoff(session, {}, std::forward<HttpRequestType>(request), remoteAddress);
    }

    void
    onRequest(Session& session);

    /**
     * Receives one WebSocket frame.
     *
     * A frame that exceeds the request size limit, does not parse or is not
     * an object is answered here with `jsonInvalid` and the frame's `size`;
     * its body is never echoed, since an unparsed body has no fields to mask.
     * Any other frame is posted to the job queue and answered from
     * processSession.
     *
     * @param session The WebSocket session the frame arrived on.
     * @param buffers The frame's bytes.
     */
    void
    onWSMessage(
        std::shared_ptr<WSSession> session,
        std::vector<boost::asio::const_buffer> const& buffers);

    void
    onClose(Session& session, boost::system::error_code const&);

    void
    onStopped(Server&);

private:
    /**
     * Serves one parsed WebSocket request.
     *
     * Closes the connection when its resource balance is past the drop
     * threshold. Otherwise checks the API version and the `command` and
     * `method` fields, dispatches through rpc::doCommand, charges the
     * session's consumer, and shapes the reply as a `response`, echoing the
     * masked request on an error.
     *
     * @param session The session the request arrived on.
     * @param coro The coroutine the request runs on.
     * @param jv The parsed request.
     * @return The reply to send.
     */
    json::Value
    processSession(
        std::shared_ptr<WSSession> const& session,
        std::shared_ptr<JobQueue::Coro> const& coro,
        json::Value const& jv);

    /**
     * Serves one HTTP request on a coroutine: hands the body, the client
     * address and the forwarding headers to processRequest, then completes
     * or closes the session as its keep-alive header asks.
     *
     * @param session The HTTP session the request arrived on.
     * @param coro The coroutine the request runs on.
     */
    void
    processSession(std::shared_ptr<Session> const& session, std::shared_ptr<JobQueue::Coro> coro);

    /**
     * Serves one HTTP body: parses it, answers a malformed or unauthorized
     * request with a plain-text status, serves a `method: "batch"` body entry
     * by entry, dispatches each request through rpc::doCommand, and writes
     * the reply with its HTTP status. An error reply echoes the masked
     * request.
     *
     * @param port The port the request arrived on, for its role and limits.
     * @param request The raw body.
     * @param remoteIPAddress The client address, for resource accounting and
     *         the role.
     * @param output Where the reply bytes are written.
     * @param coro The coroutine the request runs on.
     * @param forwardedFor The `X-Forwarded-For` header, when the port trusts
     *         a proxy.
     * @param user The `X-User` header.
     */
    void
    processRequest(
        Port const& port,
        std::string const& request,
        beast::ip::Endpoint const& remoteIPAddress,
        Output const&,
        std::shared_ptr<JobQueue::Coro> coro,
        std::string_view forwardedFor,
        std::string_view user);

    [[nodiscard]] Handoff
    statusResponse(HttpRequestType const& request) const;
};

ServerHandler::Setup
setupServerHandler(Config const& c, std::ostream& log);

std::unique_ptr<ServerHandler>
makeServerHandler(
    Application& app,
    boost::asio::io_context&,
    JobQueue&,
    NetworkOPs&,
    resource::Manager&,
    CollectorManager& cm);

}  // namespace xrpl
