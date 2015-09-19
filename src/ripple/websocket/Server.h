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

#ifndef RIPPLED_RIPPLE_WEBSOCKET_WSDOORBASE_H
#define RIPPLED_RIPPLE_WEBSOCKET_WSDOORBASE_H

#include <ripple/basics/Log.h>
#include <ripple/websocket/WebSocket.h>
#include <beast/cxx14/memory.h> // <memory>
#include <beast/threads/Thread.h>

namespace ripple {
namespace websocket {

template <class WebSocket>
class Server
    : public beast::Stoppable
    , protected beast::Thread
{
private:
    // TODO: why is this recursive?
    using LockType = std::recursive_mutex;
    using ScopedLockType = std::lock_guard <LockType>;

    ServerDescription desc_;
    LockType m_endpointLock;
    beast::Journal j_;
    typename WebSocket::EndpointPtr m_endpoint;

public:
    Server (ServerDescription const& desc)
       : beast::Stoppable (WebSocket::versionName(), desc.source)
        , Thread ("websocket")
        , desc_(desc)
       , j_ (desc.app.journal ("WebSocket"))
    {
        startThread ();
    }

    ~Server ()
    {
        stopThread ();
    }

private:
    void run () override
    {
        JLOG (j_.warning)
            << "Websocket: creating endpoint " << desc_.port;

        auto handler = WebSocket::makeHandler (desc_);
        {
            ScopedLockType lock (m_endpointLock);
            m_endpoint = WebSocket::makeEndpoint (std::move (handler));
        }

        JLOG (j_.warning)
            << "Websocket: listening on " << desc_.port;

        listen();
        {
            ScopedLockType lock (m_endpointLock);
            m_endpoint.reset();
        }

        JLOG (j_.warning)
            << "Websocket: finished listening on " << desc_.port;

        stopped ();
        JLOG (j_.warning)
            << "Websocket: stopped on " << desc_.port;
    }

    void onStop () override
    {
        JLOG (j_.warning)
            << "Websocket: onStop " << desc_.port;

        typename WebSocket::EndpointPtr endpoint;
        {
            ScopedLockType lock (m_endpointLock);
            endpoint = m_endpoint;
        }

        if (endpoint)
            endpoint->stop ();
        signalThreadShouldExit ();
    }

    void listen();
};

} // websocket
} // ripple

#endif
