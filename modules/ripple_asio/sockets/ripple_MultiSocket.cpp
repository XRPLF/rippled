//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

RippleMultiSocket::Options::Options (Flags flags)
    : useClientSsl (false)
    , enableServerSsl (false)
    , requireServerSsl (false)
    , requireServerProxy (false)
{
    setFromFlags (flags);
}

void RippleMultiSocket::Options::setFromFlags (Flags flags)
{
   useClientSsl = (flags & client_ssl) != 0;
   enableServerSsl = (flags & (server_ssl | server_ssl_required)) != 0;
   requireServerSsl = (flags & server_ssl_required) != 0;
   requireServerProxy = (flags & server_proxy) !=0;
}

//------------------------------------------------------------------------------

template <class Stream>
class RippleMultiSocketType : public RippleMultiSocket
{
public:
    // This shouldn't be needed
    /*
    struct SocketInterfaces
        : SocketInterface::Socket
        , SocketInterface::Stream
        , SocketInterface::Handshake
    {};
    */

    typedef typename boost::remove_reference <Stream>::type next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

    typedef typename boost::add_reference <next_layer_type>::type next_layer_type_ref;
    typedef typename boost::asio::ssl::stream <next_layer_type_ref> SslStreamType;

    typedef RippleMultiSocketType <Stream> ThisType;

    enum Status
    {
        needMore,
        proxy,
        plain,
        ssl
    };

    template <class Arg>
    explicit RippleMultiSocketType (Arg& arg, Options options = Options())
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

    bool is_handshaked ()
    {
        return true;
    }

    void* native_object_raw ()
    {
        return &m_next_layer;
    }

    boost::asio::io_service& get_io_service () noexcept
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

    boost::system::error_code cancel (boost::system::error_code& ec)
    {
        return lowest_layer ().cancel (ec);
    }

    boost::system::error_code close (boost::system::error_code& ec)
    {
        return lowest_layer ().close (ec);
    }

    boost::system::error_code shutdown (Socket::shutdown_type what, boost::system::error_code& ec)
    {
        return lowest_layer ().shutdown (what, ec);
    }

    //--------------------------------------------------------------------------
    //
    // StreamInterface
    //
    //--------------------------------------------------------------------------

    std::size_t read_some (MutableBuffers const& buffers, boost::system::error_code& ec)
    {
        if (m_buffer.size () > 0)
        {
            ec = boost::system::error_code ();
            std::size_t const amount = boost::asio::buffer_copy (buffers, m_buffer.data ());
            m_buffer.consume (amount);
            return amount;
        }
        return stream ().read_some (buffers, ec);
    }

    std::size_t write_some (ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        return stream ().write_some (buffers, ec);
    }

    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        if (m_buffer.size () > 0)
        {
            // Return the leftover bytes from the handshake
            std::size_t const amount = boost::asio::buffer_copy (buffers, m_buffer.data ());
            m_buffer.consume (amount);
            return m_io_service.post (m_strand.wrap (boost::bind (
                BOOST_ASIO_MOVE_CAST(TransferCall)(handler), boost::system::error_code (), amount)));
        }
        return stream ().async_read_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred)));
    }

    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return stream ().async_write_some (buffers,
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
    }

    //--------------------------------------------------------------------------

    boost::system::error_code handshake (Socket::handshake_type role, boost::system::error_code& ec)
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

    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        Action const action = calcAction (type);
        switch (action)
        {
        default:
        case actionPlain:
            return handshakePlainAsync (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
            break;

        case actionSsl:
            return handshakeSslAsync (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
            break;

        case actionDetect:
            return detectHandshakeAsync (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
            break;
        }
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    boost::system::error_code handshake (handshake_type type,
        ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        Action action = calcAction (type);
        ec = boost::system::error_code ();
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

    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        Action const action = calcAction (type);
        switch (action)
        {
        default:
        case actionPlain:
            return handshakePlainAsync (buffers,
                BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
            break;

        case actionSsl:
            return handshakeSslAsync (buffers,
                BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
            break;

        case actionDetect:
            return detectHandshakeAsync (buffers,
                BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
            break;
        }
    }
#endif

    boost::system::error_code shutdown (boost::system::error_code& ec)
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

    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        if (m_status == ssl)
        {
            m_ssl_stream->async_shutdown (m_strand.wrap (boost::bind (
                BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
                boost::asio::placeholders::error)));
        }
        else
        {
            boost::system::error_code ec;
            m_next_layer.shutdown (next_layer_type::shutdown_both, ec);
            m_io_service.post (m_strand.wrap (boost::bind (
                BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec)));
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
    Action calcDetectAction (boost::system::error_code& ec)
    {
        ec = boost::system::error_code ();

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
    void failedHandshake (boost::system::error_code& ec)
    {
        // VFALCO TODO maybe use a ripple error category?
        // set this to something custom that we can recognize later?
        ec = boost::asio::error::invalid_argument;
    }

    void createPlainStream ()
    {
        m_status = plain;
        m_stream = new SocketWrapper <next_layer_type> (m_next_layer);
    }

    void handshakePlain (boost::system::error_code& ec)
    {
        ec = boost::system::error_code ();
        createPlainStream ();
    }

    void handshakePlain (ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        fatal_assert (boost::asio::buffer_size (buffers) == 0 );
        ec = boost::system::error_code ();
        createPlainStream ();
    }

    BOOST_ASIO_INITFN_RESULT_TYPE(ErrorCall, void (boost::system::error_code))
    handshakePlainAsync (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        createPlainStream ();
        return m_io_service.post (m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), boost::system::error_code())));
    }

    void createSslStream ()
    {
        m_status = ssl;
        m_ssl_stream = new SslStreamType (m_next_layer, m_context->getBoostContext ());
        m_stream = new SocketWrapper <SslStreamType> (*m_ssl_stream);
    }

    void handshakeSsl (boost::system::error_code& ec)
    {
        createSslStream ();
        m_ssl_stream->handshake (m_role, ec);
    }

    BOOST_ASIO_INITFN_RESULT_TYPE(ErrorCall, void (boost::system::error_code))
    handshakeSslAsync (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        createSslStream ();
        return m_ssl_stream->async_handshake (m_role,
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    BOOST_ASIO_INITFN_RESULT_TYPE(TransferCall, void (boost::system::error_code, std::size_t))
    handshakePlainAsync (ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG (TransferCall) handler)
    {
        fatal_assert (boost::asio::buffer_size (buffers) == 0);
        createPlainStream ();
        return m_io_service.post (m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            boost::system::error_code(), 0)));
    }

    void handshakeSsl (ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        createSslStream ();
        m_ssl_stream->handshake (m_role, buffers, ec);
    }

    BOOST_ASIO_INITFN_RESULT_TYPE (TransferCall, void (boost::system::error_code, std::size_t))
    handshakeSslAsync (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        createSslStream ();
        return m_ssl_stream->async_handshake (m_role, buffers,
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
    }
#endif

    //--------------------------------------------------------------------------

    enum
    {
        autoDetectBytes = 5
    };

    void detectHandshake (boost::system::error_code& ec)
    {
        // Top up our buffer
        bassert (m_buffer.size () == 0);
        std::size_t const needed = autoDetectBytes;
        std::size_t const amount =  m_next_layer.receive (
            m_buffer.prepare (needed), boost::asio::socket_base::message_peek, ec);
        m_buffer.commit (amount);
        if (! ec)
        {
            analyzeHandshake (m_buffer.data ());
            m_buffer.consume (amount);
            if (m_status == needMore)
                ec = boost::asio::error::invalid_argument; // should never happen
        }
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    void detectHandshake (ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        m_buffer.commit (boost::asio::buffer_copy (
            m_buffer.prepare (boost::asio::buffer_size (buffers)), buffers));
        detectHandshake (ec);
    }
#endif

    //--------------------------------------------------------------------------

    void onDetectRead (BOOST_ASIO_MOVE_ARG(ErrorCall) handler,
        boost::system::error_code const& ec, std::size_t bytes_transferred)
    {
        m_buffer.commit (bytes_transferred);

        if (! ec)
        {
            analyzeHandshake (m_buffer.data ());

            boost::system::error_code ec;

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
                            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
                        break;
                    case actionSsl:
                        handshakeSslAsync (
                            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
                        break;
                    };
                }
            }
            else
            {
                ec = boost::asio::error::invalid_argument;
            }

            if (ec)
            {
                m_io_service.post (m_strand.wrap (boost::bind (
                    BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec)));
            }
        }
    }

    BOOST_ASIO_INITFN_RESULT_TYPE(ErrorCall, void (boost::system::error_code))
    detectHandshakeAsync (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        bassert (m_buffer.size () == 0);
        return m_next_layer.async_receive (
            m_buffer.prepare (autoDetectBytes), boost::asio::socket_base::message_peek,
            m_strand.wrap (boost::bind (&ThisType::onDetectRead, this,
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)));
    }

#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    BOOST_ASIO_INITFN_RESULT_TYPE(TransferCall, void (boost::system::error_code, std::size_t))
    detectHandshakeAsync (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
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

        std::size_t const bytes = boost::asio::buffer_copy (boost::asio::buffer (data), buffers);

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
    ScopedPointer <RippleTlsContext> m_context;
    Stream m_next_layer;
    boost::asio::io_service& m_io_service;
    boost::asio::io_service::strand m_strand;
    Status m_status;
    Socket::handshake_type m_role;
    ScopedPointer <Socket> m_stream;
    ScopedPointer <SslStreamType> m_ssl_stream;
    boost::asio::streambuf m_buffer;
};

//------------------------------------------------------------------------------

RippleMultiSocket* RippleMultiSocket::New (boost::asio::io_service& io_service,
                                           Options const& options)
{
    return new RippleMultiSocketType <boost::asio::ip::tcp::socket> (io_service, options);
}

