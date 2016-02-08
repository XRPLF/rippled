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

#ifndef RIPPLE_SERVER_SERVERIMPL_H_INCLUDED
#define RIPPLE_SERVER_SERVERIMPL_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/server/Handler.h>
#include <ripple/server/Server.h>
#include <ripple/server/impl/io_list.h>
#include <beast/intrusive/List.h>
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

class BasicPeer;
class Door;

class ServerImpl : public Server
{
private:
    using clock_type = std::chrono::system_clock;

    enum
    {
        historySize = 100
    };

    using Doors = std::vector <std::shared_ptr<Door>>;

    Handler& handler_;
    beast::Journal j_;
    boost::asio::io_service& io_service_;
    boost::asio::io_service::strand strand_;
    boost::optional <boost::asio::io_service::work> work_;

    std::mutex m_;
    std::vector<Port> ports_;
    std::vector<std::weak_ptr<Door>> list_;
    int high_ = 0;
    std::array <std::size_t, 64> hist_;
    
    io_list ios_;

public:
    ServerImpl (Handler& handler,
        boost::asio::io_service& io_service, beast::Journal journal);

    ~ServerImpl();

    beast::Journal
    journal() override
    {
        return j_;
    }

    void
    ports (std::vector<Port> const& ports) override;

    void
    close() override;

    io_list&
    ios()
    {
        return ios_;
    }

public:
    boost::asio::io_service&
    get_io_service()
    {
        return io_service_;
    }

    bool
    closed();

private:
    static
    int
    ceil_log2 (unsigned long long x);
};


} // ripple

#endif
