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

#include <ripple/common/seconds_clock.h>
#include <ripple/http/Server.h>
#include <beast/intrusive/List.h>
#include <beast/threads/SharedData.h>
#include <beast/threads/Thread.h>
#include <beast/module/asio/basics/SharedArg.h>
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace ripple {
namespace HTTP {

class Door;
class Peer;

struct Stat
{
    std::size_t id;
    std::string when;
    std::chrono::seconds elapsed;
    int requests;
    std::size_t bytes_in;
    std::size_t bytes_out;
    boost::system::error_code ec;
};

class ServerImpl
{
private:
    typedef std::chrono::system_clock clock_type;

    enum
    {
        historySize = 100
    };

    struct State
    {
        // Attributes for our listening ports
        Ports ports;

        // All allocated Peer objects
        beast::List <Peer> peers;

        // All allocated Door objects
        beast::List <Door> doors;
    };

    typedef std::vector <beast::SharedPtr <Door>> Doors;

    Server& m_server;
    Handler& m_handler;
    std::thread thread_;
    std::mutex mutable mutex_;
    std::condition_variable cond_;
    beast::Journal journal_;
    boost::asio::io_service io_service_;
    boost::asio::io_service::strand m_strand;
    boost::optional <boost::asio::io_service::work> m_work;
    beast::WaitableEvent m_stopped;
    State state_;
    Doors m_doors;
    std::deque <Stat> stats_;
    std::array <std::size_t, 64> hist_;
    int high_ = 0;

public:
    ServerImpl (Server& server, Handler& handler, beast::Journal journal);
    ~ServerImpl ();

    beast::Journal
    journal() const
    {
        return journal_;
    }

    Ports const&
    getPorts () const;

    void
    setPorts (Ports const& ports);

    bool
    stopping () const;

    void
    stop (bool wait);

    Handler&
    handler();

    boost::asio::io_service&
    get_io_service();

    void
    add (Peer& peer);

    void
    add (Door& door);

    void
    remove (Peer& peer);

    void
    remove (Door& door);

    void
    report (Stat&& stat);

    void
    onWrite (beast::PropertyStream::Map& map);

private:
    static
    int
    ceil_log2 (unsigned long long x);

    static
    int 
    compare (Port const& lhs, Port const& rhs);

    void
    update();
    
    void
    on_update();

    void
    run();
};


}
}

#endif
