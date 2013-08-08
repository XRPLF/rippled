//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef BOOST_ASIO_INITFN_RESULT_TYPE
#define BOOST_ASIO_INITFN_RESULT_TYPE(expr,val) void
#endif

namespace AsioUnitTestsNamespace
{

using namespace boost;

//------------------------------------------------------------------------------

// A handshaking stream that can distinguish multiple protocols
//
// SSL note:
//
// http://stackoverflow.com/questions/8467277/boostasio-and-async-ssl-stream-how-to-detect-end-of-data-connection-close/18024071#18024071
//
class RippleHandshakeStream
    : public asio::socket_base
    , public asio::ssl::stream_base
{
public:
    struct SocketInterfaces
        : SocketInterface::Socket
        , SocketInterface::Stream
        , SocketInterface::Handshake
    {};

    enum Status
    {
        needMore,
        proxy,
        plain,
        ssl
    };

    struct Options
    {
        Options ()
            : useClientSsl (false)
            , enableServerSsl (false)
            , requireServerSsl (false)
            , requireServerProxy (false)
        {
        }

        // Always perform SSL handshake as client role
        bool useClientSsl;

        // Enable optional SSL capability as server role
        bool enableServerSsl;

        // Require SSL as server role.
        // Does not require that enableServerSsl is set
        bool requireServerSsl;

        // Require PROXY protocol handshake as server role
        bool requireServerProxy;
    };
};

template <class Stream>
class RippleHandshakeStreamType : public RippleHandshakeStream
{
public:
    typedef typename remove_reference <Stream>::type next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

    typedef typename add_reference <next_layer_type>::type next_layer_type_ref;
    typedef typename asio::ssl::stream <next_layer_type_ref> SslStreamType;

    typedef RippleHandshakeStreamType <Stream> ThisType;

    template <class Arg>
    explicit RippleHandshakeStreamType (Arg& arg, Options options = Options ())
        : m_options (options)
        , m_context (RippleTlsContext::New ())
        , m_next_layer (arg)
        , m_io_service (m_next_layer.get_io_service ())
        , m_strand (m_io_service)
        , m_status (needMore)
        , m_role (Socket::client)

    {
    }

    //--------------------------------------------------------------------------

    asio::io_service& get_io_service () noexcept
    {
        return m_io_service;
    }

    next_layer_type& next_layer () noexcept
    {
        return m_next_layer;
    }

    next_layer_type const& next_layer () const noexcept
    {
        return m_next_layer;
    }

    lowest_layer_type& lowest_layer () noexcept
    {
        return m_next_layer.lowest_layer ();
    }

    lowest_layer_type const& lowest_layer () const noexcept
    {
        return m_next_layer.lowest_layer ();
    }

    //--------------------------------------------------------------------------

    Socket& stream () const noexcept
    {
        fatal_assert (m_stream != nullptr);
        return *m_stream;
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface
    //
    //--------------------------------------------------------------------------

    system::error_code cancel (boost::system::error_code& ec)
    {
        return lowest_layer ().cancel (ec);
    }

    system::error_code close (boost::system::error_code& ec)
    {
        return lowest_layer ().close (ec);
    }

    system::error_code shutdown (Socket::shutdown_type what, boost::system::error_code& ec)
    {
        return lowest_layer ().shutdown (what, ec);
    }

    //--------------------------------------------------------------------------
    //
    // StreamInterface
    //
    //--------------------------------------------------------------------------

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, system::error_code& ec)
    {
        if (m_buffer.size () > 0)
        {
            ec = system::error_code ();
            std::size_t const amount = asio::buffer_copy (buffers, m_buffer.data ());
            m_buffer.consume (amount);
            return amount;
        }
        return stream ().read_some (buffers, ec);
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, system::error_code& ec)
    {
        return stream ().write_some (buffers, ec);
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler)
    {
        if (m_buffer.size () > 0)
        {
            // Return the leftover bytes from the handshake
            std::size_t const amount = asio::buffer_copy (buffers, m_buffer.data ());
            m_buffer.consume (amount);
            return m_io_service.post (m_strand.wrap (boost::bind (
                BOOST_ASIO_MOVE_CAST(ReadHandler)(handler), system::error_code (), amount)));
        }
        return stream ().async_read_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(ReadHandler)(handler), boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred)));
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        return stream ().async_write_some (buffers,
            BOOST_ASIO_MOVE_CAST(WriteHandler)(handler));
    }

    //--------------------------------------------------------------------------

    system::error_code handshake (Socket::handshake_type role, system::error_code& ec)
    {
        Action action = calcAction (role);

        switch (action)
        {
        default:
        case actionPlain:
            handshakePlain (ec);
            break;

        case actionSsl:
            handshakeSsl (ec);
            break;

        case actionDetect:
            detectHandshake (ec);
            if (! ec)
            {
                action = calcDetectAction (ec);
                switch (action)
                {
                default:
                case actionPlain:
                    handshakePlain (ec);
                    break;
                case actionSsl:
                    handshakeSsl (ec);
                    break;
                };
            }
            break;
        }

        return ec;
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type type,
        ConstBufferSequence const& buffers, boost::system::error_code& ec)
    {
        Action action = calcAction (type);
        ec = system::error_code ();
        switch (action)
        {
        default:
        case actionPlain:
            handshakePlain (buffers, ec);
            break;
        case actionSsl:
            handshakeSsl (buffers, ec);
            break;
        case actionDetect:
            detectHandshake (buffers, ec);
            if (! ec)
            {
                action = calcDetectAction (ec);
                switch (action)
                {
                default:
                case actionPlain:
                    handshakePlain (buffers, ec);
                    break;
                case actionSsl:
                    handshakeSsl (buffers, ec);
                    break;
                };
            }
            break;
        }
        return ec;
    }
#endif

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        Action const action = calcAction (type);
        switch (action)
        {
        default:
        case actionPlain:
            return handshakePlainAsync (BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
            break;

        case actionSsl:
            return handshakeSslAsync (BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
            break;

        case actionDetect:
            return detectHandshakeAsync (BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
            break;
        }
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, const ConstBufferSequence& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        Action const action = calcAction (type);
        switch (action)
        {
        default:
        case actionPlain:
            return handshakePlainAsync (buffers,
                BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
            break;

        case actionSsl:
            return handshakeSslAsync (buffers,
                BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
            break;

        case actionDetect:
            return detectHandshakeAsync (buffers,
                BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
            break;
        }
    }
#endif

    system::error_code shutdown (system::error_code& ec)
    {
        if (m_status == ssl)
        {
            return m_ssl_stream->shutdown (ec);
        }
        else
        {
            // we need to close the lwest layer
            return m_next_layer.shutdown (next_layer_type::shutdown_both, ec);
        }
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler)
    {
        if (m_status == ssl)
        {
            m_ssl_stream->async_shutdown (m_strand.wrap (boost::bind (
                BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler),
                boost::asio::placeholders::error)));
        }
        else
        {
            system::error_code ec;
            m_next_layer.shutdown (next_layer_type::shutdown_both, ec);
            m_io_service.post (m_strand.wrap (boost::bind (
                BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler), ec)));
        }
    }

    //--------------------------------------------------------------------------

    enum Action
    {
        actionDetect,
        actionPlain,
        actionSsl,
        actionFail
    };

    // Determines what action to take based on
    // the stream options and the desired role.
    //
    Action calcAction (Socket::handshake_type role)
    {
        m_role = role;

        if (role == Socket::server)
        {
            if (! m_options.enableServerSsl &&
                ! m_options.requireServerSsl &&
                ! m_options.requireServerProxy)
            {
                return actionPlain;
            }
            else if (m_options.requireServerSsl && ! m_options.requireServerProxy)
            {
                return actionSsl;
            }
            else
            {
                return actionDetect;
            }
        }
        else if (m_role == Socket::client)
        {
            if (m_options.useClientSsl)
            {
                return actionSsl;
            }
            else
            {
                return actionPlain;
            }
        }

        return actionPlain;
    }

    // Determines what action to take based on the auto-detected
    // handshake, the stream options, and desired role.
    //
    Action calcDetectAction (system::error_code& ec)
    {
        ec = system::error_code ();

        if (m_status == plain)
        {
            if (! m_options.requireServerProxy && ! m_options.requireServerSsl)
            {
                return actionPlain;
            }
            else
            {
                failedHandshake (ec);
                return actionFail;
            }
        }
        else if (m_status == ssl)
        {
            if (! m_options.requireServerProxy)
            {
                if (m_options.enableServerSsl || m_options.requireServerSsl)
                {
                    return actionSsl;
                }
                else
                {
                    failedHandshake (ec);
                    return actionFail;
                }
            }
            else
            {
                failedHandshake (ec);
                return actionFail;
            }
        }
        else if (m_status == proxy)
        {
            if (m_options.requireServerProxy)
            {
                // read the rest of the proxy string
                // then transition to SSL handshake mode
                failedHandshake (ec);
                return actionFail;
            }
            else
            {
                // Can we make PROXY optional?
                failedHandshake (ec);
                return actionFail;
            }
        }

        failedHandshake (ec);
        return actionFail;
    }

    //--------------------------------------------------------------------------

    // called when options disallow handshake
    void failedHandshake (system::error_code& ec)
    {
        // VFALCO TODO maybe use a ripple error category?
        // set this to something custom that we can recognize later?
        ec = asio::error::invalid_argument;
    }

    void createPlainStream ()
    {
        m_status = plain;
        m_stream = new SocketWrapper <next_layer_type> (m_next_layer);
    }

    void handshakePlain (system::error_code& ec)
    {
        ec = system::error_code ();
        createPlainStream ();
    }

    template <typename ConstBufferSequence>
    void handshakePlain (ConstBufferSequence const& buffers, system::error_code& ec)
    {
        fatal_assert (asio::buffer_size (buffers) == 0 );
        ec = system::error_code ();
        createPlainStream ();
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    handshakePlainAsync (BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        createPlainStream ();
        return m_io_service.post (m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler), system::error_code())));
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    handshakePlainAsync (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        fatal_assert (asio::buffer_size (buffers) == 0);
        createPlainStream ();
        return m_io_service.post (m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler),
            system::error_code(), 0)));
    }
#endif

    void createSslStream ()
    {
        m_status = ssl;
        m_ssl_stream = new SslStreamType (m_next_layer, m_context->getBoostContext ());
        m_stream = new SocketWrapper <SslStreamType> (*m_ssl_stream);
    }

    void handshakeSsl (system::error_code& ec)
    {
        createSslStream ();
        m_ssl_stream->handshake (m_role, ec);
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence>
    void handshakeSsl (ConstBufferSequence const& buffers, system::error_code& ec)
    {
        createSslStream ();
        m_ssl_stream->handshake (m_role, buffers, ec);
    }
#endif

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    handshakeSslAsync (BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        createSslStream ();
        return m_ssl_stream->async_handshake (m_role,
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE (BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    handshakeSslAsync (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        createSslStream ();
        return m_ssl_stream->async_handshake (m_role, buffers,
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
    }
#endif

    //--------------------------------------------------------------------------

    enum
    {
        autoDetectBytes = 5
    };

    void detectHandshake (system::error_code& ec)
    {
        // Top up our buffer
        bassert (m_buffer.size () == 0);
        std::size_t const needed = autoDetectBytes;
        std::size_t const amount =  m_next_layer.receive (
            m_buffer.prepare (needed), asio::socket_base::message_peek, ec);
        m_buffer.commit (amount);
        if (! ec)
        {
            analyzeHandshake (m_buffer.data ());
            m_buffer.consume (amount);
            if (m_status == needMore)
                ec = asio::error::invalid_argument; // should never happen
        }
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <class ConstBufferSequence>
    void detectHandshake (ConstBufferSequence const& buffers, system::error_code& ec)
    {
        m_buffer.commit (asio::buffer_copy (
            m_buffer.prepare (asio::buffer_size (buffers)), buffers));
        detectHandshake (ec);
    }
#endif

    //--------------------------------------------------------------------------

    template <typename HandshakeHandler>
    void onDetectRead (BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler,
        system::error_code const& ec, std::size_t bytes_transferred)
    {
        m_buffer.commit (bytes_transferred);

        if (! ec)
        {
            analyzeHandshake (m_buffer.data ());

            system::error_code ec;

            if (m_status != needMore)
            {
                m_buffer.consume (bytes_transferred);

                Action action = calcDetectAction (ec);
                if (! ec)
                {
                    switch (action)
                    {
                    default:
                    case actionPlain:
                        handshakePlainAsync (
                            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
                        break;
                    case actionSsl:
                        handshakeSslAsync (
                            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
                        break;
                    };
                }
            }
            else
            {
                ec = asio::error::invalid_argument;
            }

            if (ec)
            {
                m_io_service.post (m_strand.wrap (boost::bind (
                    BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler), ec)));
            }
        }
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    detectHandshakeAsync (BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        bassert (m_buffer.size () == 0);
        return m_next_layer.async_receive (
            m_buffer.prepare (autoDetectBytes), asio::socket_base::message_peek,
            m_strand.wrap (boost::bind (&ThisType::onDetectRead <HandshakeHandler>, this,
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler),
            asio::placeholders::error, asio::placeholders::bytes_transferred)));
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    detectHandshakeAsync (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        fatal_error ("unimplemented");
    }
#endif

    //--------------------------------------------------------------------------

    static inline bool isPrintable (unsigned char c)
    {
        return (c < 127) && (c > 31);
    }

    template <class ConstBufferSequence>
    void analyzeHandshake (ConstBufferSequence const& buffers)
    {
        m_status = needMore;

        unsigned char data [5];

        std::size_t const bytes = asio::buffer_copy (asio::buffer (data), buffers);

        if (bytes > 0)
        {
            if (                 isPrintable (data [0]) &&
                 ((bytes < 2) || isPrintable (data [1])) &&
                 ((bytes < 3) || isPrintable (data [2])) &&
                 ((bytes < 4) || isPrintable (data [3])) &&
                 ((bytes < 5) || isPrintable (data [4])))
            {
                if (bytes < 5 || memcmp (data, "PROXY", 5) != 0)
                {
                    m_status = plain;
                }
                else
                {
                    m_status = proxy;
                }
            }
            else
            {
                m_status = ssl;
            }
        }
    }

private:
    Options m_options;
    //RippleSslContext m_context;
    ScopedPointer <RippleTlsContext> m_context;
    Stream m_next_layer;
    asio::io_service& m_io_service;
    asio::io_service::strand m_strand;
    Status m_status;
    Socket::handshake_type m_role;
    ScopedPointer <Socket> m_stream;
    ScopedPointer <SslStreamType> m_ssl_stream;
    asio::streambuf m_buffer;
};

//------------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------------

class AsioUnitTests : public UnitTest
{
public:
    AsioUnitTests () : UnitTest ("Asio", "ripple", runManual)
    {
    }

    //--------------------------------------------------------------------------

    // These are passed as template arguments and package up
    // the parameters needed to establish the connection.

    // ip::tcp with v4 addresses
    //
    struct TcpV6
    {
        typedef asio::ip::tcp Protocol;

        static asio::ip::tcp::endpoint server_endpoint ()
        {
            return asio::ip::tcp::endpoint (asio::ip::tcp::v6 (), 1052);
        }

        static asio::ip::tcp::endpoint client_endpoint ()
        {
            return asio::ip::tcp::endpoint (asio::ip::address_v6 ().from_string ("::1"), 1052);
        }
    };

    // ip::tcp with v6 addresses
    //
    struct TcpV4
    {
        typedef asio::ip::tcp Protocol;

        static asio::ip::tcp::endpoint server_endpoint ()
        {
            return asio::ip::tcp::endpoint (asio::ip::address_v4::any (), 1053);
        }

        static asio::ip::tcp::endpoint client_endpoint ()
        {
            return asio::ip::tcp::endpoint (asio::ip::address_v4::loopback (), 1053);
        }
    };

    //--------------------------------------------------------------------------

    // We create our own error category to distinguish unexpected
    // errors like connection failures, versus intended errors like
    // a planned mismatch in handshakes.

    enum
    {
        timeout = 1,
        unexpected // a unexpected test result was encountered
    };

    struct unit_test_category_t : system::error_category
    {
        char const* name () const noexcept
        {
            return "unit_test";
        }

        std::string message (int ev) const
        {
            switch (ev)
            {
            case timeout: return "The timeout expired before the test could complete";
            case unexpected: return "An unexpected test result was encountered";
            default:
                break;
            };

            return "unknown";
        }

        system::error_condition default_error_condition (int ev) const noexcept
        {
            return system::error_condition (ev, *this);
        }

        bool equivalent (int code, system::error_condition const& condition) const noexcept
        {
            return default_error_condition (code) == condition;
        }

        bool equivalent (system::error_code const& code, int condition) const noexcept
        {
            return *this == code.category() && code.value() == condition;
        }
    };

    static unit_test_category_t& unit_test_category ()
    {
        static unit_test_category_t category;
        return category;
    }

    //--------------------------------------------------------------------------

    // These flags get combined to determine the handshaking attributes
    //
    enum
    {
        none = 0,
        client_ssl = 1,
        server_ssl = 2,
        server_ssl_required = 4,
        server_proxy = 8
    };

    // The scenario object provides inputs to construct children with
    // the test information. It also holds the outputs of the client and
    // server threads.
    //
    class Scenario
    {
    public:
        // Implicit construction from flags
        Scenario (int options = 0)
        {
            handshakeOptions.useClientSsl = (options & client_ssl) != 0;
            handshakeOptions.enableServerSsl = (options & (server_ssl | server_ssl_required)) != 0;
            handshakeOptions.requireServerSsl = (options & server_ssl_required) != 0;
            handshakeOptions.requireServerProxy = (options & server_proxy) !=0;
        }

        // inputs
        RippleHandshakeStream::Options handshakeOptions;

        // outputs
        system::error_code client_error;
        system::error_code server_error;
    };

    //--------------------------------------------------------------------------

    // Common code for client and server tests
    //
    class BasicTest : public Thread
    {
    public:
        enum
        {
            // how long to wait until we give up
            //milliSecondsToWait = 1000
            //milliSecondsToWait = 20000
            milliSecondsToWait = -1
        };

        BasicTest (UnitTest& test,
                   Scenario& scenario,
                   Socket::handshake_type role)
            : Thread ((role == Socket::client) ? "client" : "server")
            , m_test (test)
            , m_scenario (scenario)
            , m_role (role)
        {
        }

        ~BasicTest ()
        {
        }

        // Called from the unit test thread, reports the
        // error to the unit test if it indicates a failure.
        //
        bool check_success (system::error_code const& ec, bool eofIsOkay = false)
        {
            if (eofIsOkay && ec == asio::error::eof)
                return true;
            return m_test.expect (
                (! ec) || (eofIsOkay && ec == asio::error::eof), ec.message ());
        }

        // Called from the thread to check the error code.
        // This sets the error code in the scenario appropriately.
        //
        bool thread_success (system::error_code const& ec, bool eofIsOkay = false)
        {
            if (! check_success (ec, eofIsOkay))
            {
                if (m_role == Socket::server)
                    m_scenario.server_error = ec;
                else
                    m_scenario.client_error = ec;
                return false;
            }
            return true;
        }

        // Called from the thread to check a condition
        // This just calls thread_success with a special code if the condition is false
        //
        bool thread_expect (bool condition)
        {
            if (! condition)
                return thread_success (system::error_code (unexpected, unit_test_category ()));
            return true;
        }

        virtual system::error_code start (system::error_code& ec) = 0;

        virtual void finish () = 0;

    protected:
        UnitTest& m_test;
        Scenario& m_scenario;
        Socket::handshake_type const m_role;
    };

    //--------------------------------------------------------------------------

    // Common code for synchronous operations
    //
    class BasicSync : public BasicTest
    {
    public:
        BasicSync (UnitTest& test,
                   Scenario& scenario,
                   Socket::handshake_type role)
            : BasicTest (test, scenario, role)
        {
        }

        void finish ()
        {
            // This is dangerous
            if (! stopThread (milliSecondsToWait))
            {
                check_success (system::error_code (timeout, unit_test_category ()));
            }
        }
    };

    //--------------------------------------------------------------------------

    // Common code for synchronous servers
    //
    class BasicSyncServer : public BasicSync
    {
    public:
        BasicSyncServer (UnitTest& test, Scenario& scenario)
            : BasicSync (test, scenario, Socket::server)
        {
        }

        void process (Socket& socket, system::error_code& ec)
        {
            {
                asio::streambuf buf (5);
                std::size_t const amount = asio::read_until (socket, buf, "hello", ec);

                if (! thread_success (ec))
                    return;

                if (! thread_expect (amount == 5))
                    return;

                if (! thread_expect (buf.size () == 5))
                    return;
            }

            {
                std::size_t const amount = asio::write (socket, asio::buffer ("goodbye", 7), ec);

                if (! thread_success (ec))
                    return;

                if (! thread_expect (amount == 7))
                    return;
            }
        }
    };

    //--------------------------------------------------------------------------

    // Common code for synchronous clients
    //
    class BasicSyncClient : public BasicSync
    {
    public:
        BasicSyncClient (UnitTest& test, Scenario& scenario)
            : BasicSync (test, scenario, Socket::client)
        {
        }

        void process (Socket& socket, system::error_code& ec)
        {
            {
                std::size_t const amount = asio::write (socket, asio::buffer ("hello", 5), ec);

                if (! thread_success (ec))
                    return;

                if (! thread_expect (amount == 5))
                    return;
            }

            {
                char data [7];

                size_t const amount = asio::read (socket, asio::buffer (data, 7), ec);

                if (! thread_success (ec, true))
                    return;

                if (! thread_expect (amount == 7))
                    return;

                thread_expect (memcmp (&data, "goodbye", 7) == 0);
            }

            // Wait for 1 byte which should never come. Instead,
            // the server should close its end and we will get eof
            {
                char data [1];
                asio::read (socket, asio::buffer (data, 1), ec);
                if (ec == asio::error::eof)
                {
                    ec = system::error_code ();
                }
                else if (thread_success (ec))
                {
                    thread_expect (false);
                }
            }
        }
    };

    //--------------------------------------------------------------------------

    // A synchronous server
    //
    template <class Transport>
    class SyncServer : public BasicSyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        SyncServer (UnitTest& test, Scenario& scenario)
            : BasicSyncServer (test, scenario)
            , m_acceptor (m_io_service)
            , m_socket (m_io_service)
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return ec;

            if (! check_success (m_acceptor.set_option (typename SocketType::reuse_address (true), ec)))
                return ec;

            if (! check_success (m_acceptor.bind (Transport::server_endpoint (), ec)))
                return ec;

            if (! check_success (m_acceptor.listen (asio::socket_base::max_connections, ec)))
                return ec;

            startThread ();

            return ec;
        }

    private:
        void run ()
        {
            system::error_code ec;

            if (! thread_success (m_acceptor.accept (m_socket, ec)))
                return;

            if (! thread_success (m_acceptor.close (ec)))
                return;

            SocketWrapper <SocketType> socket (m_socket);

            process (socket, ec);

            if (! ec)
            {
                if (! thread_success (m_socket.shutdown (SocketType::shutdown_both, ec)))
                    return;

                if (! thread_success (m_socket.close (ec)))
                    return;
            }
        }

    protected:
        asio::io_service m_io_service;
        Acceptor m_acceptor;
        SocketType m_socket;
    };

    //--------------------------------------------------------------------------

    // A synchronous client
    //
    template <class Transport>
    class SyncClient : public BasicSyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        SyncClient (UnitTest& test, Scenario& scenario)
            : BasicSyncClient (test, scenario)
            , m_socket (m_io_service)
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            startThread ();

            return ec;
        }

    private:
        void run ()
        {
            system::error_code ec;

            if (! thread_success (m_socket.connect (Transport::client_endpoint (), ec)))
                return;

            SocketWrapper <SocketType> socket (m_socket);

            process (socket, ec);

            if (! ec)
            {
                if (! thread_success (m_socket.shutdown (SocketType::shutdown_both, ec)))
                    return;

                if (! thread_success (m_socket.close (ec)))
                    return;
            }
        }

    private:
        asio::io_service m_io_service;
        SocketType m_socket;
    };

    //--------------------------------------------------------------------------

    // A synchronous server that supports a handshake
    //
    template <class Transport, template <class Stream = typename Transport::Protocol::socket&> class HandshakeType>
    class HandshakeSyncServer : public BasicSyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        HandshakeSyncServer (UnitTest& test, Scenario& scenario)
            : BasicSyncServer (test, scenario)
            , m_socket (m_io_service)
            , m_acceptor (m_io_service)
            , m_handshake (m_socket, m_scenario.handshakeOptions)
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return ec;

            if (! check_success (m_acceptor.set_option (typename SocketType::reuse_address (true), ec)))
                return ec;

            if (! check_success (m_acceptor.bind (Transport::server_endpoint (), ec)))
                return ec;

            if (! check_success (m_acceptor.listen (asio::socket_base::max_connections, ec)))
                return ec;

            startThread ();

            return ec;
        }

    private:
        void run ()
        {
            system::error_code ec;

            if (! thread_success (m_acceptor.accept (m_socket, ec)))
                return;

            if (! thread_success (m_acceptor.close (ec)))
                return;

            SocketWrapper <HandshakeType <SocketType&> > socket (m_handshake);

            if (! thread_success (socket.handshake (m_role, ec)))
                return;

            process (socket, ec);

            if (! ec)
            {
                // closing the stream also shuts down the socket
                if (! thread_success (socket.shutdown (ec), true))
                   return;

                if (! thread_success (m_socket.close (ec)))
                   return;
            }
        }

    protected:
        asio::io_service m_io_service;
        SocketType m_socket;
        Acceptor m_acceptor;
        HandshakeType <SocketType&> m_handshake;
    };

    //--------------------------------------------------------------------------

    // A synchronous client that supports a handshake
    //
    template <class Transport, template <class Stream> class HandshakeType>
    class HandshakeSyncClient : public BasicSyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        HandshakeSyncClient (UnitTest& test, Scenario& scenario)
            : BasicSyncClient (test, scenario)
            , m_socket (m_io_service)
            , m_handshake (m_socket, m_scenario.handshakeOptions)
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            startThread ();

            return ec;
        }

    private:
        void run ()
        {
            system::error_code ec;

            if (! thread_success (m_socket.connect (Transport::client_endpoint (), ec)))
                return;

            SocketWrapper <HandshakeType <SocketType&> > socket (m_handshake);

            if (! thread_success (socket.handshake (m_role, ec)))
                return;

            process (socket, ec);

            if (! ec)
            {
                // Without doing a shutdown on the handshake stream in the
                // client, the call to close the socket will return "short read".
                if (! thread_success (socket.shutdown (ec), true))
                   return;

                if (! thread_success (m_socket.close (ec)))
                    return;
            }
        }

    private:
        asio::io_service m_io_service;
        SocketType m_socket;
        HandshakeType <SocketType&> m_handshake;
    };

    //--------------------------------------------------------------------------

    // Common code for asynchronous operations
    //
    class BasicAsync : public BasicTest
    {
    public:
        BasicAsync (UnitTest& test,
                    Scenario& scenario,
                    Socket::handshake_type role)
            : BasicTest (test, scenario, role)
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            // put the deadline timer here

            on_start (ec);

            if (! ec)
                startThread ();

            return ec;
        }

        void finish ()
        {
            // wait for io_service::run to return
            m_done.wait ();
        }

    protected:
        virtual asio::io_service& get_io_service () = 0;
        virtual Socket& socket () = 0;

        void run ()
        {
            get_io_service ().run ();
            m_done.signal ();
        }

        virtual void on_start (system::error_code& ec) = 0;

        virtual void on_shutdown (system::error_code const& ec) = 0;

        virtual void closed () = 0;

    protected:
        asio::streambuf m_buf;

    private:
        WaitableEvent m_done;
    };

    //--------------------------------------------------------------------------

    // Common code for asynchronous servers
    //
    class BasicAsyncServer : public BasicAsync
    {
    public:
        BasicAsyncServer (UnitTest& test, Scenario& scenario)
            : BasicAsync (test, scenario, Socket::server)
        {
        }

        void on_accept (system::error_code const& ec)
        {
            asio::async_read_until (socket (), m_buf, std::string ("hello"),
                boost::bind (&BasicAsyncServer::on_read, this,
                asio::placeholders::error, asio::placeholders::bytes_transferred));
        }

        void on_read (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 5))
                    return;

                asio::async_write (socket (), asio::buffer ("goodbye", 7),
                    boost::bind (&BasicAsyncServer::on_write, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_write (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 7))
                    return;

                {
                    system::error_code ec;
                    if (! thread_success (socket ().shutdown (Socket::shutdown_both, ec)))
                        return;
                }

                on_shutdown (ec);
            }
        }

        void on_shutdown (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                system::error_code ec;

                if (! thread_success (socket ().shutdown (Socket::shutdown_both, ec)))
                    return;

                if (! thread_success (socket ().close (ec)))
                    return;

                closed ();
            }
        }
    };

    //--------------------------------------------------------------------------

    // Common code for asynchronous clients
    //
    class BasicAsyncClient : public BasicAsync
    {
    public:
        BasicAsyncClient (UnitTest& test, Scenario& scenario)
            : BasicAsync (test, scenario, Socket::client)
        {
        }

        void on_connect (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                asio::async_write (socket (), asio::buffer ("hello", 5),
                    boost::bind (&BasicAsyncClient::on_write, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_write (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 5))
                    return;

                asio::async_read_until (socket (), m_buf, std::string ("goodbye"),
                    boost::bind (&BasicAsyncClient::on_read, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_read (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 7))
                    return;

                // should check the data here?
                m_buf.consume (bytes_transferred);

                asio::async_read (socket (), m_buf.prepare (1),
                    boost::bind (&BasicAsyncClient::on_read_final, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_read_final (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (ec == asio::error::eof)
            {
                system::error_code ec; // to hide the eof

                if (! thread_success (socket ().shutdown (Socket::shutdown_both, ec)))
                    return;

                on_shutdown (ec);
            }
            else if (thread_success (ec))
            {
                thread_expect (false);
            }
        }

        void on_shutdown (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                system::error_code ec;

                if (! thread_success (socket ().shutdown (Socket::shutdown_both, ec)))
                    return;

                if (! thread_success (socket ().close (ec)))
                    return;

                closed ();
            }
        }
    };

    //--------------------------------------------------------------------------

    template <class Transport>
    class AsyncServer : public BasicAsyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        AsyncServer (UnitTest& test, Scenario& scenario)
            : BasicAsyncServer (test, scenario)
            , m_acceptor (m_io_service)
            , m_socket (m_io_service)
            , m_socketWrapper (m_socket)
        {
        }

        asio::io_service& get_io_service ()
        {
            return m_io_service;
        }

        Socket& socket ()
        {
            return m_socketWrapper;
        }

        void on_start (system::error_code& ec)
        {
            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return;

            if (! check_success (m_acceptor.set_option (typename SocketType::reuse_address (true), ec)))
                return;

            if (! check_success (m_acceptor.bind (Transport::server_endpoint (), ec)))
                return;

            if (! check_success (m_acceptor.listen (asio::socket_base::max_connections, ec)))
                return;

            m_acceptor.async_accept (m_socket, boost::bind (
                &BasicAsyncServer::on_accept, this, asio::placeholders::error));
        }

        void closed ()
        {
            system::error_code ec;
            if (! thread_success (m_acceptor.close (ec)))
                return;
        }

    private:
        asio::io_service m_io_service;
        Acceptor m_acceptor;
        SocketType m_socket;
        asio::streambuf m_buf;
        SocketWrapper <SocketType> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // An asynchronous client
    //
    template <class Transport>
    class AsyncClient : public BasicAsyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        AsyncClient (UnitTest& test, Scenario& scenario)
            : BasicAsyncClient (test, scenario)
            , m_socket (m_io_service)
            , m_socketWrapper (m_socket)
        {
        }

        asio::io_service& get_io_service ()
        {
            return m_io_service;
        }

        Socket& socket ()
        {
            return m_socketWrapper;
        }

        void on_start (system::error_code& ec)
        {
            m_socket.async_connect (Transport::client_endpoint (),
                boost::bind (&BasicAsyncClient::on_connect, this, asio::placeholders::error));
        }

        void closed ()
        {
        }

    private:
        asio::io_service m_io_service;
        SocketType m_socket;
        asio::streambuf m_buf;
        SocketWrapper <SocketType> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // An asynchronous handshaking server
    //
    template <class Transport, template <class Stream> class HandshakeType>
    class HandshakeAsyncServer : public BasicAsyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;
        typedef HandshakeType <SocketType&> StreamType;
        typedef HandshakeAsyncServer <Transport, HandshakeType> ThisType;

        HandshakeAsyncServer (UnitTest& test, Scenario& scenario)
            : BasicAsyncServer (test, scenario)
            , m_acceptor (m_io_service)
            , m_socket (m_acceptor.get_io_service ())
            , m_stream (m_socket, m_scenario.handshakeOptions)
            , m_socketWrapper (m_stream)
        {
        }

        asio::io_service& get_io_service ()
        {
            return m_io_service;
        }

        Socket& socket ()
        {
            return m_socketWrapper;
        }

        void on_start (system::error_code& ec)
        {
            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return;

            if (! check_success (m_acceptor.set_option (typename SocketType::reuse_address (true), ec)))
                return;

            if (! check_success (m_acceptor.bind (Transport::server_endpoint (), ec)))
                return;

            if (! check_success (m_acceptor.listen (asio::socket_base::max_connections, ec)))
                return;

            m_acceptor.async_accept (m_socket, boost::bind (
                &ThisType::on_accept, this, asio::placeholders::error));
        }

        void on_accept (system::error_code const& ec)
        {
            {
                system::error_code ec;
                if (! thread_success (m_acceptor.close (ec)))
                    return;
            }

            if (thread_success (ec))
            {
                socket ().async_handshake (Socket::server,
                    boost::bind (&ThisType::on_handshake, this, asio::placeholders::error));
            }
        }

        void on_handshake (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                asio::async_read_until (socket (), m_buf, std::string ("hello"),
                    boost::bind (&ThisType::on_read, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_read (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 5))
                    return;

                asio::async_write (socket (), asio::buffer ("goodbye", 7),
                    boost::bind (&ThisType::on_write, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_write (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 7))
                    return;

                socket ().async_shutdown (boost::bind (&ThisType::on_shutdown, this,
                    asio::placeholders::error));
            }
        }

        void on_shutdown (system::error_code const& ec)
        {
            if (thread_success (ec, true))
            {
                system::error_code ec;

                if (! thread_success (socket ().close (ec)))
                    return;

                closed ();
            }
        }

        void closed ()
        {
        }

    private:
        asio::io_service m_io_service;
        Acceptor m_acceptor;
        SocketType m_socket;
        asio::streambuf m_buf;
        StreamType m_stream;
        SocketWrapper <StreamType> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // An asynchronous handshaking client
    //
    template <class Transport, template <class Stream> class HandshakeType>
    class HandshakeAsyncClient : public BasicAsyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       SocketType;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;
        typedef HandshakeType <SocketType&> StreamType;
        typedef HandshakeAsyncClient <Transport, HandshakeType> ThisType;

        HandshakeAsyncClient (UnitTest& test, Scenario& scenario)
            : BasicAsyncClient (test, scenario)
            , m_socket (m_io_service)
            , m_stream (m_socket, m_scenario.handshakeOptions)
            , m_socketWrapper (m_stream)
        {
        }

        asio::io_service& get_io_service ()
        {
            return m_io_service;
        }

        Socket& socket ()
        {
            return m_socketWrapper;
        }

        void on_start (system::error_code& ec)
        {
            m_socket.async_connect (Transport::client_endpoint (), boost::bind (
                &ThisType::on_connect, this, asio::placeholders::error));
        }

        void on_connect (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                socket ().async_handshake (Socket::client,
                    boost::bind (&ThisType::on_handshake, this, asio::placeholders::error));
            }
        }

        void on_handshake (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                asio::async_write (socket (), asio::buffer ("hello", 5),
                    boost::bind (&ThisType::on_write, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_write (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 5))
                    return;

                asio::async_read_until (socket (), m_buf, std::string ("goodbye"),
                    boost::bind (&ThisType::on_read, this,
                    asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_read (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (thread_success (ec))
            {
                if (! thread_expect (bytes_transferred == 7))
                    return;

                m_buf.consume (bytes_transferred);

                asio::async_read (socket (), m_buf.prepare (1), boost::bind (&ThisType::on_read_final,
                    this, asio::placeholders::error, asio::placeholders::bytes_transferred));
            }
        }

        void on_read_final (system::error_code const& ec, std::size_t bytes_transferred)
        {
            if (ec == asio::error::eof)
            {
                socket ().async_shutdown (boost::bind (&ThisType::on_shutdown, this,
                    asio::placeholders::error));
            }
            else if (thread_success (ec))
            {
                thread_expect (false);
            }
        }

        void on_shutdown (system::error_code const& ec)
        {
            if (thread_success (ec, true))
            {
                system::error_code ec;

                if (! thread_success (socket ().close (ec)))
                    return;

                closed ();
            }
        }

        void closed ()
        {
        }

    private:
        asio::io_service m_io_service;
        SocketType m_socket;
        asio::streambuf m_buf;
        StreamType m_stream;
        SocketWrapper <StreamType> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // Analyzes the client and server settings to
    // determine if the correct test case outcome was achieved.
    //
    // This relies on distinguishing abnormal errors (like a socket connect
    // failing, which should never happen) from errors that arise naturally
    // because of the test parameters.
    //
    // For example, a non-ssl client attempting a
    // connection to a server that has ssl required.
    //
    void checkScenario (Scenario const& s)
    {
        if (s.handshakeOptions.useClientSsl)
        {
            if (s.handshakeOptions.enableServerSsl)
            {
            }
            else
            {
                // client ssl on, but server ssl disabled
                expect (s.client_error.value () != 0);
            }
        }
    }

    //--------------------------------------------------------------------------

    // Test any generic synchronous client/server pair
    //
    template <class ServerType, class ClientType>
    void testScenario (Scenario scenario = Scenario ())
    {
        String s;
        s   << "scenario <"
            << typeid (ServerType).name () << ", "
            << typeid (ClientType).name () << ">";
        beginTestCase (s);

        system::error_code ec;

        try
        {
            ServerType server (*this, scenario);

            try
            {
                ClientType client (*this, scenario);

                server.start (ec);

                if (expect (! ec, ec.message ()))
                {
                    client.start (ec);

                    if (expect (! ec, ec.message ()))
                    {
                        // at this point the threads for the client and
                        // server should be doing their thing. so we will
                        // just try to stop them within some reasonable
                        // amount of time. by then they should have finished
                        // what they were doing and set the error codes in
                        // the scenario, or they will have gotten hung and
                        // will need to be killed. If they hang, we will
                        // record a timeout in the corresponding scenario
                        // error code and deal with it.
                    }

                    client.finish ();
                }

                server.finish ();

                // only check scenario results if we
                // didn't get an unexpected error
                if (! ec)
                {
                    checkScenario (scenario);
                }
            }
            catch (...)
            {
                failException ();
            }
        }
        catch (...)
        {
            failException ();
        }
    }

    //--------------------------------------------------------------------------

    // Test wrapper and facade assignment and lifetime management
    //
    void testFacade ()
    {
        typedef asio::ip::tcp Protocol;
        typedef Protocol::socket SocketType;

#if 0
        beginTestCase ("facade");

        asio::io_service ios;

        {
            SharedWrapper <SocketType> f1 (new SocketType (ios));
            SharedWrapper <SocketType> f2 (f1);

            expect (f1 == f2);
        }

        {
            SharedWrapper <SocketType> f1 (new SocketType (ios));
            SharedWrapper <SocketType> f2 (new SocketType (ios));

            expect (f1 != f2);
            f2 = f1;
            expect (f1 == f2);
        }
#endif
        // test typedef inheritance
        {
            typedef SocketWrapper <SocketType> SocketWrapper;
            //typedef SocketWrapper::lowest_layer_type lowest_layer_type;
        }
    }

    //--------------------------------------------------------------------------

    template <class ServerType, class ClientType>
    void testHandshakes ()
    {
        testScenario <ServerType, ClientType> (client_ssl | server_ssl);
        testScenario <ServerType, ClientType> (client_ssl | server_ssl_required);
#if 0
        testScenario <ServerType, ClientType> (client_ssl);
        testScenario <ServerType, ClientType> (server_ssl);
        testScenario <ServerType, ClientType> (server_ssl_required);
#endif
    }


    template <class Transport>
    void testTransport ()
    {
#if 1
        // Synchronous
        testScenario <SyncServer  <Transport>, SyncClient  <Transport> > ();
        testScenario <HandshakeSyncServer <Transport, RippleHandshakeStreamType>,
                      SyncClient <Transport> > ();
        testScenario <SyncServer <Transport>,
                      HandshakeSyncClient <Transport, RippleHandshakeStreamType> > ();
        testScenario <HandshakeSyncServer <Transport, RippleHandshakeStreamType>,
                      HandshakeSyncClient <Transport, RippleHandshakeStreamType> > ();
#endif

        // Asynchronous
        testScenario <AsyncServer <Transport>, SyncClient  <Transport> > ();
#if 1
        testScenario <SyncServer  <Transport>, AsyncClient <Transport> > ();
        testScenario <AsyncServer <Transport>, AsyncClient <Transport> > ();

        // Asynchronous
        testScenario <HandshakeSyncServer  <Transport, RippleHandshakeStreamType>,
                      HandshakeAsyncClient <Transport, RippleHandshakeStreamType> > ();
        testScenario <HandshakeAsyncServer <Transport, RippleHandshakeStreamType>,
                      HandshakeSyncClient  <Transport, RippleHandshakeStreamType> > ();
        testScenario <HandshakeAsyncServer <Transport, RippleHandshakeStreamType>,
                      HandshakeAsyncClient <Transport, RippleHandshakeStreamType> > ();

        // Handshaking
        testHandshakes <HandshakeSyncServer  <Transport, RippleHandshakeStreamType>,
                        HandshakeSyncClient  <Transport, RippleHandshakeStreamType> > ();
        testHandshakes <HandshakeSyncServer  <Transport, RippleHandshakeStreamType>,
                        HandshakeAsyncClient <Transport, RippleHandshakeStreamType> > ();
        testHandshakes <HandshakeAsyncServer <Transport, RippleHandshakeStreamType>,
                        HandshakeSyncClient  <Transport, RippleHandshakeStreamType> > ();
        testHandshakes <HandshakeAsyncServer <Transport, RippleHandshakeStreamType>,
                        HandshakeAsyncClient <Transport, RippleHandshakeStreamType> > ();
#endif
    }

//------------------------------------------------------------------------------

    void runTest ()
    {
        testFacade ();
        testTransport <TcpV4> ();
        testTransport <TcpV6> ();
    }
};

static AsioUnitTests asioUnitTests;

}


/*

What we want is a MultiSocket
    Derived from beast::Socket so it can be used generically

BUT, conforms to 


*/
