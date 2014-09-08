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

#include <ripple/http/impl/Peer.h>
#include <beast/asio/ssl.h>
#include <cassert>

namespace ripple {
namespace HTTP {

/*
Reference:
http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
*/

std::atomic <std::size_t> Peer::s_count_;

std::size_t
Peer::count()
{
    return s_count_.load();
}

Peer::Peer (ServerImpl& server, Port const& port, beast::Journal journal)
    : journal_ (journal)
    , server_ (server)
    , strand_ (server_.get_io_service())
    , timer_ (server_.get_io_service())
    , parser_ (message_, true)
{
    ++s_count_;

    static std::atomic <int> sid;
    nid_ = ++sid;
    id_ = std::string("#") + std::to_string(nid_) + " ";

    tag = nullptr;

    int flags = MultiSocket::Flag::server_role;

    switch (port.security)
    {
    case Port::Security::no_ssl:
        break;

    case Port::Security::allow_ssl:
        flags |= MultiSocket::Flag::ssl;
        break;

    case Port::Security::require_ssl:
        flags |= MultiSocket::Flag::ssl_required;
        break;
    }

    socket_.reset (MultiSocket::New (
        server_.get_io_service(), port.context->get(), flags));

    server_.add (*this);

    if (journal_.trace) journal_.trace << id_ <<
        "created";
}

Peer::~Peer ()
{
    if (callClose_)
    {
        Stat stat;
        stat.id = nid_;
        stat.when = std::move (when_str_);
        stat.elapsed = std::chrono::duration_cast <
            std::chrono::seconds> (clock_type::now() - when_);
        stat.requests = request_count_;
        stat.bytes_in = bytes_in_;
        stat.bytes_out = bytes_out_;
        stat.ec = std::move (ec_);
        server_.report (std::move (stat));

        server_.handler().onClose (session(), ec_);
    }

    server_.remove (*this);

    if (journal_.trace) journal_.trace << id_ <<
        "destroyed: " << request_count_ <<
            ((request_count_ == 1) ? " request" : " requests");

    --s_count_;
}

//------------------------------------------------------------------------------

// Called when the acceptor accepts our socket.
void
Peer::accept (boost::asio::ip::tcp::endpoint endpoint)
{
    if (! strand_.running_in_this_thread())
        return server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::accept, shared_from_this(), endpoint)));

    if (journal_.trace) journal_.trace << id_ <<
        "accept:    " << endpoint.address();

    callClose_ = true;

    when_ = clock_type::now();
    when_str_ = beast::Time::getCurrentTime().formatted (
        "%Y-%b-%d %H:%M:%S").toStdString();

    server_.handler().onAccept (session());

    // Handler might have closed
    if (state_ == State::closed)
    {
        // VFALCO TODO Is this the correct way to close the socket?
        //             See what state the socket is in and verify.
        //closed();
        return;
    }

    if (socket_->needs_handshake ())
    {
        start_timer();
        socket_->async_handshake (beast::asio::abstract_socket::server,
            strand_.wrap (std::bind (&Peer::on_handshake, shared_from_this(),
                beast::asio::placeholders::error)));
    }
    else
    {
        async_read();
    }
}

// Called by a completion handler when error is not eof or aborted.
void
Peer::fail (error_code ec)
{
    assert (ec);
    assert (strand_.running_in_this_thread());

    if (journal_.debug) journal_.debug << id_ <<
        "fail:      " << ec.message();

    ec_ = ec;
    socket_->cancel(ec);
}

// Start the timer.
// If the timer expires, the session is considered
// timed out and will be forcefully closed.
void
Peer::start_timer()
{
    timer_.expires_from_now (
        boost::posix_time::seconds (
            timeoutSeconds));

    timer_.async_wait (strand_.wrap (std::bind (
        &Peer::on_timer, shared_from_this(),
            beast::asio::placeholders::error)));
}

void
Peer::async_write ()
{
    void const* data;
    std::size_t bytes;
    {
        std::lock_guard <std::mutex> lock (mutex_);
        buffer& b = write_queue_.front();
        data = b.data.get() + b.used;
        bytes = b.bytes - b.used;
    }

    start_timer();
    boost::asio::async_write (*socket_, boost::asio::buffer (data, bytes),
        boost::asio::transfer_at_least (1), strand_.wrap (std::bind (
            &Peer::on_write, shared_from_this(),
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

void
Peer::async_read()
{
    start_timer();
    boost::asio::async_read (*socket_, read_buf_.prepare (bufferSize),
        boost::asio::transfer_at_least (1), strand_.wrap (std::bind (
            &Peer::on_read, shared_from_this(),
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred)));
}

//------------------------------------------------------------------------------

// Called when session times out
void
Peer::on_timer (error_code ec)
{
    if (state_ == State::closed)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "timer:     closed";
        return;
    }

    if (ec == boost::asio::error::operation_aborted)
    {
        // Disable this otherwise we log needlessly
        // on every new read and write.
        /*
        if (journal_.trace) journal_.trace << id_ <<
            "timer:     aborted";
        */
        return;
    }

    if (! ec)
        ec = boost::system::errc::make_error_code (
            boost::system::errc::timed_out);

    if (journal_.debug) journal_.debug << id_ <<
        "timer:     " << ec.message();
    fail (ec);
}

// Called when the handshake completes
void
Peer::on_handshake (error_code ec)
{
    {
        error_code ec;
        timer_.cancel(ec);
    }

    bool const ssl = socket_->ssl_handle() != nullptr;

    if (state_ == State::closed)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "handshake: closed";
        return;
    }

    if (ec == boost::asio::error::operation_aborted)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "handshake: aborted";
        return;
    }

    if (ec)
    {
        if (journal_.debug) journal_.debug << id_ <<
            "handshake: " << ec.message();
        return fail (ec);
    }

    if (journal_.trace) journal_.trace << id_ <<
        "handshake" << (ssl ? ": ssl" : "");

    async_read();
}

// Called repeatedly with the http request data
void
Peer::on_read (error_code ec, std::size_t bytes_transferred)
{
    // This needs to happen before the call to onRequest
    // otherwise we could time out if the Handler takes too long.
    {
        error_code ec;
        timer_.cancel(ec);
    }

    if (state_ == State::closed)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "read:      closed";
        return;
    }

    if (ec == boost::asio::error::operation_aborted)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "read:      aborted";
        return;
    }

    if (beast::asio::is_short_read (ec))
    {
        if (journal_.trace) journal_.trace << id_ <<
            "read:      " << ec.message();
        return fail (ec);
    }

    if (ec && ec != boost::asio::error::eof)
    {
        if (journal_.debug) journal_.debug << id_ <<
            "read:      " << ec.message();
        return fail (ec);
    }

    bool const eof = ec == boost::asio::error::eof;
    if (! eof)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "read:      " << bytes_transferred << " bytes";
    }
    else
    {
        // End of stream reached:
        // http://www.boost.org/doc/libs/1_56_0/doc/html/boost_asio/overview/core/streams.html
        if (bytes_transferred != 0)
            if (journal_.error) journal_.error << id_ <<
                "read:      eof (" << bytes_transferred << " bytes)";
        if (journal_.debug) journal_.debug << id_ <<
            "read:      eof";
        ec = error_code{};
    }

    bytes_in_ += bytes_transferred;
    read_buf_.commit (bytes_transferred);

    std::pair <bool, std::size_t> result;
    if (! eof)
    {
        result = parser_.write (read_buf_.data());
        if (result.first)
            read_buf_.consume (result.second);
        else
            ec = parser_.error();
    }
    else
    {
        result.first = parser_.write_eof();
        if (! result.first)
            ec = parser_.error();
    }

    // VFALCO TODO Currently parsing errors are treated the
    //             same as the connection dropping. Instead, we
    // should request that the handler compose a proper HTTP error
    // response. This requires refactoring HTTPReply() into
    // something sensible.
    //
    if (! ec && parser_.complete())
    {
        // Perform half-close when Connection: close and not SSL
        if (! message_.keep_alive() &&
            ! socket_->needs_handshake())
            socket_->shutdown (socket::shutdown_receive, ec);

        if (! ec)
        {
            ++request_count_;
            server_.handler().onRequest (session());
            return;
        }
    }

    if (ec)
    {
        if (journal_.debug) journal_.debug << id_ <<
            "read:      " << ec.message();
        return fail (ec);
    }

    if (! eof)
        async_read();
}

// Called when async_write completes.
void
Peer::on_write (error_code ec, std::size_t bytes_transferred)
{
    {
        error_code ec;
        timer_.cancel (ec);
    }

    if (state_ == State::closed)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "write:     closed";
        return;
    }

    if (ec == boost::asio::error::operation_aborted)
    {
        if (journal_.trace) journal_.trace << id_ <<
            "write:     aborted";
        return;
    }

    if (ec)
    {
        if (journal_.debug) journal_.debug << id_ <<
            "write:     " << ec.message();
        return fail (ec);
    }

    if (bytes_transferred == 0)
        if (journal_.error) journal_.error << id_ <<
            "write:     0 bytes";

    if (journal_.trace) journal_.trace << id_ <<
        "write:     " << bytes_transferred << " bytes";

    bytes_out_ += bytes_transferred;

    bool empty;
    {
        std::lock_guard <std::mutex> lock (mutex_);
        buffer& b = write_queue_.front();
        b.used += bytes_transferred;
        if (b.used == b.bytes)
        {
            write_queue_.pop_front();
            empty = write_queue_.empty();
        }
        else
        {
            assert (b.used < b.bytes);
            empty = false;
        }
    }

    if (! empty)
        return async_write();
    
    if (! complete_)
        return;

    // Handler is done writing, did we graceful close?
    if (state_ == State::flush)
    {
        if (socket_->needs_handshake())
        {
            // ssl::stream
            start_timer();
            socket_->async_shutdown (strand_.wrap (std::bind (
                &Peer::on_shutdown, shared_from_this(),
                    beast::asio::placeholders::error)));
            return;
        }

        {
            error_code ec;
            socket_->shutdown (MultiSocket::shutdown_send, ec);
        }
        return;
    }

    // keep-alive
    complete_ = false;
    async_read();
}

// Called when async_shutdown completes
void
Peer::on_shutdown (error_code ec)
{
    {
        error_code ec;
        timer_.cancel (ec);
    }

    if (ec == boost::asio::error::operation_aborted)
    {
        // We canceled i/o on the socket, or we called async_shutdown
        // and then closed the socket before getting the completion.
        if (journal_.trace) journal_.trace << id_ <<
            "shutdown:  aborted";
        return;
    }
    
    if (ec == boost::asio::error::eof)
    {
        // Happens when ssl::stream::async_shutdown completes without error
        if (journal_.trace) journal_.trace << id_ <<
            "shutdown:  eof";
        return;
    }

    if ((ec.category() == boost::asio::error::get_ssl_category())
         && (ERR_GET_REASON(ec.value()) == SSL_R_SHORT_READ))
    {
        // Remote peer failed to send SSL close_notify message.
        if (journal_.trace) journal_.trace << id_ <<
            "shutdown:  missing close_notify";
        return;
    }
    
    if (ec)
    {
        if (journal_.debug) journal_.debug << id_ <<
            "shutdown:  " << ec.message();
        return fail (ec);
    }

    if (journal_.trace) journal_.trace << id_ <<
        "shutdown";
}

//------------------------------------------------------------------------------

// Send a copy of the data.
void
Peer::write (void const* buffer, std::size_t bytes)
{
    if (bytes == 0)
        return;

    bool empty;
    {
        std::lock_guard <std::mutex> lock (mutex_);
        empty = write_queue_.empty();
        write_queue_.emplace_back (buffer, bytes);
    }

    if (empty)
        server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::async_write, shared_from_this())));
}

// Make the Session asynchronous
void
Peer::detach ()
{
    if (! detach_ref_)
    {
        assert (! work_);

        // Maintain an additional reference while detached
        detach_ref_ = shared_from_this();

        // Prevent the io_service from running out of work.
        // The work object will be destroyed with the Peer
        // after the Session is closed and handlers complete.
        //
        work_ = boost::in_place (std::ref (
            server_.get_io_service()));
    }
}

// Called to indicate the response has been written (but not sent)
void
Peer::complete()
{
    if (! strand_.running_in_this_thread())
        return server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::complete, shared_from_this())));

    // Reattach
    detach_ref_.reset();
    work_ = boost::none;

    message_ = beast::http::message{};
    parser_ = beast::http::parser{message_, true};
    complete_ = true;

    bool empty;
    {
        std::lock_guard <std::mutex> lock (mutex_);
        empty = write_queue_.empty();
    }

    if (empty)
    {
        // keep-alive
        complete_ = false;
        async_read();
    }
}

// Called from the Handler to close the session.
void
Peer::close (bool graceful)
{
    if (! strand_.running_in_this_thread())
        return server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::close, shared_from_this(), graceful)));

    // Reattach
    detach_ref_.reset();
    work_ = boost::none;

    assert (state_ == State::open);
    state_ = graceful ? State::flush : State::closed;

    complete_ = true;

    if (graceful)
    {
        bool empty;
        {
            std::lock_guard <std::mutex> lock (mutex_);
            empty = write_queue_.empty();
        }

        if (! empty)
            return;

        if (socket_->needs_handshake())
        {
            start_timer();
            socket_->async_shutdown (strand_.wrap (std::bind (
                &Peer::on_shutdown, shared_from_this(),
                    beast::asio::placeholders::error)));
            return;
        }
    }

    error_code ec;
    timer_.cancel (ec);
    socket_->close (ec);
}

}
}
