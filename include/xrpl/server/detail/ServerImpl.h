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

#ifndef XRPL_SERVER_SERVERIMPL_H_INCLUDED
#define XRPL_SERVER_SERVERIMPL_H_INCLUDED

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/core/List.h>
#include <xrpl/server/detail/Door.h>
#include <xrpl/server/detail/io_list.h>

#include <boost/asio.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace ripple {

using Endpoints =
    std::unordered_map<std::string, boost::asio::ip::tcp::endpoint>;

/** A multi-protocol server.

    This server maintains multiple configured listening ports,
    with each listening port allows for multiple protocols including
    HTTP, HTTP/S, WebSocket, Secure WebSocket, and the Peer protocol.
*/
class Server
{
public:
    /** Destroy the server.
        The server is closed if it is not already closed. This call
        blocks until the server has stopped.
    */
    virtual ~Server() = default;

    /** Returns the Journal associated with the server. */
    virtual beast::Journal
    journal() = 0;

    /** Set the listening port settings.
        This may only be called once.
    */
    virtual Endpoints
    ports(std::vector<Port> const& v) = 0;

    /** Close the server.
        The close is performed asynchronously. The handler will be notified
        when the server has stopped. The server is considered stopped when
        there are no pending I/O completion handlers and all connections
        have closed.
        Thread safety:
            Safe to call concurrently from any thread.
    */
    virtual void
    close() = 0;
};

template <class Handler>
class ServerImpl : public Server
{
private:
    using clock_type = std::chrono::system_clock;

    enum { historySize = 100 };

    Handler& handler_;
    beast::Journal const j_;
    boost::asio::io_context& io_context_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;

    std::mutex m_;
    std::vector<Port> ports_;
    std::vector<std::weak_ptr<Door<Handler>>> list_;
    int high_ = 0;
    std::array<std::size_t, 64> hist_;

    io_list ios_;

public:
    ServerImpl(
        Handler& handler,
        boost::asio::io_context& io_context,
        beast::Journal journal);

    ~ServerImpl();

    beast::Journal
    journal() override
    {
        return j_;
    }

    Endpoints
    ports(std::vector<Port> const& ports) override;

    void
    close() override;

    io_list&
    ios()
    {
        return ios_;
    }

    boost::asio::io_context&
    get_io_context()
    {
        return io_context_;
    }

    bool
    closed();

private:
    static int
    ceil_log2(unsigned long long x);
};

template <class Handler>
ServerImpl<Handler>::ServerImpl(
    Handler& handler,
    boost::asio::io_context& io_context,
    beast::Journal journal)
    : handler_(handler)
    , j_(journal)
    , io_context_(io_context)
    , strand_(boost::asio::make_strand(io_context_))
    , work_(std::in_place, boost::asio::make_work_guard(io_context_))
{
}

template <class Handler>
ServerImpl<Handler>::~ServerImpl()
{
    // Handler::onStopped will not be called
    work_ = std::nullopt;
    ios_.close();
    ios_.join();
}

template <class Handler>
Endpoints
ServerImpl<Handler>::ports(std::vector<Port> const& ports)
{
    if (closed())
        Throw<std::logic_error>("ports() on closed Server");
    ports_.reserve(ports.size());
    Endpoints eps;
    eps.reserve(ports.size());
    for (auto const& port : ports)
    {
        ports_.push_back(port);
        auto& internalPort = ports_.back();
        if (auto sp = ios_.emplace<Door<Handler>>(
                handler_, io_context_, internalPort, j_))
        {
            list_.push_back(sp);

            auto ep = sp->get_endpoint();
            if (!internalPort.port)
                internalPort.port = ep.port();
            eps.emplace(port.name, std::move(ep));

            sp->run();
        }
    }
    return eps;
}

template <class Handler>
void
ServerImpl<Handler>::close()
{
    ios_.close([&] {
        work_ = std::nullopt;
        handler_.onStopped(*this);
    });
}

template <class Handler>
bool
ServerImpl<Handler>::closed()
{
    return ios_.closed();
}
}  // namespace ripple

#endif
