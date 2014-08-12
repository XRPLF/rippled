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

namespace ripple {
namespace HTTP {

Peer::Peer (ServerImpl& impl, Port const& port)
    : impl_ (impl)
    , strand_ (impl_.get_io_service())
    , data_timer_ (impl_.get_io_service())
    , request_timer_ (impl_.get_io_service())
    , buffer_ (bufferSize)
    , writesPending_ (0)
    , closed_ (false)
    , callClose_ (false)
    , errorCode_ (0)
    , detached_ (0)
{
    tag = nullptr;

    int flags = MultiSocket::Flag::server_role;
    switch (port.security)
    {
    default:
        bassertfalse;

    case Port::no_ssl:
        break;

    case Port::allow_ssl:
        flags |= MultiSocket::Flag::ssl;
        break;

    case Port::require_ssl:
        flags |= MultiSocket::Flag::ssl_required;
        break;
    }

    socket_.reset (MultiSocket::New (
        impl_.get_io_service(), port.context->get(), flags));

    impl_.add (*this);
}

Peer::~Peer ()
{
    if (callClose_)
        impl_.handler().onClose (session(), errorCode_);

    impl_.remove (*this);
}

//--------------------------------------------------------------------------

// Returns the Content-Body as a single buffer.
// VFALCO NOTE This is inefficient...
std::string
Peer::content()
{
    std::string s;
    beast::DynamicBuffer const& body (
        parser_.request()->body ());
    s.resize (body.size ());
    boost::asio::buffer_copy (
        boost::asio::buffer (&s[0],
            s.size()), body.data <boost::asio::const_buffer>());
    return s;
}

// Send a copy of the data.
void
Peer::write (void const* buffer, std::size_t bytes)
{
    // Make sure this happens on an io_service thread.
    impl_.get_io_service().dispatch (strand_.wrap (
        std::bind (&Peer::async_write, shared_from_this(),
            SharedBuffer (static_cast <char const*> (buffer), bytes))));
}

// Make the Session asynchronous
void
Peer::detach ()
{
    if (detached_.exchange (1) == 0)
    {
        bassert (! work_);
        bassert (detach_ref_ == nullptr);

        // Maintain an additional reference while detached
        detach_ref_ = shared_from_this();

        // Prevent the io_service from running out of work.
        // The work object will be destroyed with the Peer
        // after the Session is closed and handlers complete.
        //
        work_ = boost::in_place (std::ref (
            impl_.get_io_service()));
    }
}

// Called by the Handler to close the session.
void
Peer::close ()
{
    // Make sure this happens on an io_service thread.
    impl_.get_io_service().dispatch (strand_.wrap (
        std::bind (&Peer::handle_close, shared_from_this())));
}

//--------------------------------------------------------------------------

// Called when the handshake completes
//
void
Peer::handle_handshake (error_code ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        failed (ec);
        return;
    }

    async_read_some();
}

// Called when the data timer expires
//
void
Peer::handle_data_timer (error_code ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (closed_)
        return;

    if (ec != 0)
    {
        failed (ec);
        return;
    }

    failed (boost::system::errc::make_error_code (
        boost::system::errc::timed_out));
}

// Called when the request timer expires
//
void
Peer::handle_request_timer (error_code ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (closed_)
        return;

    if (ec != 0)
    {
        failed (ec);
        return;
    }

    failed (boost::system::errc::make_error_code (
        boost::system::errc::timed_out));
}

// Called when async_write completes.
void
Peer::handle_write (error_code ec, std::size_t bytes_transferred,
    SharedBuffer const& buf)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        failed (ec);
        return;
    }

    bassert (writesPending_ > 0);
    if (--writesPending_ == 0 && closed_)
        socket_->shutdown (socket::shutdown_send, ec);
}

// Called when async_read_some completes.
void
Peer::handle_read (error_code ec, std::size_t bytes_transferred)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0 && ec != boost::asio::error::eof)
    {
        failed (ec);
        return;
    }

    std::size_t const bytes_parsed (parser_.process (
        buffer_.getData(), bytes_transferred));

    if (parser_.error() ||
        bytes_parsed != bytes_transferred)
    {
        failed (boost::system::errc::make_error_code (
            boost::system::errc::bad_message));
        return;
    }

    if (ec == boost::asio::error::eof)
    {
        parser_.process_eof();
        ec = error_code();
    }

    if (parser_.error())
    {
        failed (boost::system::errc::make_error_code (
            boost::system::errc::bad_message));
        return;
    }

    if (! parser_.finished())
    {
        // Feed some headers to the callback
        if (parser_.fields().size() > 0)
        {
            handle_headers();
            if (closed_)
                return;
        }
    }

    if (parser_.finished ())
    {
        data_timer_.cancel();
        // VFALCO NOTE: Should we cancel this one?
        request_timer_.cancel();

        if (! socket_->needs_handshake())
            socket_->shutdown (socket::shutdown_receive, ec);

        handle_request ();
        return;
    }

    async_read_some();
}

// Called when we have some new headers.
void
Peer::handle_headers ()
{
    impl_.handler().onHeaders (session());
}

// Called when we have a complete http request.
void
Peer::handle_request ()
{
    // This is to guarantee onHeaders is called at least once.
    handle_headers();

    if (closed_)
        return;

    // Process the HTTPRequest
    impl_.handler().onRequest (session());
}

// Called to close the session.
void
Peer::handle_close ()
{
    closed_ = true;

    // Release our additional reference
    detach_ref_.reset();
}

//--------------------------------------------------------------------------
//
// Peer
//

// Called when the acceptor accepts our socket.
void
Peer::accept ()
{
    callClose_ = true;

    impl_.handler().onAccept (session());

    if (closed_)
    {
        cancel();
        return;
    }

    request_timer_.expires_from_now (
        boost::posix_time::seconds (
            requestTimeoutSeconds));

    request_timer_.async_wait (strand_.wrap (std::bind (
        &Peer::handle_request_timer, shared_from_this(),
            beast::asio::placeholders::error)));

    if (socket_->needs_handshake ())
    {
        socket_->async_handshake (beast::asio::abstract_socket::server,
            strand_.wrap (std::bind (&Peer::handle_handshake, shared_from_this(),
                beast::asio::placeholders::error)));
    }
    else
    {
        async_read_some();
    }
}

// Cancel all pending i/o and timers and send tcp shutdown.
void
Peer::cancel ()
{
    error_code ec;
    data_timer_.cancel (ec);
    request_timer_.cancel (ec);
    socket_->cancel (ec);
    socket_->shutdown (socket::shutdown_both, ec);
}

// Called by a completion handler when error is not eof or aborted.
void
Peer::failed (error_code const& ec)
{
    errorCode_ = ec.value();
    bassert (errorCode_ != 0);
    cancel ();
}

// Call the async_read_some initiating function.
void
Peer::async_read_some ()
{
    // re-arm the data timer
    // (this cancels the previous wait, if any)
    //
    data_timer_.expires_from_now (
        boost::posix_time::seconds (
            dataTimeoutSeconds));

    data_timer_.async_wait (strand_.wrap (std::bind (
        &Peer::handle_data_timer, shared_from_this(),
            beast::asio::placeholders::error)));

    // issue the read
    //
    boost::asio::mutable_buffers_1 buf (
        buffer_.getData (), buffer_.getSize ());

    socket_->async_read_some (buf, strand_.wrap (
        std::bind (&Peer::handle_read, shared_from_this(),
            beast::asio::placeholders::error,
                beast::asio::placeholders::bytes_transferred)));
}

// Send a shared buffer
void
Peer::async_write (SharedBuffer const& buf)
{
    bassert (buf.get().size() > 0);

    ++writesPending_;

    // Send the copy. We pass the SharedBuffer in the last parameter
    // so that a reference is maintained as the handler gets copied.
    // When the final completion function returns, the reference
    // count will drop to zero and the buffer will be freed.
    //
    boost::asio::async_write (*socket_,
        boost::asio::const_buffers_1 (&(*buf)[0], buf->size()),
            strand_.wrap (std::bind (&Peer::handle_write,
                shared_from_this(), beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred, buf)));
}

}
}
