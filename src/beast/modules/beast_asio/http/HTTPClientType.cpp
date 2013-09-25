//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

class HTTPClientType : public HTTPClientBase, public Uncopyable
{
private:
    using HTTPClientBase::Listener;

    typedef boost::system::error_code error_code;

    class ListenerHandler
    {
    public:
        ListenerHandler ()
            : m_owner (nullptr)
            , m_listener (nullptr)
        {
        }

        ListenerHandler (HTTPClientType* owner, Listener* listener = nullptr)
            : m_owner (owner)
            , m_listener (listener)
        {
        }

        ListenerHandler (ListenerHandler const& other)
            : m_owner (other.m_owner)
            , m_listener (other.m_listener)
        {
        }

        ListenerHandler& operator= (ListenerHandler const& other)
        {
            m_owner = other.m_owner;
            m_listener = other.m_listener;
            return *this;
        }

        void operator() (error_code)
        {
            if (m_listener != nullptr)
                m_listener->onHTTPRequestComplete (
                    *m_owner, m_owner->result ());
        }

    private:
        HTTPClientType* m_owner;
        Listener* m_listener;
    };

public:
    //--------------------------------------------------------------------------

    HTTPClientType (
        double timeoutSeconds,
        std::size_t messageLimitBytes,
        std::size_t bufferSize)
        : m_timeoutSeconds (timeoutSeconds)
        , m_messageLimitBytes (messageLimitBytes)
        , m_bufferSize (bufferSize)
    {
    }

    ~HTTPClientType ()
    {
        m_async_op = nullptr;
    }

    Result const& result () const
    {
        return m_result;
    }

    Result const& get (URL const& url)
    {
        boost::asio::io_service io_service;
        async_get (io_service, nullptr, url);
        io_service.run ();
        return result ();
    }

    //--------------------------------------------------------------------------

    void async_get (boost::asio::io_service& io_service, Listener* listener,
        URL const& url)
    {
        async_get (io_service, url, ListenerHandler (this, listener));
    }

    // Handler signature is void(error_code)
    //
    template <typename Handler>
    void async_get (boost::asio::io_service& io_service,
        URL const& url,
            BOOST_ASIO_MOVE_ARG(Handler) handler)
    {
        async_get (io_service, url, newErrorHandler (
            BOOST_ASIO_MOVE_CAST(Handler)(handler)));
    }

    void async_get (boost::asio::io_service& io_service,
        URL const& url, SharedHandlerPtr handler)
    {
        // This automatically dispatches
        m_async_op = new AsyncGetOp (
            *this, io_service, url, handler,
            m_timeoutSeconds, m_messageLimitBytes, m_bufferSize);
    }

    void cancel ()
    {
        if (m_async_op != nullptr)
        {
            m_async_op->cancel ();
            m_async_op = nullptr;
        }
    }

private:
    //--------------------------------------------------------------------------

    /** Helper function to get a const_buffer from a String. */
    static boost::asio::const_buffers_1 stringBuffer (String const& s)
    {
        return boost::asio::const_buffers_1 (s.getCharPointer (), s.length ());
    }

    /** Helper function to fill out a Query from a URL. */
    template <typename Query>
    static Query queryFromURL (URL const& url)
    {
        if (url.port () != 0)
        {
            return Query (
                url.host().toStdString(),
                url.port_string().toStdString(),
                Query::numeric_service);
        }

        return Query (
            url.host().toStdString(),
            url.scheme().toStdString());
    }

    //--------------------------------------------------------------------------

    class AsyncGetOp : public ComposedAsyncOperation
    {
    private:
        typedef boost::asio::ip::tcp Protocol;
        typedef boost::system::error_code error_code;

        typedef Protocol::resolver     resolver;
        typedef resolver::query        query;
        typedef resolver::iterator     iterator;
        typedef iterator::value_type   resolver_entry;
        typedef Protocol::socket       socket;

        //----------------------------------------------------------------------

        enum State
        {
            stateStart,
            stateResolveComplete,
            stateConnectComplete,
            stateHandshakeComplete,
            stateWriteComplete,
            stateShutdownComplete
        };

        //----------------------------------------------------------------------

        struct TimerHandler : SharedHandlerPtr
        {
            explicit TimerHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }
            void operator() (error_code const& ec)
            {
                m_owner->timerCompletion (ec);
            }

            AsyncGetOp* m_owner;
        };

        //----------------------------------------------------------------------

    public:
        AsyncGetOp (HTTPClientType& owner,
                    boost::asio::io_service& io_service,
                    URL const& url,
                    SharedHandlerPtr const& handler,
                    double timeoutSeconds,
                    std::size_t messageLimitBytes,
                    std::size_t bufferSize)
            : ComposedAsyncOperation (sizeof (*this), handler)
            , m_owner (owner)
            , m_io_service (io_service)
            , m_strand (m_io_service)
            , m_url (url)
            , m_handler (handler)
            , m_timer (io_service)
            , m_resolver (io_service)
            , m_socket (io_service)
            , m_context (boost::asio::ssl::context::sslv23)
            , m_buffer (bufferSize)
            , m_parser (HTTPParser::typeResponse)
            , m_timer_set (false)
            , m_timer_canceled (false)
            , m_timer_expired (false)
            , m_messageLimitBytes (messageLimitBytes)
            , m_bytesReceived (0)
        {
            m_context.set_default_verify_paths ();
            m_context.set_options (
                boost::asio::ssl::context::no_sslv2 |
                boost::asio::ssl::context::single_dh_use |
                boost::asio::ssl::context::default_workarounds);
            //m_context.set_verify_mode (boost::asio::ssl::verify_peer);

            if (timeoutSeconds > 0)
            {
                m_timer.expires_from_now (
                    boost::posix_time::milliseconds (
                        long (timeoutSeconds * 1000)));
                m_timer_set = true;

                ++m_io_pending;
                m_timer.async_wait (TimerHandler (this));
            }

            // Count as pending i/o
            ++m_io_pending;
            m_io_service.dispatch (
                m_strand.wrap (StartHandler (this)));
        }

        ~AsyncGetOp ()
        {
        }

        // Cancel all pending I/O, if any, and block until
        // there are no more completion handler calls pending.
        //
        void cancel ()
        {
            cancel_timer ();
            m_resolver.cancel ();
            error_code ec;
            m_socket.close (ec);

            m_done.wait ();
        }

    private:
        //----------------------------------------------------------------------

        // Counts a pending i/o as canceled
        //
        void io_canceled ()
        {
            bassert (m_io_pending.get () > 0);
            if (--m_io_pending == 0)
                m_done.signal ();
        }

        // Cancels the deadline timer.
        //
        void cancel_timer ()
        {
            // Make sure the timer was set (versus infinite timeout)
            if (m_timer_set)
            {
                // See if it was already canceled.
                if (! m_timer_canceled)
                {
                    m_timer_canceled = true;
                    error_code ec;
                    m_timer.cancel (ec);

                    // At this point, there will either be a pending completion
                    // or a pending abort for the handler. If its a completion,
                    // they will see that the timer was canceled (since we're on
                    // a strand, everything is serialized). If its an abort it
                    // counts as a cancellation anyway. Either way, we will deduct
                    // one i/o from the pending i/o count.
                }
            }
        }

        // Called to notify the original handler the operation is complete.
        //
        void complete (error_code const& ec)
        {
            // Set the error code in the result.
            m_owner.m_result.error = ec;

            // Cancel the deadline timer. This ensures that
            // we will not return 'timeout' to the caller later.
            //
            cancel_timer ();

            bassert (m_io_pending.get () > 0);

            io_canceled ();

            // We call the handler directly since we know
            // we are already in the right context, and
            // because we need to do some things afterwards.
            //
            m_handler->operator() (ec);
        }

        // Called every time an async operation completes.
        // The return value indicates if the handler should
        // stop additional activity and return immediately.
        //
        bool io_complete (error_code const& ec)
        {
            if (m_timer_expired ||
                ec == boost::asio::error::operation_aborted)
            {
                // Timer expired, or the operation was aborted due to
                // cancel, so we deduct one i/o and return immediately.
                //
                io_canceled ();
                return true;
            }

            if (ec != 0 && ec != boost::asio::error::eof)
            {
                // A real error happened, and the timer didn't expire, so
                // notify the original handler that the operation is complete.
                //
                complete (ec);
                return true;
            }

            // Process the completion as usual. If the caller does not
            // call another initiating function, it is their responsibility
            // to call io_canceled() to deduce one pending i/o.
            //
            return false;
        }

        // Called when the deadline timer expires or is canceled.
        //
        void timerCompletion (error_code ec)
        {
            bassert (m_timer_set);

            if (m_timer_canceled || ec == boost::asio::error::operation_aborted)
            {
                // If the cancel flag is set or the operation was aborted it
                // means we canceled the timer so deduct one i/o and return.
                //
                io_canceled ();
                return;
            }

            bassert (ec == 0);

            // The timer expired, so this is a real timeout scenario.
            // We want to set the error code, notify the handler, and cancel
            // all other pending i/o.
            //
            m_timer_expired = true;

            ec = error_code (boost::asio::error::timed_out,
                boost::asio::error::get_system_category ());

            // Cancel pending name resolution
            m_resolver.cancel ();

            // Close the socket. This will cancel up to 2 pending i/o
            m_socket.close (ec);

            // Notify the original handler of a timeout error.
            // The call to complete() consumes one pending i/o, which
            // we need since this function counts as one completion.
            //
            complete (ec);
        }

        //----------------------------------------------------------------------

        struct StartHandler : SharedHandlerPtr
        {
            explicit StartHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() ()
            {
                m_owner->start_complete ();
            }

            AsyncGetOp* m_owner;
        };

        struct ResolveHandler : SharedHandlerPtr
        {
            explicit ResolveHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() (error_code const& ec, iterator iter)
            {
                m_owner->resolve_complete (ec, iter);
            }

            AsyncGetOp* m_owner;
        };

        struct ConnectHandler : SharedHandlerPtr
        {
            explicit ConnectHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() (error_code const& ec)
            {
                m_owner->connect_complete (ec);
            }

            AsyncGetOp* m_owner;
        };

        struct HandshakeHandler : SharedHandlerPtr
        {
            explicit HandshakeHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() (error_code const& ec)
            {
                m_owner->handshake_complete (ec);
            }

            AsyncGetOp* m_owner;
        };

        struct WriteHandler : SharedHandlerPtr
        {
            explicit WriteHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() (error_code const& ec, std::size_t bytes_transferred)
            {
                m_owner->write_complete (ec, bytes_transferred);
            }

            AsyncGetOp* m_owner;
        };

        struct ReadHandler : SharedHandlerPtr
        {
            explicit ReadHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() (error_code const& ec, std::size_t bytes_transferred)
            {
                m_owner->read_complete (ec, bytes_transferred);
            }

            AsyncGetOp* m_owner;
        };

        struct ShutdownHandler : SharedHandlerPtr
        {
            explicit ShutdownHandler (AsyncGetOp* owner)
                : SharedHandlerPtr (owner)
                , m_owner (owner)
            {
            }

            void operator() (error_code const& ec)
            {
                m_owner->shutdown_complete (ec);
            }

            AsyncGetOp* m_owner;
        };

        //----------------------------------------------------------------------

        void async_read_some ()
        {
            boost::asio::mutable_buffers_1 buf (
                m_buffer.getData (), m_buffer.getSize ());

            m_stream->async_read_some (buf,
                m_strand.wrap (ReadHandler (this)));
        }

        // Called when the HTTP parser returns an error
        void parse_error ()
        {
            //unsigned char const http_errno (m_parser.error ());
            String const http_errmsg (m_parser.message ());

            // VFALCO TODO put the parser error in ec
            error_code ec (
                boost::system::errc::invalid_argument,
                    boost::system::system_category ());

            complete (ec);
        }

        // Called to create an error when the message is over the limit
        error_code message_limit_error ()
        {
            // VFALCO TODO Make a suitable error code
            return error_code (
                boost::system::errc::invalid_argument,
                    boost::system::system_category ());
        }

        //----------------------------------------------------------------------

        void start_complete ()
        {
            query q (queryFromURL <query> (m_url));
            m_resolver.async_resolve (q,
                m_strand.wrap (ResolveHandler (this)));
        }

        void resolve_complete (error_code ec, iterator iter)
        {
            if (io_complete (ec))
                return;

            resolver_entry const entry (*iter);
            m_socket.async_connect (entry.endpoint (),
                m_strand.wrap (ConnectHandler (this)));
        }

        void connect_complete (error_code ec)
        {
            if (io_complete (ec))
                return;

            if (m_url.scheme () == "https")
            {
                typedef boost::asio::ssl::stream <socket&> ssl_stream;
                m_stream = new SocketWrapper <ssl_stream> (m_socket, m_context);
                /*
                m_stream->set_verify_mode (
                    boost::asio::ssl::verify_peer |
                    boost::asio::ssl::verify_fail_if_no_peer_cert);
                */
                m_stream->async_handshake (
                    Socket::client, HandshakeHandler (this));
                return;
            }

            m_stream = new SocketWrapper <socket&> (m_socket);
            handshake_complete (ec);
        }

        void handshake_complete (error_code ec)
        {
            if (io_complete (ec))
                return;

            m_get_string =
                "GET " + m_url.path() + " HTTP/1.1\r\n" +
                "Host: " + m_url.host() + "\r\n" +
                "Accept: */*\r\n" +
                "Connection: close\r\n\r\n";

            boost::asio::async_write (
                *m_stream, stringBuffer (m_get_string),
                    m_strand.wrap (WriteHandler (this)));
            ++m_io_pending;

            async_read_some ();
        }

        void write_complete (error_code ec, std::size_t)
        {
            if (io_complete (ec))
                return;

            if (! m_stream->needs_handshake ())
            {
                m_socket.shutdown (socket::shutdown_send, ec);
                if (ec != 0)
                    return complete (ec);
            }

            // deduct one i/o since we aren't issuing any new one
            io_canceled ();
        }

        void read_complete (error_code ec, std::size_t bytes_transferred)
        {
            m_bytesReceived += bytes_transferred;
            if (m_bytesReceived > m_messageLimitBytes)
                ec = message_limit_error ();

            if (io_complete (ec))
                return;

            std::size_t const bytes_parsed (m_parser.process (
                m_buffer.getData (), bytes_transferred));

            if (m_parser.error ())
            {
                parse_error ();
                return;
            }

            if (bytes_parsed != bytes_transferred)
            {
                // VFALCO TODO put an appropriate error in ec
                ec = error_code (
                    boost::system::errc::invalid_argument,
                        boost::system::system_category ());
                return complete (ec);
            }

            if (ec == boost::asio::error::eof)
            {
                m_parser.process_eof ();
            }

            if (m_parser.finished ())
            {
                m_state = stateShutdownComplete;
                if (m_stream->needs_handshake ())
                    m_stream->async_shutdown (ShutdownHandler (this));
                else
                    shutdown_complete (error_code ());
                return;
            }

            async_read_some ();
        }

        void shutdown_complete (error_code ec)
        {
            if (io_complete (ec))
                return;

            m_owner.m_result.response = m_parser.response ();
            if (ec == boost::asio::error::eof)
                ec = error_code ();

            return complete (ec);
        }

    private:
        WaitableEvent m_done;
        Atomic <int> m_io_pending;
        HTTPClientType& m_owner;
        boost::asio::io_service& m_io_service;
        boost::asio::io_service& m_strand;
        URL m_url;
        SharedHandlerPtr m_handler;
        boost::asio::deadline_timer m_timer;
        resolver m_resolver;
        socket m_socket;
        ScopedPointer <Socket> m_stream;
        boost::asio::ssl::context m_context;
        MemoryBlock m_buffer;
        State m_state;
        HTTPParser m_parser;
        String m_get_string;
        bool m_timer_set;
        bool m_timer_canceled;
        bool m_timer_expired;
        std::size_t m_messageLimitBytes;
        std::size_t m_bytesReceived;
    };

    double m_timeoutSeconds;
    std::size_t m_messageLimitBytes;
    std::size_t m_bufferSize;
    boost::asio::io_service  m_io_service;
    SharedPtr <AsyncGetOp> m_async_op;
    Result m_result;
};

//------------------------------------------------------------------------------

HTTPClientBase* HTTPClientBase::New (
    double timeoutSeconds, std::size_t messageLimitBytes, std::size_t bufferSize)
{
    ScopedPointer <HTTPClientBase> object (new HTTPClientType
        (timeoutSeconds, messageLimitBytes, bufferSize));
    return object.release ();
}

//------------------------------------------------------------------------------

class HTTPClientTests
    : public UnitTest
    , public HTTPClientBase::Listener
{
public:
    typedef boost::system::error_code error_code;

    //--------------------------------------------------------------------------

    class IoServiceThread : protected Thread
    {
    public:
        explicit IoServiceThread (String name = "io_service")
            : Thread (name)
        {
        }

        ~IoServiceThread ()
        {
            join ();
        }

        boost::asio::io_service& get_io_service ()
        {
            return m_service;
        }

        void start ()
        {
            startThread ();
        }

        void join ()
        {
            this->waitForThreadToExit ();
        }

    private:
        void run ()
        {
            m_service.run ();
        }

    private:
        boost::asio::io_service m_service;
    };

    //--------------------------------------------------------------------------

    void log (HTTPMessage const& m)
    {
        for (std::size_t i = 0; i < m.headers().size(); ++i)
        {
            HTTPField const f (m.headers()[i]);
            String s;
            s = "[ '" + f.name() +
                "' , '" + f.value() + "' ]";
            logMessage (s);
        }
    }

    void log (HTTPClientBase::Result const& result)
    {
        if (result.error != 0)
        {
            logMessage (String (
                "HTTPClient error: '" + result.error.message() + "'"));
        }
        else if (! result.response.empty ())
        {
            logMessage (String ("Status: ") +
                String::fromNumber (result.response->status()));
            
            log (*result.response);
        }
        else
        {
            logMessage ("HTTPClient: no response");
        }
    }

    //--------------------------------------------------------------------------

    void onHTTPRequestComplete (
        HTTPClientBase const&, HTTPClientBase::Result const& result)
    {
        log (result);
    }

    void testSync (String const& s, double timeoutSeconds)
    {
        ScopedPointer <HTTPClientBase> client (
            HTTPClientBase::New (timeoutSeconds));

        log (client->get (ParsedURL (s).url ()));
    }

    void testAsync (String const& s, double timeoutSeconds)
    {
        IoServiceThread t;
        ScopedPointer <HTTPClientBase> client (
            HTTPClientBase::New (timeoutSeconds));

        client->async_get (t.get_io_service (), this,
            ParsedURL (s).url ());

        t.start ();
        t.join ();
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        beginTestCase ("HTTPClient::get");

        testSync (
            "http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            5);

        testAsync (
            "http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            5);

        testAsync (
            "https://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            5);

        pass ();
    }

    HTTPClientTests () : UnitTest ("HttpClient", "beast", runManual)
    {
    }
};

static HTTPClientTests httpClientTests;


