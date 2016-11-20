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

#ifndef RIPPLE_WEBSOCKET_SERVER_H_INCLUDED
#define RIPPLE_WEBSOCKET_SERVER_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/core/ThreadEntry.h>
#include <ripple/beast/core/Thread.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace ripple {
namespace websocket {

template <class WebSocket>
class Server : public Stoppable
{
private:
    ServerDescription desc_;
    std::recursive_mutex endpointMutex_;  // TODO: why is this recursive?
    std::thread thread_;
    beast::Journal j_;
    typename WebSocket::EndpointPtr endpoint_;
public:
    Server (ServerDescription const& desc)
        : Stoppable (WebSocket::versionName(), desc.source)
        , desc_(desc)
        , j_ (desc.app.journal ("WebSocket"))
    {
    }

    ~Server ()
    {
        assert (!thread_.joinable());

        if (thread_.joinable())
            LogicError ("WebSocket::Server::onStop not called.");
    }

private:
    void run ()
    {
        threadEntry (
            this, &Server::runImpl, "Server<WebSocket>::run()");
    }

    void runImpl ()
    {
        beast::Thread::setCurrentThreadName ("WebSocket");

        JLOG (j_.warn())
            << "Websocket: listening on " << desc_.port;

        try
        {
            listen();
        }
        catch (std::exception const& e)
        {
            JLOG (j_.warn()) <<
                "Websocket: failed to listen on " <<
                desc_.port << ": " << e.what();
        }

        {
            std::lock_guard<std::recursive_mutex> lock (endpointMutex_);
            endpoint_.reset();
        }

        JLOG (j_.warn())
            << "Websocket: finished listening on " << desc_.port;

        stopped ();
        JLOG (j_.warn())
            << "Websocket: stopped on " << desc_.port;
    }

    void onStart () override
    {
        JLOG (j_.warn())
                << "Websocket: creating endpoint " << desc_.port;

        {
            auto handler = WebSocket::makeHandler (desc_);
            std::lock_guard<std::recursive_mutex> lock (endpointMutex_);
            endpoint_ = WebSocket::makeEndpoint (std::move (handler));
        }

        thread_ = std::thread {&Server<WebSocket>::run, this};

        auto ep = [&]
        {
            std::lock_guard<std::recursive_mutex> lock (endpointMutex_);
            return endpoint_;
        }();

        if (ep)
            ep->wait_for_listen();
    }

    void onStop () override
    {
        JLOG (j_.warn())
            << "Websocket: onStop " << desc_.port;

        auto endpoint = [&]
        {
            std::lock_guard<std::recursive_mutex> lock (endpointMutex_);
            return endpoint_;
        }();

        if (endpoint)
            endpoint->stop ();

        thread_.join();
    }

    void listen();
};

} // websocket
} // ripple

#endif
