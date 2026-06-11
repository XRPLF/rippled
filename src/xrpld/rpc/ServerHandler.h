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
        Resource::Manager&,
        CollectorManager& cm);

public:
    // Must be public so make_unique can call it.
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
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint const& remoteAddress);

    Handoff
    onHandoff(
        Session& session,
        http_request_type&& request,  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        boost::asio::ip::tcp::endpoint const& remoteAddress)
    {
        return onHandoff(session, {}, std::forward<http_request_type>(request), remoteAddress);
    }

    void
    onRequest(Session& session);

    void
    onWSMessage(
        std::shared_ptr<WSSession> session,
        std::vector<boost::asio::const_buffer> const& buffers);

    void
    onClose(Session& session, boost::system::error_code const&);

    void
    onStopped(Server&);

private:
    json::Value
    processSession(
        std::shared_ptr<WSSession> const& session,
        std::shared_ptr<JobQueue::Coro> const& coro,
        json::Value const& jv);

    void
    processSession(std::shared_ptr<Session> const&, std::shared_ptr<JobQueue::Coro> coro);

    void
    processRequest(
        Port const& port,
        std::string const& request,
        beast::IP::Endpoint const& remoteIPAddress,
        Output const&,
        std::shared_ptr<JobQueue::Coro> coro,
        std::string_view forwardedFor,
        std::string_view user);

    [[nodiscard]] Handoff
    statusResponse(http_request_type const& request) const;
};

ServerHandler::Setup
setupServerHandler(Config const& c, std::ostream& log);

std::unique_ptr<ServerHandler>
makeServerHandler(
    Application& app,
    boost::asio::io_context&,
    JobQueue&,
    NetworkOPs&,
    Resource::Manager&,
    CollectorManager& cm);

}  // namespace xrpl
