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

#ifndef RIPPLE_PEERFINDER_CHECKER_H_INCLUDED
#define RIPPLE_PEERFINDER_CHECKER_H_INCLUDED

#include <beast/asio/IPAddressConversion.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/wrap_handler.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/system/error_code.hpp>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <beast/asio/shared_handler.h>
    
namespace ripple {
namespace PeerFinder {

class Checker
{
public:
    struct Result
    {
        /** The original address. */
        beast::IP::Endpoint address;

        /** The error code from the operation. */
        boost::system::error_code error;

        /** `true` if the endpoint is reachable, else `false`.
            Only defined if no error occurred.
        */
        bool canAccept = false;
    };

private:
    class Request
        : public std::enable_shared_from_this <Request>
        , public boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <boost::intrusive::normal_link>>
    {
    public:
        typedef boost::asio::ip::tcp        Protocol;
        typedef boost::system::error_code   error_code;
        typedef Protocol::socket            socket_type;
        typedef Protocol::endpoint          endpoint_type;

        Checker& m_owner;
        boost::asio::io_service& io_service_;
        beast::IP::Endpoint m_address;
        beast::asio::shared_handler <void (Result)> m_handler;
        socket_type socket_;
        boost::system::error_code m_error;
        bool m_canAccept;

        Request (Checker& owner, boost::asio::io_service& io_service,
            beast::IP::Endpoint const& address, beast::asio::shared_handler <
                void (Result)> const& handler)
            : m_owner (owner)
            , io_service_ (io_service)
            , m_address (address)
            , m_handler (handler)
            , socket_ (io_service_)
            , m_canAccept (false)
        {
        }

        ~Request ()
        {
            Result result;
            result.address = m_address;
            result.error = m_error;
            io_service_.wrap (m_handler) (result);

            m_owner.remove (*this);
        }

        void
        go()
        {
            socket_.async_connect (beast::IPAddressConversion::to_asio_endpoint (
                m_address), beast::asio::wrap_handler (std::bind (
                    &Request::on_connect, shared_from_this(),
                        beast::asio::placeholders::error), m_handler));
        }

        void stop()
        {
            socket_.cancel();
        }

        void on_connect (boost::system::error_code ec)
        {
            m_error = ec;
            if (ec)
                return;

            m_canAccept = true;
        }
    };

    //--------------------------------------------------------------------------

    void remove (Request& request)
    {
        std::lock_guard <std::mutex> lock (mutex_);
        list_.erase (list_.iterator_to (request));
        if (list_.size() == 0)
            cond_.notify_all();
    }

    using list_type = boost::intrusive::make_list <Request,
        boost::intrusive::constant_time_size <true>>::type;

    std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_ = false;
    boost::asio::io_service& io_service_;
    list_type list_;

public:
    explicit
    Checker (boost::asio::io_service& io_service)
        : io_service_(io_service)
    {
    }

    /** Destroy the service.
        Any pending I/O operations will be canceled. This call blocks until
        all pending operations complete (either with success or with
        operation_aborted) and the associated thread and io_service have
        no more work remaining.
    */

    ~Checker()
    {
        wait();
    }

    /** Stop the service.
        Pending I/O operations will be canceled.
        This issues cancel orders for all pending I/O operations and then
        returns immediately. Handlers will receive operation_aborted errors,
        or if they were already queued they will complete normally.
    */
    void
    stop()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        if (! stop_)
        {
            stop_ = true;
            for (auto& c : list_)
                c.stop();
        }
    }

    /** Block until all pending I/O completes. */
    void
    wait()
    {
        std::unique_lock<std::mutex> lock (mutex_);
        while (! list_.empty())
            cond_.wait (lock);
    }

    /** Performs an async connection test on the specified endpoint.
        The port must be non-zero.
    */
    void
    async_test (beast::IP::Endpoint const& endpoint,
        beast::asio::shared_handler <void (Result)> handler)
    {
        std::lock_guard<std::mutex> lock (mutex_);
        assert (! stop_);
        auto const request = std::make_shared <Request> (
            *this, io_service_, endpoint, handler);
        list_.push_back (*request);
        request->go();
    }
};

}
}

#endif
