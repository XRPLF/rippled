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

#ifndef RIPPLE_HTTP_SERVERIMPL_H_INCLUDED
#define RIPPLE_HTTP_SERVERIMPL_H_INCLUDED

#include <beast/threads/Thread.h>
#include <beast/module/asio/basics/SharedArg.h>

namespace ripple {
namespace HTTP {

class Door;
class Peer;

class ServerImpl : public beast::Thread
{
public:
    struct State
    {
        // Attributes for our listening ports
        Ports ports;

        // All allocated Peer objects
        beast::List <Peer> peers;

        // All allocated Door objects
        beast::List <Door> doors;
    };

    typedef beast::SharedData <State> SharedState;
    typedef std::vector <beast::SharedPtr <Door> > Doors;

    Server& m_server;
    Handler& m_handler;
    beast::Journal m_journal;
    boost::asio::io_service m_io_service;
    boost::asio::io_service::strand m_strand;
    boost::optional <boost::asio::io_service::work> m_work;
    beast::WaitableEvent m_stopped;
    SharedState m_state;
    Doors m_doors;

    ServerImpl (Server& server, Handler& handler, beast::Journal journal);
    ~ServerImpl ();
    beast::Journal const& journal() const;
    Ports const& getPorts () const;
    void setPorts (Ports const& ports);
    bool stopping () const;
    void stop (bool wait);

    Handler& handler();
    boost::asio::io_service& get_io_service();
    void add (Peer& peer);
    void add (Door& door);
    void remove (Peer& peer);
    void remove (Door& door);

    void handle_update ();
    void update ();
    void run ();

    static int compare (Port const& lhs, Port const& rhs);
};


}
}

#endif
