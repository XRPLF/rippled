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

#include <BeastConfig.h>
#include <ripple/app/websocket/WSDoor.h>
#include <ripple/app/websocket/WSServerHandler.h>
#include <ripple/unity/websocket.h>
#include <beast/threads/Thread.h>
#include <beast/cxx14/memory.h> // <memory>
#include <mutex>

namespace ripple {

//
// This is a light weight, untrusted interface for web clients.
// For now we don't provide proof.  Later we will.
//
// Might need to support this header for browsers: Access-Control-Allow-Origin: *
// - https://developer.mozilla.org/en-US/docs/HTTP_access_control
//

//
// Strategy:
// - We only talk to NetworkOPs (so we will work even in thin mode)
// - NetworkOPs is smart enough to subscribe and or pass back messages
//
// VFALCO NOTE NetworkOPs isn't used here...
//

class WSDoorImp
    : public WSDoor
    , protected beast::Thread
{
private:
    using LockType = std::recursive_mutex;
    using ScopedLockType = std::lock_guard <LockType>;

    std::shared_ptr<HTTP::Port> port_;
    Resource::Manager& m_resourceManager;
    InfoSub::Source& m_source;
    LockType m_endpointLock;
    std::shared_ptr<websocketpp_02::server_autotls> m_endpoint;
    CollectorManager& collectorManager_;

public:
    WSDoorImp (HTTP::Port const& port, Resource::Manager& resourceManager,
        InfoSub::Source& source, CollectorManager& cm)
        : WSDoor (source)
        , Thread ("websocket")
        , port_(std::make_shared<HTTP::Port>(port))
        , m_resourceManager (resourceManager)
        , m_source (source)
        , collectorManager_ (cm)
    {
        startThread ();
    }

    ~WSDoorImp ()
    {
        stopThread ();
    }

private:
    void run ()
    {
        WriteLog (lsINFO, WSDoor) <<
            "Websocket: '" << port_->name << "' listening on " <<
                port_->ip.to_string() << ":" << std::to_string(port_->port) <<
                    (port_->allow_admin ? "(Admin)" : "");

        websocketpp_02::server_autotls::handler::ptr handler (
            new WSServerHandler <websocketpp_02::server_autotls> (
                port_, m_resourceManager, m_source, collectorManager_));

        {
            ScopedLockType lock (m_endpointLock);

            m_endpoint = std::make_shared<websocketpp_02::server_autotls> (
                handler);
        }

        // Call the main-event-loop of the websocket server.
        try
        {
            m_endpoint->listen (port_->ip, port_->port);
        }
        catch (websocketpp_02::exception& e)
        {
            WriteLog (lsWARNING, WSDoor) << "websocketpp_02 exception: "
                                         << e.what ();

            // temporary workaround for websocketpp_02 throwing exceptions on
            // access/close races
            for (;;)
            {
                // https://github.com/zaphoyd/websocketpp_02/issues/98
                try
                {
                    m_endpoint->get_io_service ().run ();
                    break;
                }
                catch (websocketpp_02::exception& e)
                {
                    WriteLog (lsWARNING, WSDoor) << "websocketpp_02 exception: "
                                                 << e.what ();
                }
            }
        }

        {
            ScopedLockType lock (m_endpointLock);

            m_endpoint.reset();
        }

        stopped ();
    }

    void onStop ()
    {
        std::shared_ptr<websocketpp_02::server_autotls> endpoint;

        {
            ScopedLockType lock (m_endpointLock);

             endpoint = m_endpoint;
        }

        // VFALCO NOTE we probably dont want to block here
        //             but websocketpp is deficient and broken.
        //
        if (endpoint)
            endpoint->stop ();

        signalThreadShouldExit ();
    }
};

//------------------------------------------------------------------------------

WSDoor::WSDoor (Stoppable& parent)
    : Stoppable ("WSDoor", parent)
{
}

//------------------------------------------------------------------------------

std::unique_ptr<WSDoor>
make_WSDoor (HTTP::Port const& port, Resource::Manager& resourceManager,
    InfoSub::Source& source, CollectorManager& cm)
{
    std::unique_ptr<WSDoor> door;

    try
    {
        door = std::make_unique <WSDoorImp> (port, resourceManager, source, cm);
    }
    catch (...)
    {
    }

    return door;
}

}
