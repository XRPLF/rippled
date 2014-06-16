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

#ifndef RIPPLE_HTTP_PEER_H_INCLUDED
#define RIPPLE_HTTP_PEER_H_INCLUDED

#include <ripple/http/api/Session.h>
#include <ripple/common/MultiSocket.h>
#include <beast/module/asio/async/AsyncObject.h>
#include <beast/module/asio/basics/SharedArg.h>
#include <beast/module/asio/http/HTTPRequestParser.h>
#include <boost/bind.hpp>
#include <memory>

namespace ripple {
namespace HTTP {

// Holds the copy of buffers being sent
typedef beast::asio::SharedArg <std::string> SharedBuffer;

/** Represents an active connection. */
class Peer
    : public beast::SharedObject
    , public beast::asio::AsyncObject <Peer>
    , public Session
    , public beast::List <Peer>::Node
    , public beast::LeakChecked <Peer>
{
public:
    enum
    {
        // Size of our receive buffer
        bufferSize = 8192,

        // Largest HTTP request allowed
        maxRequestBytes = 32 * 1024,

        // Max seconds without receiving a byte
        dataTimeoutSeconds = 10,

        // Max seconds without completing the request
        requestTimeoutSeconds = 30

    };

    typedef beast::SharedPtr <Peer> Ptr;

    ServerImpl& m_impl;
    boost::asio::io_service::strand m_strand;
    boost::asio::deadline_timer m_data_timer;
    boost::asio::deadline_timer m_request_timer;
    std::unique_ptr <MultiSocket> m_socket;
    beast::MemoryBlock m_buffer;
    beast::HTTPRequestParser m_parser;
    int m_writesPending;
    bool m_closed;
    bool m_callClose;
    beast::SharedPtr <Peer> m_detach_ref;
    boost::optional <boost::asio::io_service::work> m_work;
    int m_errorCode;
    std::atomic <int> m_detached;

    //--------------------------------------------------------------------------

    Peer (ServerImpl& impl, Port const& port)
        : m_impl (impl)
        , m_strand (m_impl.get_io_service())
        , m_data_timer (m_impl.get_io_service())
        , m_request_timer (m_impl.get_io_service())
        , m_buffer (bufferSize)
        , m_writesPending (0)
        , m_closed (false)
        , m_callClose (false)
        , m_errorCode (0)
        , m_detached (0)
    {
        tag = nullptr;

        int flags;
        switch (port.security)
        {
        default:
            bassertfalse;
        case Port::no_ssl:      flags = MultiSocket::none; break;
        case Port::allow_ssl:   flags = MultiSocket::server_ssl; break;
        case Port::require_ssl: flags = MultiSocket::server_ssl_required; break;
        }

        m_socket.reset (MultiSocket::New (
            m_impl.get_io_service(), port.context->get(), flags));

        m_impl.add (*this);
    }

    ~Peer ()
    {
        if (m_callClose)
            m_impl.handler().onClose (session(), m_errorCode);

        m_impl.remove (*this);
    }

    //--------------------------------------------------------------------------
    //
    // Session
    //

    beast::Journal journal()
    {
        return m_impl.journal();
    }

    beast::IP::Endpoint remoteAddress()
    {
        return from_asio (get_socket().remote_endpoint());
    }

    bool headersComplete()
    {
        return m_parser.headersComplete();
    }

    beast::HTTPHeaders headers()
    {
        return beast::HTTPHeaders (m_parser.fields());
    }

    beast::SharedPtr <beast::HTTPRequest> const& request()
    {
        return m_parser.request();
    }

    // Returns the Content-Body as a single buffer.
    // VFALCO NOTE This is inefficient...
    std::string content()
    {
        std::string s;
        beast::DynamicBuffer const& body (
            m_parser.request()->body ());
        s.resize (body.size ());
        boost::asio::buffer_copy (
            boost::asio::buffer (&s[0],
                s.size()), body.data <boost::asio::const_buffer>());
        return s;
    }

    // Send a copy of the data.
    void write (void const* buffer, std::size_t bytes)
    {
        // Make sure this happens on an io_service thread.
        m_impl.get_io_service().dispatch (m_strand.wrap (
            boost::bind (&Peer::handle_write, Ptr (this),
                SharedBuffer (static_cast <char const*> (buffer), bytes),
                    CompletionCounter (this))));
    }

    // Make the Session asynchronous
    void detach ()
    {
        if (m_detached.exchange (1) == 0)
        {
            bassert (! m_work);
            bassert (m_detach_ref.empty());

            // Maintain an additional reference while detached
            m_detach_ref = this;

            // Prevent the io_service from running out of work.
            // The work object will be destroyed with the Peer
            // after the Session is closed and handlers complete.
            //
            m_work = boost::in_place (std::ref (
                m_impl.get_io_service()));
        }
    }

    // Called by the Handler to close the session.
    void close ()
    {
        // Make sure this happens on an io_service thread.
        m_impl.get_io_service().dispatch (m_strand.wrap (
            boost::bind (&Peer::handle_close, Ptr (this),
                CompletionCounter (this))));
    }

    //--------------------------------------------------------------------------
    //
    // Completion Handlers
    //

    // Called when the last pending completion handler returns.
    void asyncHandlersComplete ()
    {
    }

    // Called when the acceptor accepts our socket.
    void handle_accept ()
    {
        m_callClose = true;

        m_impl.handler().onAccept (session());

        if (m_closed)
        {
            cancel();
            return;
        }

        m_request_timer.expires_from_now (
            boost::posix_time::seconds (
                requestTimeoutSeconds));

        m_request_timer.async_wait (m_strand.wrap (boost::bind (
            &Peer::handle_request_timer, Ptr(this),
                boost::asio::placeholders::error,
                    CompletionCounter (this))));

        if (m_socket->needs_handshake ())
        {
            m_socket->async_handshake (beast::asio::abstract_socket::server,
                m_strand.wrap (boost::bind (&Peer::handle_handshake, Ptr(this),
                    boost::asio::placeholders::error,
                        CompletionCounter (this))));
        }
        else
        {
            async_read_some();
        }
    }

    // Called from an io_service thread to write the shared buffer.
    void handle_write (SharedBuffer const& buf, CompletionCounter)
    {
        async_write (buf);
    }

    // Called when the handshake completes
    //
    void handle_handshake (error_code ec, CompletionCounter)
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
    void handle_data_timer (error_code ec, CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (m_closed)
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
    void handle_request_timer (error_code ec, CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (m_closed)
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
    void handle_write (error_code ec, std::size_t bytes_transferred,
        SharedBuffer buf, CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec != 0)
        {
            failed (ec);
            return;
        }

        bassert (m_writesPending > 0);
        if (--m_writesPending == 0 && m_closed)
            m_socket->shutdown (socket::shutdown_send);
    }

    // Called when async_read_some completes.
    void handle_read (error_code ec, std::size_t bytes_transferred, CompletionCounter)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec != 0 && ec != boost::asio::error::eof)
        {
            failed (ec);
            return;
        }

        std::size_t const bytes_parsed (m_parser.process (
            m_buffer.getData(), bytes_transferred));

        if (m_parser.error() ||
            bytes_parsed != bytes_transferred)
        {
            failed (boost::system::errc::make_error_code (
                boost::system::errc::bad_message));
            return;
        }

        if (ec == boost::asio::error::eof)
        {
            m_parser.process_eof();
            ec = error_code();
        }

        if (m_parser.error())
        {
            failed (boost::system::errc::make_error_code (
                boost::system::errc::bad_message));
            return;
        }

        if (! m_parser.finished())
        {
            // Feed some headers to the callback
            if (m_parser.fields().size() > 0)
            {
                handle_headers();
                if (m_closed)
                    return;
            }
        }

        if (m_parser.finished ())
        {
            m_data_timer.cancel();
            // VFALCO NOTE: Should we cancel this one?
            m_request_timer.cancel();

            if (! m_socket->needs_handshake())
                m_socket->shutdown (socket::shutdown_receive);

            handle_request ();
            return;
        }

        async_read_some();
    }

    // Called when we have some new headers.
    void handle_headers ()
    {
        m_impl.handler().onHeaders (session());
    }

    // Called when we have a complete http request.
    void handle_request ()
    {
        // This is to guarantee onHeaders is called at least once.
        handle_headers();

        if (m_closed)
            return;

        // Process the HTTPRequest
        m_impl.handler().onRequest (session());
    }

    // Called to close the session.
    void handle_close (CompletionCounter)
    {
        m_closed = true;

        // Release our additional reference
        m_detach_ref = nullptr;
    }

    //--------------------------------------------------------------------------
    //
    // Peer
    //

    // Returns the asio socket for the peer.
    socket& get_socket()
    {
        return m_socket->this_layer<socket>();
    }

    // Return the Session associated with this peer's session.
    Session& session ()
    {
        return *this;
    }

    // Cancel all pending i/o and timers and send tcp shutdown.
    void cancel ()
    {
        error_code ec;
        m_data_timer.cancel (ec);
        m_request_timer.cancel (ec);
        m_socket->cancel (ec);
        m_socket->shutdown (socket::shutdown_both);
    }

    // Called by a completion handler when error is not eof or aborted.
    void failed (error_code const& ec)
    {
        m_errorCode = ec.value();
        bassert (m_errorCode != 0);
        cancel ();
    }

    // Call the async_read_some initiating function.
    void async_read_some ()
    {
        // re-arm the data timer
        // (this cancels the previous wait, if any)
        //
        m_data_timer.expires_from_now (
            boost::posix_time::seconds (
                dataTimeoutSeconds));

        m_data_timer.async_wait (m_strand.wrap (boost::bind (
            &Peer::handle_data_timer, Ptr(this),
                boost::asio::placeholders::error,
                    CompletionCounter (this))));

        // issue the read
        //
        boost::asio::mutable_buffers_1 buf (
            m_buffer.getData (), m_buffer.getSize ());

        m_socket->async_read_some (buf, m_strand.wrap (
            boost::bind (&Peer::handle_read, Ptr (this),
                boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred,
                        CompletionCounter (this))));
    }

    // Send a shared buffer
    void async_write (SharedBuffer const& buf)
    {
        bassert (buf.get().size() > 0);

        ++m_writesPending;

        // Send the copy. We pass the SharedBuffer in the last parameter
        // so that a reference is maintained as the handler gets copied.
        // When the final completion function returns, the reference
        // count will drop to zero and the buffer will be freed.
        //
        boost::asio::async_write (*m_socket,
            boost::asio::const_buffers_1 (&(*buf)[0], buf->size()),
                m_strand.wrap (boost::bind (&Peer::handle_write,
                    Ptr (this), boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred,
                            buf, CompletionCounter (this))));
    }

    // Send a copy of a BufferSequence
    template <typename BufferSequence>
    void async_write (BufferSequence const& buffers)
    {
        // Iterate over each linear vector in the BufferSequence.
        for (typename BufferSequence::const_iterator iter (buffers.begin());
            iter != buffers.end(); ++iter)
        {
            typename BufferSequence::value_type const& buffer (*iter);

            // Put a copy of this section of the buffer sequence into
            // a reference counted, shared container.
            //
            async_write (SharedBuffer (
                boost::asio::buffer_cast <char const*> (buffer),
                    boost::asio::buffer_size (buffer)));
        }
    }
};

}
}

#endif
