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
#include <ripple/basics/contract.h>
#include <ripple/server/impl/Door.h>
#include <ripple/server/impl/PlainPeer.h>
#include <ripple/server/impl/SSLPeer.h>
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
   http://boost.org/doc/libs/1_56_0/doc/html/boost_asio/reference/streambuf.html
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
        endpoint_type remote_address)
    : Child(door)
    , socket_(std::move(socket))
    , timer_(socket_.get_io_service())
    , remote_address_(remote_address)
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
        return door_.create(ssl, buf.data(),
            std::move(socket_), remote_address_);
    if (ec != boost::asio::error::operation_aborted)
        if (door_.server_.journal().trace) door_.server_.journal().trace <<
            "Error detecting ssl: " << ec.message() <<
                " from " << remote_address_;
}

//------------------------------------------------------------------------------

Door::Door (boost::asio::io_service& io_service,
        ServerImpl& server, Port const& port)
    : port_(std::make_shared<Port>(port))
    , server_(server)
    , acceptor_(io_service)
    , strand_(io_service)
    , ssl_ (
        port_->protocol.count("https") > 0 ||
        //port_->protocol.count("wss") > 0 ||
        port_->protocol.count("peer") > 0)
    , plain_ (
        //port_->protocol.count("ws") > 0 ||
        port_->protocol.count("http") > 0)
{
    server_.add (*this);

    error_code ec;
    endpoint_type const local_address =
        endpoint_type(port.ip, port.port);

    acceptor_.open(local_address.protocol(), ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Open port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    acceptor_.set_option(
        boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Option for port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    acceptor_.bind(local_address, ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Bind port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    acceptor_.listen(boost::asio::socket_base::max_connections, ec);
    if (ec)
    {
        if (server_.journal().error) server_.journal().error <<
            "Listen on port '" << port.name << "' failed:" << ec.message();
        Throw<std::exception> ();
    }

    if (server_.journal().info) server_.journal().info <<
        "Opened " << port;
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

template <class ConstBufferSequence>
void
Door::create (bool ssl, ConstBufferSequence const& buffers,
    socket_type&& socket, endpoint_type remote_address)
{
    if (server_.closed())
        return;

    if (ssl)
    {
        auto const peer = std::make_shared <SSLPeer> (*this,
            server_.journal(), remote_address, buffers,
                std::move(socket));
        add(peer);
        return peer->run();
    }

    auto const peer = std::make_shared <PlainPeer> (*this,
        server_.journal(), remote_address, buffers,
            std::move(socket));
    add(peer);
    peer->run();
}

void
Door::do_accept (boost::asio::yield_context yield)
{
    for(;;)
    {
        error_code ec;
        endpoint_type remote_address;
        socket_type socket (acceptor_.get_io_service());
        acceptor_.async_accept (socket, remote_address, yield[ec]);
        if (ec && ec != boost::asio::error::operation_aborted)
            if (server_.journal().error) server_.journal().error <<
                "accept: " << ec.message();
        if (ec == boost::asio::error::operation_aborted || server_.closed())
            break;
        if (ec)
            continue;

        if (ssl_ && plain_)
        {
            auto const c = std::make_shared <detector> (
                *this, std::move(socket), remote_address);
            add(c);
            c->run();
        }
        else if (ssl_ || plain_)
        {
            create(ssl_, boost::asio::null_buffers{},
                std::move(socket), remote_address);
        }
    }
}

}
}
