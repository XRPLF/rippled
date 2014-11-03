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

#include <ripple/http/impl/Door.h>
#include <ripple/http/impl/PlainPeer.h>
#include <ripple/http/impl/SSLPeer.h>
#include <boost/asio/buffer.hpp>
#include <beast/asio/placeholders.h>
#include <beast/asio/ssl_bundle.h>
#include <functional>

namespace ripple {
namespace HTTP {

/** Detect SSL client handshakes.
    Analyzes the bytes in the provided buffer to detect the SSL client
    handshake. If the buffer contains insufficient data, more data will be
    read from the stream until there is enough to determine a result.
    No bytes are discarded from buf. Any additional bytes read are retained.
    buf must provide an interface compatible with boost::asio::streambuf
        http://www.boost.org/doc/libs/1_56_0/doc/html/boost_asio/reference/streambuf.html
    See
        http://www.ietf.org/rfc/rfc2246.txt
        Section 7.4. Handshake protocol
    @param socket The stream to read from
    @param buf A buffer to hold the received data
    @param yield A yield context
    @return The error code if an error occurs, otherwise `true` if
            the data read indicates the SSL client handshake.
*/
template <class Socket, class StreamBuf, class Yield>
std::pair <boost::system::error_code, bool>
detect_ssl (Socket& socket, StreamBuf& buf, Yield yield)
{
    std::pair <boost::system::error_code, bool> result;
    result.second = false;
    for(;;)
    {
        std::size_t const max = 4; // the most bytes we could need
        unsigned char data[max];
        auto const bytes = boost::asio::buffer_copy (
            boost::asio::buffer(data), buf.data());

        if (bytes > 0)
        {
            if (data[0] != 0x16) // message type 0x16 = "SSL Handshake"
                break;
        }

        if (bytes >= max)
        {
            result.second = true;
            break;
        }

        buf.commit(boost::asio::async_read (socket,
            buf.prepare(max - bytes), boost::asio::transfer_at_least(1),
                yield[result.first]));
        if (result.first)
            break;
    }
    return result;
}

//------------------------------------------------------------------------------

Door::Child::Child(Door& door)
    : door_(door)
{
}

Door::Child::~Child()
{
    door_.remove(*this);
}

//------------------------------------------------------------------------------

Door::detector::detector (Door& door, socket_type&& socket,
        endpoint_type endpoint)
    : Child(door)
    , socket_ (std::move(socket))
    , timer_ (socket_.get_io_service())
    , remote_endpoint_ (endpoint)
{
}

void
Door::detector::run()
{
    // do_detect must be called before do_timer or else
    // the timer can be canceled before it gets set.
    boost::asio::spawn (door_.strand_, std::bind (&detector::do_detect,
        shared_from_this(), std::placeholders::_1));

    boost::asio::spawn (door_.strand_, std::bind (&detector::do_timer,
        shared_from_this(), std::placeholders::_1));
}

void
Door::detector::close()
{
    error_code ec;
    socket_.close(ec);
    timer_.cancel(ec);
}

void
Door::detector::do_timer (yield_context yield)
{
    error_code ec; // ignored
    while (socket_.is_open())
    {
        timer_.async_wait (yield[ec]);
        if (timer_.expires_from_now() <= std::chrono::seconds(0))
            socket_.close(ec);
    }
}

void
Door::detector::do_detect (boost::asio::yield_context yield)
{
    bool ssl;
    error_code ec;
    beast::asio::streambuf buf(16);
    timer_.expires_from_now(std::chrono::seconds(15));
    std::tie(ec, ssl) = detect_ssl(socket_, buf, yield);
    error_code unused;
    timer_.cancel(unused);
    if (! ec)
        return door_.create(ssl, std::move(buf),
            std::move(socket_), remote_endpoint_);
    if (ec != boost::asio::error::operation_aborted)
        if (door_.server_.journal().trace) door_.server_.journal().trace <<
            "Error detecting ssl: " << ec.message() <<
                " from " << remote_endpoint_;
}

//------------------------------------------------------------------------------

Door::Door (boost::asio::io_service& io_service,
        ServerImpl& server, Port const& port)
    : port_(port)
    , server_(server)
    , acceptor_(io_service)
    , strand_(io_service)
{
    server_.add (*this);

    error_code ec;
    endpoint_type const local_address = to_asio(port);

    acceptor_.open(local_address.protocol(), ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Error opening listener: " << ec.message();
        throw std::exception();
        return;
    }

    acceptor_.set_option(
        boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Error setting listener options: " << ec.message();
        throw std::exception();
        return;
    }

    acceptor_.bind(local_address, ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Error binding to endpoint " << local_address <<
                ", '" << ec.message() << "'";
        throw std::exception();
        return;
    }

    acceptor_.listen(boost::asio::socket_base::max_connections, ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Error on listen: " << local_address <<
                ", '" << ec.message() << "'";
        throw std::exception();
        return;
    }

    if (server_.journal().info) server_.journal().info <<
        "Bound to endpoint " << to_string (acceptor_.local_endpoint());
}

Door::~Door()
{
    {
        // Block until all detector, Peer objects destroyed
        std::unique_lock<std::mutex> lock(mutex_);
        while (! list_.empty())
            cond_.wait(lock);
    }
    server_.remove (*this);
}

void
Door::run()
{
    boost::asio::spawn (strand_, std::bind (&Door::do_accept,
        shared_from_this(), std::placeholders::_1));
}

void
Door::close()
{
    if (! strand_.running_in_this_thread())
        return strand_.post(std::bind(
            &Door::close, shared_from_this()));
    error_code ec;
    acceptor_.close(ec);
    // Close all detector, Peer objects
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& _ : list_)
    {
        auto const peer = _.second.lock();
        if (peer != nullptr)
            peer->close();
    }
}

void
Door::remove (Child& c)
{
    std::lock_guard<std::mutex> lock(mutex_);
    list_.erase(&c);
    if (list_.empty())
        cond_.notify_all();
}

//------------------------------------------------------------------------------

void
Door::add (std::shared_ptr<Child> const& child)
{
    std::lock_guard<std::mutex> lock(mutex_);
    list_.emplace(child.get(), child);
}

void
Door::create (bool ssl, beast::asio::streambuf&& buf,
    socket_type&& socket, endpoint_type remote_address)
{
    if (server_.closed())
        return;
    error_code ec;
    switch (port_.security)
    {
    case Port::Security::no_ssl:
        if (ssl)
            ec = boost::system::errc::make_error_code (
                boost::system::errc::invalid_argument);

    case Port::Security::require_ssl:
        if (! ssl)
            ec = boost::system::errc::make_error_code (
                boost::system::errc::invalid_argument);

    case Port::Security::allow_ssl:
        if (! ec)
        {
            if (ssl)
            {
                auto const peer = std::make_shared <SSLPeer> (*this,
                    server_.journal(), remote_address, buf.data(),
                        std::move(socket));
                add(peer);
                peer->run();
                return;
            }

            auto const peer = std::make_shared <PlainPeer> (*this,
                server_.journal(), remote_address, buf.data(),
                    std::move(socket));
            add(peer);
            peer->run();
            return;
        }
        break;
    }

    if (ec)
        if (server_.journal().trace) server_.journal().trace <<
            "Error detecting ssl: " << ec.message() <<
                " from " << remote_address;
}

void
Door::do_accept (boost::asio::yield_context yield)
{
    for(;;)
    {
        error_code ec;
        endpoint_type endpoint;
        socket_type socket (acceptor_.get_io_service());
        acceptor_.async_accept (socket, endpoint, yield[ec]);
        if (ec && ec != boost::asio::error::operation_aborted)
            if (server_.journal().error) server_.journal().error <<
                "accept: " << ec.message();
        if (ec == boost::asio::error::operation_aborted || server_.closed())
            break;
        if (ec)
            continue;
        if (port_.security == Port::Security::no_ssl)
        {
            auto const peer = std::make_shared <PlainPeer> (*this,
                server_.journal(), endpoint, boost::asio::null_buffers(),
                    std::move(socket));
            add(peer);
            peer->run();
        }
        else if (port_.security == Port::Security::require_ssl)
        {
            auto const peer = std::make_shared <SSLPeer> (*this,
                server_.journal(), endpoint, boost::asio::null_buffers(),
                    std::move(socket));
            add(peer);
            peer->run();
        }
        else
        {
            auto const c = std::make_shared <detector> (
                *this, std::move(socket), endpoint);
            add(c);
            c->run();
        }
    }
}

}
}
