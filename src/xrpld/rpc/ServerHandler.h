#ifndef XRPL_RPC_SERVERHANDLER_H_INCLUDED
#define XRPL_RPC_SERVERHANDLER_H_INCLUDED

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/CollectorManager.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/rpc/detail/WSInfoSub.h>

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

namespace ripple {

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
        struct client_t
        {
            explicit client_t() = default;

            bool secure = false;
            std::string ip;
            std::uint16_t port = 0;
            std::string user;
            std::string password;
            std::string admin_user;
            std::string admin_password;
        };

        // Configuration when acting in client role
        client_t client;

        // Configuration for the Overlay
        boost::asio::ip::tcp::endpoint overlay;

        void
        makeContexts();
    };

private:
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;

    Application& app_;
    Resource::Manager& m_resourceManager;
    beast::Journal m_journal;
    NetworkOPs& m_networkOPs;
    std::unique_ptr<Server> m_server;
    Setup setup_;
    Endpoints endpoints_;
    JobQueue& m_jobQueue;
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
    make_ServerHandler(
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
        boost::asio::io_context& io_context,
        JobQueue& jobQueue,
        NetworkOPs& networkOPs,
        Resource::Manager& resourceManager,
        CollectorManager& cm);

    ~ServerHandler();

    using Output = Json::Output;

    void
    setup(Setup const& setup, beast::Journal journal);

    Setup const&
    setup() const
    {
        return setup_;
    }

    Endpoints const&
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
        boost::asio::ip::tcp::endpoint const& remote_address);

    Handoff
    onHandoff(
        Session& session,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint const& remote_address)
    {
        return onHandoff(
            session,
            {},
            std::forward<http_request_type>(request),
            remote_address);
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
    Json::Value
    processSession(
        std::shared_ptr<WSSession> const& session,
        std::shared_ptr<JobQueue::Coro> const& coro,
        Json::Value const& jv);

    void
    processSession(
        std::shared_ptr<Session> const&,
        std::shared_ptr<JobQueue::Coro> coro);

    void
    processRequest(
        Port const& port,
        std::string const& request,
        beast::IP::Endpoint const& remoteIPAddress,
        Output&&,
        std::shared_ptr<JobQueue::Coro> coro,
        std::string_view forwardedFor,
        std::string_view user);

    Handoff
    statusResponse(http_request_type const& request) const;
};

ServerHandler::Setup
setup_ServerHandler(Config const& c, std::ostream&& log);

std::unique_ptr<ServerHandler>
make_ServerHandler(
    Application& app,
    boost::asio::io_context&,
    JobQueue&,
    NetworkOPs&,
    Resource::Manager&,
    CollectorManager& cm);

}  // namespace ripple

#endif
