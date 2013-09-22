//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace HTTP {

Peer::Peer (ServerImpl& impl, Port const& port)
    : m_impl (impl)
    , m_strand (m_impl.get_io_service())
    , m_data_timer (m_impl.get_io_service())
    , m_request_timer (m_impl.get_io_service())
    , m_buffer (bufferSize)
    , m_parser (HTTPParser::typeRequest)
    , m_session (*this)
    , m_writesPending (0)
    , m_closed (false)
    , m_callClose (false)
{
    int flags;
    switch (port.security)
    {
    default:
        bassertfalse;
    case Port::no_ssl:      flags = MultiSocket::none; break;
    case Port::allow_ssl:   flags = MultiSocket::server_ssl; break;
    case Port::require_ssl: flags = MultiSocket::server_ssl_required; break;
    }

    m_socket = MultiSocket::New (m_impl.get_io_service(), port.context->get(), flags);

    m_impl.add (*this);
}

Peer::~Peer ()
{
    if (m_callClose)
        m_impl.handler().onClose (m_session);

    m_impl.remove (*this);
}

// Returns the asio socket for the peer.
//
socket& Peer::get_socket()
{
    return m_socket->this_layer<socket>();
}

// Return the Session associated with this peer's session.
//
SessionImpl& Peer::session ()
{
    return m_session;
}

// Indicates that the Handler closed the Session
//
void Peer::close ()
{
    // Make sure this happens on an i/o service thread.
    m_impl.get_io_service().dispatch (m_strand.wrap (
        boost::bind (&Peer::handle_close, Ptr (this),
            CompletionCounter (this))));
}

// Cancels all pending i/o and timers and sends tcp shutdown.
//
void Peer::cancel ()
{
    error_code ec;
    m_data_timer.cancel (ec);
    m_request_timer.cancel (ec);
    m_socket->cancel (ec);
    m_socket->shutdown (socket::shutdown_both);
}

// Called when I/O completes with an error that is not eof or aborted.
//
void Peer::failed (error_code ec)
{
    cancel ();
}

// Called when there are no more completion handlers pending.
//
void Peer::asyncHandlersComplete ()
{
}

// Send a copy of the data.
//
void Peer::write (void const* buffer, std::size_t bytes)
{
    SharedBuffer buf (static_cast <char const*> (buffer), bytes);
    // Make sure this happens on an i/o service thread.
    m_impl.get_io_service().dispatch (m_strand.wrap (
        boost::bind (&Peer::handle_write, Ptr (this),
            buf, CompletionCounter (this))));
}

// Called from an io_service thread to write the shared buffer.
//
void Peer::handle_write (SharedBuffer const& buf, CompletionCounter)
{
    async_write (buf);
}

// Send a shared buffer
//
void Peer::async_write (SharedBuffer const& buf)
{
    bassert (buf.get().size() > 0);

    ++m_writesPending;

    // Send the copy. We pass the SharedArg in the last parameter
    // so that a reference is maintained as the handler gets copied.
    // When the final completion function returns, the reference
    // count will drop to zero and the buffer will be freed.
    //
    boost::asio::async_write (*m_socket,
        boost::asio::const_buffers_1 (&buf->front(), buf->size()),
            m_strand.wrap (boost::bind (&Peer::handle_write,
                Ptr (this), boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred,
                        buf, CompletionCounter (this))));
}

// Calls the async_read_some initiating function.
//
void Peer::async_read_some ()
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

// Called when the acceptor gives us the connection.
//
void Peer::handle_accept ()
{
    m_callClose = true;

    // save remote addr
    m_session.remoteAddress = from_asio (
        get_socket().remote_endpoint()).withPort (0);
    m_impl.handler().onAccept (m_session);

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
        m_socket->async_handshake (Socket::server, m_strand.wrap (
            boost::bind (&Peer::handle_handshake, Ptr(this),
                boost::asio::placeholders::error,
                    CompletionCounter (this))));
    }
    else
    {
        async_read_some();
    }
}

// Called when the handshake completes
//
void Peer::handle_handshake (error_code ec, CompletionCounter)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        // fail
        return;
    }

    async_read_some();
}

// Called when the data timer expires
//
void Peer::handle_data_timer (error_code ec, CompletionCounter)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        // fail
        return;
    }

    // They took too long to send any bytes
    cancel();
}

// Called when the request timer expires
//
void Peer::handle_request_timer (error_code ec, CompletionCounter)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec != 0)
    {
        // fail
        return;
    }

    // They took too long to complete the request
    cancel();
}

// Called when the Session is closed by the Handler.
//
void Peer::handle_close (CompletionCounter)
{
    m_closed = true;
    m_session.handle_close();
}

// Called when async_write completes.
//
void Peer::handle_write (error_code ec, std::size_t bytes_transferred,
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
    {
        m_socket->shutdown (socket::shutdown_send);
    }
}

// Called when async_read_some completes.
//
void Peer::handle_read (error_code ec, std::size_t bytes_transferred, CompletionCounter)
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
        // set ec manually and call failed()
        return;
    }

    if (ec == boost::asio::error::eof)
    {
        m_parser.process_eof();
        ec = error_code();
    }

    if (m_parser.error())
    {
        // set ec manually and call failed()
        return;
    }

    if (! m_parser.finished())
    {
        // Feed some headers to the callback
        if (m_parser.fields().size() > 0)
        {
            handle_headers ();
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
//
void Peer::handle_headers ()
{
    m_session.headersComplete = m_parser.headersComplete();
    m_session.headers = HTTPHeaders (m_parser.fields());
    m_impl.handler().onHeaders (m_session);
}

// Called when we have a complete http request.
//
void Peer::handle_request ()
{
    // This is to guarantee onHeaders is called at least once.
    handle_headers();

    if (m_closed)
        return;

    m_session.request = m_parser.request();

    // Turn the Content-Body into a linear buffer.
    ContentBodyBuffer const& body (m_session.request->body ());
    m_session.content.resize (body.size ());
    boost::asio::buffer_copy (
        boost::asio::buffer (&m_session.content.front(),
            m_session.content.size()), body.data());

    // Process the HTTPRequest
    m_impl.handler().onRequest (m_session);
}

}
}
