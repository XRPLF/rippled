//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_RPC_SERVERHANDLERIMP_H_INCLUDED
#define RIPPLE_RPC_SERVERHANDLERIMP_H_INCLUDED

#include <ripple/app/main/CollectorManager.h>
#include <ripple/core/JobQueue.h>
#include <ripple/json/Output.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/WSInfoSub.h>
#include <ripple/server/Server.h>
#include <ripple/server/Session.h>
#include <ripple/server/WSSession.h>
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

class ServerHandlerImp
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
        struct overlay_t
        {
            explicit overlay_t() = default;

            boost::asio::ip::address ip;
            std::uint16_t port = 0;
        };

        overlay_t overlay;

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
    JobQueue& m_jobQueue;
    beast::insight::Counter rpc_requests_;
    beast::insight::Event rpc_size_;
    beast::insight::Event rpc_time_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stopped_{false};
    std::map<std::reference_wrapper<Port const>, int> count_;

public:
    ServerHandlerImp(
        Application& app,
        boost::asio::io_service& io_service,
        JobQueue& jobQueue,
        NetworkOPs& networkOPs,
        Resource::Manager& resourceManager,
        CollectorManager& cm);

    ~ServerHandlerImp();

    using Output = Json::Output;

    void
    setup(Setup const& setup, beast::Journal journal);

    Setup const&
    setup() const
    {
        return setup_;
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
        boost::string_view forwardedFor,
        boost::string_view user);

    Handoff
    statusResponse(http_request_type const& request) const;
};

}  // namespace ripple

#endif
