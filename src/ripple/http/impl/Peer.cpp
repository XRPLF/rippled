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
#include <cassert>

namespace ripple {
namespace HTTP {

Peer::Peer (ServerImpl& server, Port const& port, beast::Journal journal)
    : journal_ (journal)
    , server_ (server)
    , strand_ (server_.get_io_service())
    , timer_ (server_.get_io_service())
    , parser_ (message_, true)
    , pending_writes_ (0)
    , closed_ (false)
    , callClose_ (false)
    , detached_ (0)
    , request_count_ (0)
    , bytes_in_ (0)
    , bytes_out_ (0)
{
    static std::atomic <int> sid;
    id_ = std::string("#") + std::to_string(sid++);

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

    if (journal_.trace) journal_.trace <<
        id_ << " created";
}


Peer::~Peer ()
{
    if (callClose_)
    {
        Stat stat;
        stat.when = std::move (when_str_);
        stat.elapsed = std::chrono::duration_cast <
            std::chrono::seconds> (clock_type::now() - when_);
        stat.requests = request_count_;
        stat.bytes_in = bytes_in_;
        stat.bytes_out = bytes_out_;
        stat.ec = std::move (ec_);

        server_.report (std::move (stat));
        if (journal_.trace) journal_.trace <<
            id_ << " onClose (" << ec_ << ")";

        server_.handler().onClose (session(), ec_);
    }

    server_.remove (*this);

    if (journal_.trace) journal_.trace <<
        id_ << " destroyed";
}

//------------------------------------------------------------------------------

// Called when the acceptor accepts our socket.
void
Peer::accept ()
{
    if (! strand_.running_in_this_thread())
        return server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::accept, shared_from_this())));

    if (journal_.trace) journal_.trace <<
        id_ << " accept";

    callClose_ = true;

    when_ = clock_type::now();
    when_str_ = beast::Time::getCurrentTime().formatted (
        "%Y-%b-%d %H:%M:%S").toStdString();

    if (journal_.trace) journal_.trace <<
        id_ << " onAccept";

    server_.handler().onAccept (session());

    // Handler might have closed
    if (closed_)
    {
        // VFALCO TODO Is this the correct way to close the socket?
        //             See what state the socket is in and verify.
        cancel();
        return;
    }

    if (socket_->needs_handshake ())
    {
        start_timer();

        socket_->async_handshake (beast::asio::abstract_socket::server,
            strand_.wrap (std::bind (&Peer::on_ssl_handshake, shared_from_this(),
                beast::asio::placeholders::error)));
    }
    else
    {
        on_read_request (error_code{}, 0);
    }
}

// Cancel all pending i/o and timers and send tcp shutdown.
void
Peer::cancel ()
{
    if (journal_.trace) journal_.trace <<
        id_ << " cancel";

    error_code ec;
    timer_.cancel (ec);
    socket_->cancel (ec);
    socket_->shutdown (socket::shutdown_both, ec);
}

// Called by a completion handler when error is not eof or aborted.
void
Peer::failed (error_code const& ec)
{
    if (journal_.trace) journal_.trace <<
        id_ << " failed, " << ec.message();

    ec_ = ec;
    assert (ec_);
    cancel ();
}

// Start the timer.
// If the timer expires, the session is considered
// timed out and will be forcefully closed.
void
Peer::start_timer()
{
    if (journal_.trace) journal_.trace <<
        id_ << " start_timer";

    timer_.expires_from_now (
        boost::posix_time::seconds (
            timeoutSeconds));

    timer_.async_wait (strand_.wrap (std::bind (
        &Peer::on_timer, shared_from_this(),
            beast::asio::placeholders::error)));
}

// Send a shared buffer
void
Peer::async_write (SharedBuffer const& buf)
{
    assert (buf.get().size() > 0);

    ++pending_writes_;

    if (journal_.trace) journal_.trace <<
        id_ << " async_write, pending_writes = " << pending_writes_;

    start_timer();

    // Send the copy. We pass the SharedBuffer in the last parameter
    // so that a reference is maintained as the handler gets copied.
    // When the final completion function returns, the reference
    // count will drop to zero and the buffer will be freed.
    //
    boost::asio::async_write (*socket_,
        boost::asio::const_buffers_1 (&(*buf)[0], buf->size()),
            strand_.wrap (std::bind (&Peer::on_write_response,
                shared_from_this(), beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred, buf)));
}

//------------------------------------------------------------------------------

// Called when session times out
void
Peer::on_timer (error_code ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (! ec)
        ec = boost::system::errc::make_error_code (
            boost::system::errc::timed_out);

    failed (ec);
}

// Called when the handshake completes
void
Peer::on_ssl_handshake (error_code ec)
{
    if (journal_.trace) journal_.trace <<
        id_ << " on_ssl_handshake, " << ec.message();

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        failed (ec);
        return;
    }

    on_read_request (error_code{}, 0);
}

// Called repeatedly with the http request data
void
Peer::on_read_request (error_code ec, std::size_t bytes_transferred)
{
    if (journal_.trace)
    {
        if (! ec_)
            journal_.trace <<
                id_ << " on_read_request, " <<
                    bytes_transferred << " bytes";
        else
            journal_.trace <<
                id_ << " on_read_request failed, " <<
                    ec.message();
    }

    if (ec == boost::asio::error::operation_aborted)
        return;

    bool const eof = ec == boost::asio::error::eof;
    if (eof)
        ec = error_code{};

    if (! ec)
    {
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

        if (! ec)
        {
            if (parser_.complete())
            {
                // ???
                //if (! socket_->needs_handshake())
                //    socket_->shutdown (socket::shutdown_receive, ec);

                if (journal_.trace) journal_.trace <<
                    id_ << " onRequest";

                ++request_count_;

                server_.handler().onRequest (session());
                return;
            }
        }
    }

    if (ec)
    {
        failed (ec);
        return;
    }

    start_timer();

    socket_->async_read_some (read_buf_.prepare (bufferSize), strand_.wrap (
        std::bind (&Peer::on_read_request, shared_from_this(),
            beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

// Called when async_write completes.
void
Peer::on_write_response (error_code ec, std::size_t bytes_transferred,
    SharedBuffer const& buf)
{
    timer_.cancel (ec);

    if (journal_.trace)
    {
        if (! ec_)
            journal_.trace <<
                id_ << " on_write_response, " <<
                    bytes_transferred << " bytes";
        else
            journal_.trace <<
                id_ << " on_write_response failed, " <<
                    ec.message();
    }

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        failed (ec);
        return;
    }

    bytes_out_ += bytes_transferred;

    assert (pending_writes_ > 0);
    if (--pending_writes_ > 0)
        return;

    if (closed_)
    {
        socket_->shutdown (socket::shutdown_send, ec);
        return;

    }
}

//------------------------------------------------------------------------------

// Send a copy of the data.
void
Peer::write (void const* buffer, std::size_t bytes)
{
    server_.get_io_service().dispatch (strand_.wrap (
        std::bind (&Peer::async_write, shared_from_this(),
            SharedBuffer (static_cast <char const*> (buffer), bytes))));
}

// Called to indicate the response has been written (but not sent)
void
Peer::complete()
{
    if (! strand_.running_in_this_thread())
        return server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::complete, shared_from_this())));

    message_ = beast::http::message{};
    parser_ = beast::http::parser{message_, true};

    on_read_request (error_code{}, 0);
}

// Make the Session asynchronous
void
Peer::detach ()
{
    if (detached_.exchange (1) == 0)
    {
        assert (! work_);
        assert (detach_ref_ == nullptr);

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

// Called from the Handler to close the session.
void
Peer::close (bool graceful)
{
    closed_ = true;

    if (! strand_.running_in_this_thread())
        return server_.get_io_service().dispatch (strand_.wrap (
            std::bind (&Peer::close, shared_from_this(), graceful)));

    if (! graceful || pending_writes_ == 0)
    {
        // We should cancel pending i/o

        if (pending_writes_ == 0)
        {
        }
    }

    // Release our additional reference
    detach_ref_.reset();
}

}
}
