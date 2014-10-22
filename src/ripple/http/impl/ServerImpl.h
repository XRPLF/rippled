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

class BasicPeer;
class Door;

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

class ServerImpl : public Server
{
private:
    typedef std::chrono::system_clock clock_type;

    enum
    {
        historySize = 100
    };

    typedef std::vector <std::shared_ptr<Door>> Doors;

    Handler& handler_;
    std::thread thread_;
    std::mutex mutable mutex_;
    std::condition_variable cond_;
    beast::Journal journal_;
    boost::asio::io_service io_service_;
    boost::asio::io_service::strand strand_;
    boost::optional <boost::asio::io_service::work> work_;
    beast::WaitableEvent stopped_;
    Ports ports_;
    Doors doors_;
    beast::List <Door> door_list_;
    beast::List <BasicPeer> peers_;
    std::deque <Stat> stats_;
    std::array <std::size_t, 64> hist_;
    int high_ = 0;

public:
    ServerImpl (Handler& handler, beast::Journal journal);
    ~ServerImpl ();

    beast::Journal
    journal() const override
    {
        return journal_;
    }

    Ports const&
    getPorts () const override;

    void
    setPorts (Ports const& ports) override;

    void
    stopAsync() override
    {
        stop(false);
    }

    void
    stop() override
    {
        stop (true);
    }

    void
    onWrite (beast::PropertyStream::Map& map) override;

public:
    bool
    stopping () const;

    void
    stop (bool wait);

    Handler&
    handler();

    boost::asio::io_service&
    get_io_service();

    void
    add (BasicPeer& peer);

    void
    add (Door& door);

    void
    remove (BasicPeer& peer);

    void
    remove (Door& door);

    void
    report (Stat&& stat);

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
