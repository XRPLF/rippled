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
#include <beast/chrono/chrono_io.h>
#include <boost/chrono/chrono_io.hpp>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <stdio.h>
#include <time.h>

namespace ripple {
namespace HTTP {

ServerImpl::ServerImpl (Server& server,
    Handler& handler, beast::Journal journal)
    : m_server (server)
    , m_handler (handler)
    , journal_ (journal)
    , m_strand (io_service_)
    , m_work (boost::in_place (std::ref (io_service_)))
    , m_stopped (true)
    , hist_{}
{
    thread_ = std::thread (std::bind (
        &ServerImpl::run, this));
}

ServerImpl::~ServerImpl ()
{
    thread_.join();
}

Ports const&
ServerImpl::getPorts () const
{
    std::lock_guard <std::mutex> lock (mutex_);
    return state_.ports;
}

void
ServerImpl::setPorts (Ports const& ports)
{
    std::lock_guard <std::mutex> lock (mutex_);
    state_.ports = ports;
    update();
}

bool
ServerImpl::stopping () const
{
    return ! m_work;
}

void
ServerImpl::stop (bool wait)
{
    if (! stopping())
    {
        m_work = boost::none;
        update();
    }

    if (wait)
        m_stopped.wait();
}

//--------------------------------------------------------------------------
//
// Server
//

Handler&
ServerImpl::handler()
{
    return m_handler;
}

boost::asio::io_service&
ServerImpl::get_io_service()
{
    return io_service_;
}

// Inserts the peer into our list of peers. We only remove it
// from the list inside the destructor of the Peer object. This
// way, the Peer can never outlive the server.
//
void
ServerImpl::add (BasicPeer& peer)
{
    std::lock_guard <std::mutex> lock (mutex_);
    state_.peers.push_back (peer);
}

void
ServerImpl::add (Door& door)
{
    std::lock_guard <std::mutex> lock (mutex_);
    state_.doors.push_back (door);
}

// Removes the peer from our list of peers. This is only called from
// the destructor of Peer. Essentially, the item in the list functions
// as a weak_ptr.
//
void
ServerImpl::remove (BasicPeer& peer)
{
    std::lock_guard <std::mutex> lock (mutex_);
    state_.peers.erase (state_.peers.iterator_to (peer));
}

void
ServerImpl::remove (Door& door)
{
    std::lock_guard <std::mutex> lock (mutex_);
    state_.doors.erase (state_.doors.iterator_to (door));
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

void
ServerImpl::onWrite (beast::PropertyStream::Map& map)
{
    std::lock_guard <std::mutex> lock (mutex_);

    // VFALCO TODO Write the list of doors

    map ["active"] = state_.peers.size();

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

//--------------------------------------------------------------------------

int
ServerImpl::compare (Port const& lhs, Port const& rhs)
{
    if (lhs < rhs)
        return -1;
    else if (rhs < lhs)
        return 1;
    return 0;
}

void
ServerImpl::update()
{
    io_service_.post (m_strand.wrap (std::bind (
        &ServerImpl::on_update, this)));
}

// Updates our Door list based on settings.
void
ServerImpl::on_update ()
{
    /*
    if (! m_strand.running_in_this_thread())
        io_service_.dispatch (m_strand.wrap (std::bind (
            &ServerImpl::update, this)));
    */

    if (! stopping())
    {
        // Make a local copy to shorten the lock
        //
        Ports ports;
        {
            std::lock_guard <std::mutex> lock (mutex_);
            ports = state_.ports;
        }

        std::sort (ports.begin(), ports.end());

        // Walk the Door list and the Port list simultaneously and
        // build a replacement Door vector which we will then swap in.
        //
        Doors doors;
        Doors::iterator door (m_doors.begin());
        for (Ports::const_iterator port (ports.begin());
            port != ports.end(); ++port)
        {
            int comp = 0;

            while (door != m_doors.end() &&
                    ((comp = compare (*port, (*door)->port())) > 0))
            {
                (*door)->cancel();
                ++door;
            }

            if (door != m_doors.end())
            {
                if (comp < 0)
                {
                    doors.push_back (std::make_shared <Door> (
                        io_service_, *this, *port));
                    doors.back()->listen();
                }
                else
                {
                    // old Port and new Port are the same
                    doors.push_back (*door);
                    ++door;
                }
            }
            else
            {
                doors.push_back (std::make_shared <Door> (
                    io_service_, *this, *port));
                doors.back()->listen();
            }
        }

        // Any remaining Door objects are not in the new set, so cancel them.
        //
        for (;door != m_doors.end();)
            (*door)->cancel();

        m_doors.swap (doors);
    }
    else
    {
        // Cancel pending I/O on all doors.
        //
        for (Doors::iterator iter (m_doors.begin());
            iter != m_doors.end(); ++iter)
        {
            (*iter)->cancel();
        }

        // Remove our references to the old doors.
        //
        m_doors.resize (0);
    }
}

// Thread entry point to perform io_service work
void
ServerImpl::run()
{
    static std::atomic <int> id;

    beast::Thread::setCurrentThreadName (
        std::string("HTTP::Server #") + std::to_string (++id));

    io_service_.run();

    m_stopped.signal();
    m_handler.onStopped (m_server);
}

}
}
