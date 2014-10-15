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

#include <ripple/http/impl/ServerImpl.h>
#include <ripple/http/impl/Peer.h>
#include <beast/chrono/chrono_io.h>
#include <boost/chrono/chrono_io.hpp>
#include <cassert>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <stdio.h>
#include <time.h>

namespace ripple {
namespace HTTP {

ServerImpl::ServerImpl (Handler& handler, beast::Journal journal)
    : handler_ (handler)
    , journal_ (journal)
    , strand_ (io_service_)
    , work_ (boost::in_place (std::ref (io_service_)))
    , hist_{}
{
    thread_ = std::thread (std::bind (
        &ServerImpl::run, this));
}

ServerImpl::~ServerImpl()
{
    close();
    {
        // Block until all Door objects destroyed
        std::unique_lock<std::mutex> lock(mutex_);
        while (! list_.empty())
            cond_.wait(lock);
    }
    thread_.join();
}

void
ServerImpl::ports (std::vector<Port> const& ports)
{
    if (closed())
        throw std::logic_error("ports() on closed HTTP::Server");
    for(auto const& _ : ports)
        std::make_shared<Door>(
            io_service_, *this, _)->run();
}

void
ServerImpl::onWrite (beast::PropertyStream::Map& map)
{
    std::lock_guard <std::mutex> lock (mutex_);
    map ["active"] = list_.size();
    {
        std::string s;
        for (int i = 0; i <= high_; ++i)
        {
            if (i)
                s += ", ";
            s += std::to_string (hist_[i]);
        }
        map ["hist"] = s;
    }
    {
        beast::PropertyStream::Set set ("history", map);
        for (auto const& stat : stats_)
        {
            beast::PropertyStream::Map item (set);

            item ["id"] = stat.id;
            item ["when"] = stat.when;

            {
                std::stringstream ss;
                ss << stat.elapsed;
                item ["elapsed"] = ss.str();
            }

            item ["requests"] = stat.requests;
            item ["bytes_in"] = stat.bytes_in;
            item ["bytes_out"] = stat.bytes_out;
            if (stat.ec)
                item ["error"] = stat.ec.message();
        }
    }
}

void
ServerImpl::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (work_)
    {
        work_ = boost::none;
        // Close all Door objects
        for(auto& _ :list_)
            _.close();
    }
}

//--------------------------------------------------------------------------

void
ServerImpl::add (Child& child)
{
    std::lock_guard<std::mutex> lock(mutex_);
    list_.push_back(child);
}

void
ServerImpl::remove (Child& child)
{
    std::lock_guard<std::mutex> lock(mutex_);
    list_.erase(list_.iterator_to(child));
    if (list_.empty())
    {
        handler_.onStopped(*this);
        cond_.notify_all();
    }
}

bool
ServerImpl::closed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ! work_;
}

void
ServerImpl::report (Stat&& stat)
{
    int const bucket = ceil_log2 (stat.requests);
    std::lock_guard <std::mutex> lock (mutex_);
    ++hist_[bucket];
    high_ = std::max (high_, bucket);
    if (stats_.size() >= historySize)
        stats_.pop_back();
    stats_.emplace_front (std::move(stat));
}

//--------------------------------------------------------------------------

int
ServerImpl::ceil_log2 (unsigned long long x)
{
    static const unsigned long long t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull
    };

    int y = (((x & (x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for(i = 0; i < 6; i++) {
        int k = (((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return y;
}

void
ServerImpl::run()
{
    static std::atomic <int> id;
    beast::Thread::setCurrentThreadName (
        std::string("HTTP::Server #") + std::to_string (++id));
    io_service_.run();
}

}
}
