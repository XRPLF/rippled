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

#ifndef RIPPLE_SERVER_SERVERHANDLERIMP_H_INCLUDED
#define RIPPLE_SERVER_SERVERHANDLERIMP_H_INCLUDED

#include <ripple/core/Job.h>
#include <ripple/core/JobCoro.h>
#include <ripple/json/Output.h>
#include <ripple/server/Handler.h>
#include <ripple/server/ServerHandler.h>
#include <ripple/server/Session.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/app/main/CollectorManager.h>
#include <map>
#include <mutex>

namespace ripple {

bool operator< (Port const& lhs, Port const& rhs)
{
    return lhs.name < rhs.name;
}

// Private implementation
class ServerHandlerImp
    : public ServerHandler
    , public Handler
{
private:
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
    std::mutex countlock_;
    std::map<std::reference_wrapper<Port const>, int> count_;

public:
    ServerHandlerImp (Application& app, Stoppable& parent,
        boost::asio::io_service& io_service, JobQueue& jobQueue,
            NetworkOPs& networkOPs, Resource::Manager& resourceManager,
                CollectorManager& cm);

    ~ServerHandlerImp();

private:
    using Output = Json::Output;

    void
    setup (Setup const& setup, beast::Journal journal) override;

    Setup const&
    setup() const override
    {
        return setup_;
    }

    //
    // Stoppable
    //

    void
    onStop() override;

    //
    // HTTP::Handler
    //

    bool
    onAccept (Session& session,
        boost::asio::ip::tcp::endpoint endpoint) override;

    Handoff
    onHandoff (Session& session,
        std::unique_ptr <beast::asio::ssl_bundle>&& bundle,
            beast::http::message&& request,
                boost::asio::ip::tcp::endpoint remote_address) override;

    Handoff
    onHandoff (Session& session, boost::asio::ip::tcp::socket&& socket,
        beast::http::message&& request,
            boost::asio::ip::tcp::endpoint remote_address) override;
    void
    onRequest (Session& session) override;

    void
    onClose (Session& session,
        boost::system::error_code const&) override;

    void
    onStopped (Server&) override;

    //--------------------------------------------------------------------------

    void
    processSession (std::shared_ptr<Session> const&,
        std::shared_ptr<JobCoro> jobCoro);

    void
    processRequest (Port const& port, std::string const& request,
        beast::IP::Endpoint const& remoteIPAddress, Output&&,
        std::shared_ptr<JobCoro> jobCoro,
        std::string forwardedFor, std::string user);

private:
    bool
    isWebsocketUpgrade (beast::http::message const& request);

    bool
    authorized (Port const& port,
        std::map<std::string, std::string> const& h);
};

}

#endif
