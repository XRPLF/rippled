//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "../ripple_net/ripple_net.h"

namespace ripple {

using namespace beast;

//------------------------------------------------------------------------------

HTTPServer::Port::Port ()
    : port (0)
    , security (no_ssl)
    , context (nullptr)
{
}

HTTPServer::Port::Port (Port const& other)
    : port (other.port)
    , addr (other.addr)
    , security (other.security)
    , context (other.context)
{
}

HTTPServer::Port& HTTPServer::Port::operator= (Port const& other)
{
    port = other.port;
    addr = other.addr;
    security = other.security;
    context = other.context;
    return *this;
}

HTTPServer::Port::Port (
    uint16 port_,
    IPEndpoint const& addr_,
    Security security_,
    SSLContext* context_)
    : port (port_)
    , addr (addr_)
    , security (security_)
    , context (context_)
{
}

int compare (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs)
{
    int comp;
    
    comp = compare (lhs.addr, rhs.addr);
    if (comp != 0)
        return comp;

    if (lhs.port < rhs.port)
        return -1;
    else if (lhs.port > rhs.port)
        return 1;

    if (lhs.security < rhs.security)
        return -1;
    else if (lhs.security > rhs.security)
        return 1;

    // 'context' does not participate in the comparison

    return 0;
}

bool operator== (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs) { return compare (lhs, rhs) == 0; }
bool operator!= (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs) { return compare (lhs, rhs) != 0; }
bool operator<  (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs) { return compare (lhs, rhs) <  0; }
bool operator<= (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs) { return compare (lhs, rhs) <= 0; }
bool operator>  (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs) { return compare (lhs, rhs) >  0; }
bool operator>= (HTTPServer::Port const& lhs, HTTPServer::Port const& rhs) { return compare (lhs, rhs) >= 0; }

//------------------------------------------------------------------------------

HTTPServer::ScopedStream::ScopedStream (Session& session)
    : m_session (session)
{
}

HTTPServer::ScopedStream::ScopedStream (ScopedStream const& other)
    : m_session (other.m_session)
{
}

HTTPServer::ScopedStream::ScopedStream (Session& session,
                                        std::ostream& manip (std::ostream&))
    : m_session (session)
{
    m_ostream << manip;
}

HTTPServer::ScopedStream::~ScopedStream ()
{
    if (! m_ostream.str().empty())
        m_session.write (m_ostream.str());
}

std::ostream& HTTPServer::ScopedStream::operator<< (std::ostream& manip (std::ostream&)) const
{
    return m_ostream << manip;
}

std::ostringstream& HTTPServer::ScopedStream::ostream () const
{
    return m_ostream;
}

//------------------------------------------------------------------------------

HTTPServer::Session::Session ()
    : headersComplete (false)
    , tag (nullptr)
{
    content.reserve (1000);
    reply.reserve (1000);
}

HTTPServer::ScopedStream HTTPServer::Session::operator<< (
    std::ostream& manip (std::ostream&))
{
    return ScopedStream (*this, manip);
}

//------------------------------------------------------------------------------

class HTTPServer::Impl : public Thread
{
public:
    typedef boost::system::error_code error_code;
    typedef boost::asio::ip::tcp Protocol;
    typedef boost::asio::ip::address address;
    typedef Protocol::endpoint endpoint_t;
    typedef Protocol::acceptor acceptor;
    typedef Protocol::socket socket;

    static std::string to_string (address const& addr)
    {
        return addr.to_string();
    }

    static std::string to_string (endpoint_t const& endpoint)
    {
        std::stringstream ss;
        ss << to_string (endpoint.address());
        if (endpoint.port() != 0)
            ss << ":" << std::dec << endpoint.port();
        return std::string (ss.str());
    }

    static endpoint_t to_asio (Port const& port)
    {
        if (port.addr.isV4())
        {
            IPEndpoint::V4 v4 (port.addr.v4());
            std::string const& s (v4.to_string());
            return endpoint_t (address().from_string (s), port.port);
        }

        //IPEndpoint::V6 v6 (ep.v6());
        return endpoint_t ();
    }

    static IPEndpoint from_asio (endpoint_t const& endpoint)
    {
        std::stringstream ss (to_string (endpoint));
        IPEndpoint ep;
        ss >> ep;
        return ep;
    }

    //--------------------------------------------------------------------------

    // Holds the copy of buffers being sent
    typedef SharedArg <std::string> SharedBuffer;

    class Peer;

    class SessionImp : public Session
    {
    public:
        Peer& m_peer;
        bool m_closed;
        boost::optional <boost::asio::io_service::work> m_work;

        explicit SessionImp (Peer& peer)
            : m_peer (peer)
            , m_closed (false)
        {
        }

        ~SessionImp ()
        {
        }

        bool closed() const
        {
            return m_closed;
        }

        void write (void const* buffer, std::size_t bytes)
        {
            m_peer.write (buffer, bytes);
        }

        void close()
        {
            m_closed = true;
        }

        void detach()
        {
            if (! m_work)
                m_work = boost::in_place (boost::ref (
                    m_peer.m_impl.get_io_service()));
        }
    };

    //--------------------------------------------------------------------------

    /** Represents an active connection. */
    class Peer
        : public SharedObject
        , public AsyncObject <Peer>
        , public List <Peer>::Node
        , public LeakChecked <Peer>
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

        typedef SharedPtr <Peer> Ptr;

        Impl& m_impl;
        boost::asio::io_service::strand m_strand;
        boost::asio::deadline_timer m_data_timer;
        boost::asio::deadline_timer m_request_timer;
        ScopedPointer <MultiSocket> m_socket;
        MemoryBlock m_buffer;
        HTTPParser m_parser;
        SessionImp m_session;
        int m_writesPending;
        bool m_callClose;

        Peer (Impl& impl, Port const& port)
            : m_impl (impl)
            , m_strand (m_impl.get_io_service())
            , m_data_timer (m_impl.get_io_service())
            , m_request_timer (m_impl.get_io_service())
            , m_buffer (bufferSize)
            , m_parser (HTTPParser::typeRequest)
            , m_session (*this)
            , m_writesPending (0)
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

        ~Peer ()
        {
            if (m_callClose)
                m_impl.handler().onClose (m_session);

            m_impl.remove (*this);
        }

        // Returns the asio socket for the peer.
        //
        socket& get_socket()
        {
            return m_socket->this_layer<socket>();
        }

        // Return the Session associated with this peer's session.
        //
        SessionImp& session ()
        {
            return m_session;
        }

        // Cancels all pending i/o and timers and sends tcp shutdown.
        //
        void cancel ()
        {
            error_code ec;
            m_data_timer.cancel (ec);
            m_request_timer.cancel (ec);
            m_socket->cancel (ec);
            m_socket->shutdown (socket::shutdown_both);
        }

        // Called when I/O completes with an error that is not eof or aborted.
        //
        void failed (error_code ec)
        {
            cancel ();
        }

        // Called when there are no more completion handlers pending.
        //
        void asyncHandlersComplete ()
        {
        }

        // Send a copy of the data.
        //
        void write (void const* buffer, std::size_t bytes)
        {
            SharedBuffer buf (static_cast <char const*> (buffer), bytes);
            // Make sure this happens on an i/o service thread.
            m_impl.get_io_service().dispatch (m_strand.wrap (
                boost::bind (&Peer::handle_write, Ptr (this),
                    buf, CompletionCounter (this))));
        }

        // Called from an io_service thread to write the shared buffer.
        //
        void handle_write (SharedBuffer const& buf, CompletionCounter)
        {
            async_write (buf);
        }

        // Send a shared buffer
        //
        void async_write (SharedBuffer const& buf)
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

        // Send a copy of the buffer sequence.
        //
        template <typename BufferSequence>
        void async_write (BufferSequence const& buffers)
        {
            // Count the number of buffers
            std::size_t const nbuf (std::distance (
                buffers.begin(), buffers.end()));

            // Iterate over each linear vector in the BufferSequence.
            for (typename BufferSequence::const_iterator iter (buffers.begin());
                iter != buffers.end(); ++iter)
            {
                typename BufferSequence::value_type const& buffer (*iter);

                // Put a copy of this section of the buffer sequence into
                // a reference counted, shared container.
                //
                SharedBuffer buf (
                    boost::asio::buffer_cast <char const*> (buffer),
                        boost::asio::buffer_size (buffer));

                async_write (buf);
            }
        }

        // Calls the async_read_some initiating function.
        //
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

        // Sends a copy of the reply in the session if it is not empty.
        // Returns `true` if m_session.closed is `true`
        // On return, reply.empty() will return `true`.
        //
        void maybe_send_reply ()
        {
            if (! m_session.reply.empty())
            {
                async_write (boost::asio::const_buffers_1 (
                    &m_session.reply.front(), m_session.reply.size()));
                m_session.reply.clear();
            }
        }

        // Called when the acceptor gives us the connection.
        //
        void handle_accept ()
        {
            m_callClose = true;

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
        void handle_handshake (error_code ec, CompletionCounter)
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
        void handle_data_timer (error_code ec, CompletionCounter)
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
        void handle_request_timer (error_code ec, CompletionCounter)
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

        // Called when async_write completes.
        //
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
            if (--m_writesPending == 0 && m_session.closed())
            {
                m_socket->shutdown (socket::shutdown_send);
            }
        }

        // Called when async_read_some completes.
        //
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
                    if (m_session.closed())
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
        void handle_headers ()
        {
            m_session.headersComplete = m_parser.headersComplete();
            m_session.headers = HTTPHeaders (m_parser.fields());
            m_impl.handler().onHeaders (m_session);

            maybe_send_reply ();
        }

        // Called when we have a complete http request.
        //
        void handle_request ()
        {
            // This is to guarantee onHeaders is called at least once.
            handle_headers();

            if (m_session.closed())
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

            maybe_send_reply ();
        }
    };

    //--------------------------------------------------------------------------

    /** A listening socket. */
    class Door
        : public SharedObject
        , public AsyncObject <Door>
        , public List <Door>::Node
        , public LeakChecked <Door>
    {
    public:
        typedef SharedPtr <Door> Ptr;

        Impl& m_impl;
        acceptor m_acceptor;
        Port m_port;

        Door (Impl& impl, Port const& port)
            : m_impl (impl)
            , m_acceptor (m_impl.get_io_service(), to_asio (port))
            , m_port (port)
        {
            m_impl.add (*this);

            error_code ec;

            m_acceptor.set_option (acceptor::reuse_address (true), ec);
            if (ec)
            {
                m_impl.journal().error <<
                    "Error setting acceptor socket option: " << ec.message();
            }

            if (! ec)
            {
                m_impl.journal().info << "Bound to endpoint " <<
                    to_string (m_acceptor.local_endpoint());

                async_accept();
            }
            else
            {
                m_impl.journal().error << "Error binding to endpoint " <<
                    to_string (m_acceptor.local_endpoint()) <<
                    ", '" << ec.message() << "'";
            }
        }

        ~Door ()
        {
            m_impl.remove (*this);
        }

        Port const& port () const
        {
            return m_port;
        }

        void cancel ()
        {
            m_acceptor.cancel();
        }

        void failed (error_code ec)
        {
        }

        void asyncHandlersComplete ()
        {
        }

        void async_accept ()
        {
            Peer* peer (new Peer (m_impl, m_port));
            m_acceptor.async_accept (peer->get_socket(), boost::bind (
                &Door::handle_accept, Ptr(this),
                    boost::asio::placeholders::error,
                        Peer::Ptr (peer), CompletionCounter (this)));
        }

        void handle_accept (error_code ec, Peer::Ptr peer, CompletionCounter)
        {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (ec)
            {
                m_impl.journal().error << "Accept failed: " << ec.message();
                return;
            }

            async_accept();

            // Save remote address in session
            peer->session().remoteAddress = from_asio (
                peer->get_socket().remote_endpoint()).withPort (0);
            m_impl.handler().onAccept (peer->session());

            if (peer->session().closed())
            {
                peer->cancel();
                return;
            }

            peer->handle_accept();
        }
    };

    //--------------------------------------------------------------------------

    struct State
    {
        // Attributes for our listening ports
        Ports ports;

        // All allocated Peer objects
        List <Peer> peers;

        // All allocated Door objects
        List <Door> doors;
    };

    typedef SharedData <State> SharedState;
    typedef std::vector <Door::Ptr> Doors;

    HTTPServer& m_server;
    Handler& m_handler;
    Journal m_journal;
    boost::asio::io_service m_io_service;
    boost::asio::io_service::strand m_strand;
    boost::optional <boost::asio::io_service::work> m_work;
    WaitableEvent m_stopped;
    SharedState m_state;
    Doors m_doors;

    //--------------------------------------------------------------------------

    Impl (HTTPServer& server, Handler& handler, Journal journal)
        : Thread ("RPC::HTTPServer")
        , m_server (server)
        , m_handler (handler)
        , m_journal (journal)
        , m_strand (m_io_service)
        , m_work (boost::in_place (boost::ref (m_io_service)))
        , m_stopped (true)
    {
        startThread ();
    }

    ~Impl ()
    {
        stopThread ();
    }

    Journal const& journal() const
    {
        return m_journal;
    }

    Ports const& getPorts () const
    {
        SharedState::UnlockedAccess state (m_state);
        return state->ports;
    }

    void setPorts (Ports const& ports)
    {
        SharedState::Access state (m_state);
        state->ports = ports;
        update();
    }

    bool stopping () const
    {
        return ! m_work;
    }

    void stop (bool wait)
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

    Handler& handler()
    {
        return m_handler;
    }

    boost::asio::io_service& get_io_service()
    {
        return m_io_service;
    }

    // Inserts the peer into our list of peers. We only remove it
    // from the list inside the destructor of the Peer object. This
    // way, the Peer can never outlive the server.
    //
    void add (Peer& peer)
    {
        SharedState::Access state (m_state);
        state->peers.push_back (peer);
    }

    void add (Door& door)
    {
        SharedState::Access state (m_state);
        state->doors.push_back (door);
    }

    // Removes the peer from our list of peers. This is only called from
    // the destructor of Peer. Essentially, the item in the list functions
    // as a weak_ptr.
    //
    void remove (Peer& peer)
    {
        SharedState::Access state (m_state);
        state->peers.erase (state->peers.iterator_to (peer));
    }

    void remove (Door& door)
    {
        SharedState::Access state (m_state);
        state->doors.push_back (door);
    }

    //--------------------------------------------------------------------------
    //
    // Thread
    //

    // Updates our Door list based on settings.
    //
    void handle_update ()
    {
        if (! stopping())
        {
            // Make a local copy to shorten the lock
            //
            Ports ports;
            {
                SharedState::ConstAccess state (m_state);
                ports = state->ports;
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
                int comp;

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
                        doors.push_back (new Door (*this, *port));
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
                    doors.push_back (new Door (*this, *port));
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

    // Causes handle_update to run on the io_service
    //
    void update ()
    {
        m_io_service.post (m_strand.wrap (boost::bind (
            &Impl::handle_update, this)));
    }

    // The main i/o processing loop.
    //
    void run ()
    {
        m_io_service.run ();

        m_stopped.signal();
        m_handler.onStopped (m_server);
    }
};

//------------------------------------------------------------------------------

HTTPServer::HTTPServer (Handler& handler, Journal journal)
    : m_impl (new Impl (*this, handler, journal))
{
}

HTTPServer::~HTTPServer ()
{
    stop();
}

Journal const& HTTPServer::journal () const
{
    return m_impl->journal();
}

HTTPServer::Ports const& HTTPServer::getPorts () const
{
    return m_impl->getPorts();
}

void HTTPServer::setPorts (Ports const& ports)
{
    m_impl->setPorts (ports);
}

void HTTPServer::stopAsync ()
{
    m_impl->stop(false);
}

void HTTPServer::stop ()
{
    m_impl->stop(true);
}

//------------------------------------------------------------------------------

}
