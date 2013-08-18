//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MULTISOCKETTYPE_H_INCLUDED
#define RIPPLE_MULTISOCKETTYPE_H_INCLUDED

/** Template for producing instances of MultiSocket
*/
template <class StreamSocket>
class MultiSocketType
    : public MultiSocket
{
protected:
#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // For some reason, the ssl::stream buffered handshaking
    // routines don't seem to work right. Setting this bool
    // to false will bypass the ssl::stream buffered handshaking 
    // API and just resort to our PrefilledReadStream which works
    // great and we need it anyway to handle the non-buffered case.
    //
    static bool const useBufferedHandshake = false;
#endif

    typedef boost::system::error_code error_code;

public:
    typedef typename boost::remove_reference <StreamSocket>::type next_layer_type;
    typedef typename boost::add_reference <next_layer_type>::type next_layer_type_ref;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
    typedef typename boost::asio::ssl::stream <next_layer_type_ref> ssl_stream_type;

    template <class Arg>
    explicit MultiSocketType (Arg& arg, int flags = 0)
        : m_flags (cleaned_flags (flags))
        , m_needs_handshake (false)
        , m_needs_shutdown (false)
        , m_needs_shutdown_stream (false)
        , m_next_layer (arg)
        , m_stream (new_stream ())
        , m_strand (get_io_service ())
    {
    }

protected:
    /** The current stream we are passing everything through.
        This object gets dynamically created and replaced with other
        objects as we process the various flags for handshaking.
    */
    Socket& stream () const noexcept
    {
        bassert (m_stream != nullptr);
        return *m_stream;
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
    //
    // basic_io_object
    //

    boost::asio::io_service& get_io_service ()
    {
        return m_next_layer.get_io_service ();
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //

    void* lowest_layer (char const* type_name) const
    {
        char const* const name (typeid (lowest_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*>(&lowest_layer ()));
        return nullptr;
    }

    void* native_handle (char const* type_name) const
    {
        char const* const name (typeid (next_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*>(&next_layer ()));
        return nullptr;
    }

    error_code cancel (error_code& ec)
    {
        return lowest_layer ().cancel (ec);
    }

    error_code shutdown (Socket::shutdown_type what, error_code& ec)
    {
        return lowest_layer ().shutdown (what, ec);
    }

    error_code close (error_code& ec)
    {
        return lowest_layer ().close (ec);
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    std::size_t read_some (MutableBuffers const& buffers, error_code& ec)
    {
        return stream ().read_some (buffers, ec);
    }

    std::size_t write_some (ConstBuffers const& buffers, error_code& ec)
    {
        return stream ().write_some (buffers, ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(HandlerCall) handler)
    {
        return stream ().async_read_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(HandlerCall)(handler), boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(HandlerCall) handler)
    {
        return stream ().async_write_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(HandlerCall)(handler), boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    /** Determine if the caller needs to call a handshaking function. */
    bool needs_handshake ()
    {
        return m_needs_handshake;
    }

    error_code handshake (Socket::handshake_type type, error_code& ec)
    {
        if (init_handshake (type, ec))
            return stream ().handshake (type, ec);
        return ec;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandlerCall) handler)
    {
        bassert (m_context.isNull ());

        error_code ec;
        if (init_handshake (type, ec))
        {
            // The original handler has already been wrapped for us
            // so set the context and indicate the beginning of the
            // composed operation.
            //
            //handler.beginComposed ();
            //m_context = handler.getContext ();
            return stream ().async_handshake (type, handler);
        }

        return get_io_service ().post (HandlerCall (HandlerCall::Post (),
            BOOST_ASIO_MOVE_CAST(HandlerCall)(handler), ec));
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec)
    {
        if (init_handshake (type, ec))
            return stream ().handshake (type, buffers, ec);
        return ec;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(HandlerCall) handler)
    {
        error_code ec;
        if (init_handshake (type, ec))
            return stream ().async_handshake (type, buffers,
                BOOST_ASIO_MOVE_CAST(HandlerCall)(handler));
        return get_io_service ().post (HandlerCall (handler, ec, 0));
    }
#endif

    error_code shutdown (error_code& ec)
    {
        if (m_needs_shutdown)
        {
            if (m_needs_shutdown_stream)
                return stream ().shutdown (ec);
            return ec = error_code ();
        }
        return handshake_error (ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(HandlerCall) handler)
    {
        error_code ec;
        if (m_needs_shutdown)
        {
            if (m_needs_shutdown_stream)
                return stream ().async_shutdown (
                    BOOST_ASIO_MOVE_CAST(HandlerCall)(handler));
        }
        else
        {
            ec = handshake_error ();
        }

        return get_io_service ().post (HandlerCall (HandlerCall::Post (),
            BOOST_ASIO_MOVE_CAST(HandlerCall)(handler),
                ec));
    }

    //--------------------------------------------------------------------------
    //
    // Handshake implementation details
    //
    //--------------------------------------------------------------------------

    // Checks flags for preconditions and returns a cleaned-up version
    //
    static Flag cleaned_flags (Flag flags)
    {
        // Can't set both client and server
        check_precondition (! flags.set (Flag::client_role | Flag::server_role));

        if (flags.set (Flag::client_role))
        {
            // clients ignore ssl_required
            flags = flags.without (Flag::ssl_required);
        }
        else if (flags.set (Flag::server_role))
        {
            // servers ignore ssl when ssl_required is set
            if (flags.set (Flag::ssl_required))
                flags = flags.without (Flag::ssl);
        }
        else
        {
            // if not client or server, clear out all the flags
            flags = Flag::peer;
        }

        return flags;
    }

    //--------------------------------------------------------------------------

    // Create a plain stream that just wraps the next layer.
    //
    Socket* new_plain_stream ()
    {
        return new SocketWrapper <next_layer_type&> (m_next_layer);
    }

    // Create a plain stream but front-load it with some bytes.
    // A copy of the buffers is made.
    //
    template <typename ConstBufferSequence>
    Socket* new_plain_stream (ConstBufferSequence const& buffers)
    {
        if (boost::asio::buffer_size (buffers) > 0)
        {
            typedef PrefilledReadStream <next_layer_type&> this_type;
            return new SocketWrapper <this_type> (m_next_layer, buffers);
        }
        return new_plain_stream ();
    }

    // Creates an ssl stream
    //
    Socket* new_ssl_stream ()
    {
        typedef typename boost::asio::ssl::stream <next_layer_type&> this_type;
        return new SocketWrapper <this_type> (
            m_next_layer, MultiSocket::getRippleTlsBoostContext ());
    }

    // Creates an ssl stream, but front-load it with some bytes.
    // A copy of the buffers is made.
    //
    template <typename ConstBufferSequence>
    Socket* new_ssl_stream (ConstBufferSequence const& buffers)
    {
        if (boost::asio::buffer_size (buffers) > 0)
        {
            typedef PrefilledReadStream <next_layer_type&> next_type;
            typedef boost::asio::ssl::stream <next_type> this_type;
            SocketWrapper <this_type>* const socket = new SocketWrapper <this_type> (
                m_next_layer, MultiSocket::getRippleTlsBoostContext ());
            socket->this_layer().next_layer().fill (buffers);
            return socket;
        }
        return new_ssl_stream ();
    }

    // Creates a stream that detects whether or not an
    // SSL handshake is being attempted.
    //
    Socket* new_detect_ssl_stream ()
    {
        SSL3DetectCallback* callback = new SSL3DetectCallback (this);
        typedef HandshakeDetectStreamType <next_layer_type&, HandshakeDetectLogicSSL3> this_type;
        return new SocketWrapper <this_type> (callback, m_next_layer);
    }

    // Creates an ssl detect stream and front-loads it with data.
    //
    template <typename ConstBufferSequence>
    Socket* new_detect_ssl_stream (ConstBufferSequence const& buffers)
    {
        if (boost::asio::buffer_size (buffers) > 0)
        {
            SSL3DetectCallback* callback = new SSL3DetectCallback (this);
            typedef HandshakeDetectStreamType <next_layer_type&, HandshakeDetectLogicSSL3> this_type;
            SocketWrapper <this_type>* const socket =
                new SocketWrapper <this_type> (callback, m_next_layer);
            socket->this_layer ().fill (buffers);
            return socket;
        }
        return new_detect_ssl_stream ();
    }

    // Creates a stream that detects whether or not a PROXY
    // handshake is being sent.
    //
    template <typename ConstBufferSequence>
    Socket* new_proxy_stream (ConstBufferSequence const& buffers)
    {
        // Doesn't make sense to already have data if we're expecting PROXY
        check_precondition (boost::asio::buffer_size (buffers) == 0);

        PROXYDetectCallback* callback = new PROXYDetectCallback (this);
        typedef HandshakeDetectStreamType <next_layer_type&, HandshakeDetectLogicPROXY> this_type;
        return new SocketWrapper <this_type> (callback, m_next_layer);
    }

    //--------------------------------------------------------------------------

    // Called at construction time, this returns a stream appropriate for
    // the current settings. If the return value is nullptr, it means that
    // the stream cannot be determined until handshake or async_handshake
    // is called. In that case, init_handshake will set m_stream.
    //
    // This also sets the m_needs_handshake and m_needs_shutdown flags
    //
    Socket* new_stream ()
    {
        Socket* socket = nullptr;

        if (is_client ())
        {
            if (m_flags.set (Flag::proxy))
            {
                // Client sends PROXY in the plain so make
                // sure they have an underlying stream right away.
                socket = new_plain_stream ();
                m_needs_handshake = m_flags.set (Flag::ssl);
            }
            else if (m_flags.set (Flag::ssl))
            {
                socket = nullptr;
                m_needs_handshake = true;
            }
            else
            {
                socket = new_plain_stream ();
                m_needs_handshake = false;
            }
        }
        else if (is_server ())
        {
            if (m_flags.set (Flag::proxy))
            {
                // PROXY comes first in the plain
                socket = nullptr;
                m_needs_handshake = true;
            }
            else if (m_flags.set (Flag::ssl_required))
            {
                socket = nullptr;
                m_needs_handshake = true;
            }
            else if (m_flags.set (Flag::ssl))
            {
                socket = nullptr;
                m_needs_handshake = true;
            }
            else
            {
                m_needs_handshake = false;
                socket = new_plain_stream ();
            }
        }
        else
        {
            // peer, plain stream, no handshake
            m_needs_handshake = false;
            socket = new_plain_stream ();
        }

        // We only set this to true when the handshake succeeds.
        m_needs_shutdown = false;

        return socket;
    }

    //--------------------------------------------------------------------------

    // Bottleneck to indicate a failed handshake.
    //
    error_code handshake_error ()
    {
        // VFALCO TODO maybe use a ripple error category?
        // set this to something custom that we can recognize later?
        //
        m_needs_shutdown = false;
        return boost::asio::error::invalid_argument;
    }

    error_code handshake_error (error_code& ec)
    {
        return ec = handshake_error ();
    }

    // Sanity checks and cleans up our flags based on the desired handshake
    // type and correctly sets the current stream based on the settings.
    // If the requested handshake type is incompatible with the socket flags
    // set on construction, an error is returned.
    //
    // This also clears the m_needs_handshake flag on an error.
    //
    // Returns true if the stream was set.
    //
    bool init_handshake (handshake_type type,
        error_code& ec, ConstBuffers const& buffers = ConstBuffers())
    {
        bassert (m_needs_handshake);

        // Peer roles cannot handshake.
        if (m_flags == Flag (Flag::peer))
        {
            handshake_error (ec);
            return false;
        }

        // Handshake type must match the role flags
        if ( (type == client && ! is_client ()) ||
             (type == server && ! is_server ()))
        {
            handshake_error (ec);
            return false;
        }

        ec = error_code ();

        if (is_client ())
        {
            // PROXY sent before handshake, in plain
            m_flags = m_flags.without (Flag::proxy);

            if (! m_flags.set (Flag::ssl))
            {
                handshake_error (ec);
                return false;
            }

            m_stream = new_ssl_stream (buffers);
            m_needs_shutdown_stream = true;
        }
        else
        {
            bassert (is_server ());

            if (m_flags.set (Flag::proxy))
            {
                m_stream = new_proxy_stream (ConstBuffers ());   
            }                
            else if (m_flags.set (Flag::ssl_required))
            {
                m_stream = new_ssl_stream (buffers);
                m_needs_shutdown_stream = true;
            }
            else if (m_flags.set (Flag::ssl))
            {
                m_stream = new_detect_ssl_stream (buffers);
            }
            else
            {
                // This happens after a proxy handshake
                m_stream = new_plain_stream (buffers);
            }
        }

        m_needs_shutdown = true;

        return true;
    }

    //--------------------------------------------------------------------------
    //
    // PROXY callback
    //
    class PROXYDetectCallback
        : public HandshakeDetectStream <HandshakeDetectLogicPROXY>::Callback 
    {
    public:
        typedef HandshakeDetectLogicPROXY LogicType;

        explicit PROXYDetectCallback (MultiSocketType <StreamSocket>* owner)
            : m_owner (owner)
        {
        }

        void on_detect (LogicType& logic,
            error_code& ec, ConstBuffers const& buffers)
        {
            m_owner->on_detect_proxy (logic, ec, buffers);
        }

        void on_async_detect (LogicType& logic, error_code const& ec,
            ConstBuffers const& buffers, HandlerCall const& origHandler)
        {
            m_owner->on_async_detect_proxy (logic, ec, buffers, origHandler);
        }

    #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
        void on_async_detect (LogicType& logic, error_code const& ec,
            ConstBuffers const& buffers, HandlerCall const& origHandler)
        {
            m_owner->on_async_detect_proxy (logic, ec, buffers, origHandler);
        }
    #endif

    private:
        MultiSocketType <StreamSocket>* m_owner;
    };

    //--------------------------------------------------------------------------

    void setProxyInfo (HandshakeDetectLogicPROXY::ProxyInfo const proxyInfo)
    {
        // Do something with it
    }

    void on_detect_proxy (HandshakeDetectLogicPROXY& logic,
        error_code& ec, ConstBuffers const& buffers)
    {
        bassert (is_server ());

        if (ec)
            return;

        if (! logic.success ())
        {
            handshake_error (ec);
            return;
        }

        // Remove the proxy flag
        m_flags = m_flags.without (Flag::proxy);

        setProxyInfo (logic.getInfo ());

        if (init_handshake (Socket::server, ec, buffers))
        {
            if (stream ().needs_handshake ())
                stream ().handshake (Socket::server, ec);
        }
    }

    // origHandler cannot be a reference since it gets move-copied
    void on_async_detect_proxy (HandshakeDetectLogicPROXY& logic,
        error_code ec, ConstBuffers const& buffers, HandlerCall origHandler)
    {
        if (! ec)
        {
            // Failed to receive PROXY handshake
            if (! logic.success ())
                handshake_error (ec);
        }

        if (! ec)
        {
            // Remove the proxy flag from our flag set
            m_flags = m_flags.without (Flag::proxy);

            setProxyInfo (logic.getInfo ());

            if (init_handshake (Socket::server, ec, buffers))
            {
                if (stream ().needs_handshake ())
                {
                    stream ().async_handshake (Socket::server, origHandler);
                    return;
                }
            }
        }

        get_io_service ().post (HandlerCall (HandlerCall::Post (),
            BOOST_ASIO_MOVE_CAST(HandlerCall)(origHandler), ec));
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // origHandler cannot be a reference since it gets move-copied
    void on_async_detect_proxy (HandshakeDetectLogicPROXY& logic,
        error_code const& ec, ConstBuffers const& buffers, HandlerCall origHandler)
    {
        std::size_t bytes_transferred = 0;

        if (! ec)
        {
            if (logic.success ())
            {
            }
        }
    }
#endif

    //--------------------------------------------------------------------------

    void on_detect_ssl (HandshakeDetectLogicSSL3& logic,
        error_code& ec, ConstBuffers const& buffers)
    {
        bassert (is_server ());

        if (! ec)
        {
            if (logic.success ())
            {
            #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
                if (useBufferedHandshake)
                {
                    m_stream = new_ssl_stream ();
                    stream ().handshake (Socket::server, buffers, ec);
                    return;
                }
            #endif

                m_stream = new_ssl_stream (buffers);
                m_needs_shutdown_stream = true;

                stream ().handshake (Socket::server, ec);
                return;
            }

            m_stream = new_plain_stream (buffers);
        }
    }

    // origHandler cannot be a reference since it gets move-copied
    void on_async_detect_ssl (HandshakeDetectLogicSSL3& logic,
        error_code const& ec, ConstBuffers const& buffers, HandlerCall origHandler)
    {
        if (! ec)
        {
            if (logic.success ())
            {
                m_stream = new_ssl_stream (buffers);
                m_needs_shutdown_stream = true;
                // Is this a continuation?
                stream ().async_handshake (Socket::server, origHandler);
                return;
            }

            // Note, this assignment will destroy the
            // detector, and our allocated callback along with it.
            m_stream = new_plain_stream (buffers);
        }

        // Is this a continuation?
        get_io_service ().post (HandlerCall (HandlerCall::Post (),
            origHandler, ec));
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // origHandler cannot be a reference since it gets move-copied
    void on_async_detect_ssl (HandshakeDetectLogicSSL3& logic,
        error_code const& ec, ConstBuffers const& buffers, HandlerCall origHandler)
    {
        std::size_t bytes_transferred = 0;

        if (! ec)
        {
            if (logic.success ())
            {
                // Is this a continuation?
                m_stream = new_ssl_stream ();
                stream ().async_handshake (Socket::server, buffers, origHandler);
                return;
            }

            // Note, this assignment will destroy the
            // detector, and our allocated callback along with it.
            m_stream = new_plain_stream (buffers);

            // Since the handshake was not SSL we leave
            // bytes_transferred at zero since we didn't use anything.
        }

        // Is this a continuation?
        get_io_service ().post (HandlerCall (
            origHandler, ec, bytes_transferred));
    }
#endif

    //--------------------------------------------------------------------------
    //
    // SSL3 Callback
    //
    class SSL3DetectCallback
        : public HandshakeDetectStream <HandshakeDetectLogicSSL3>::Callback 
    {
    public:
        typedef HandshakeDetectLogicSSL3 LogicType;

        explicit SSL3DetectCallback (MultiSocketType <StreamSocket>* owner)
            : m_owner (owner)
        {
        }

        void on_detect (LogicType& logic, error_code& ec,
            ConstBuffers const& buffers)
        {
            m_owner->on_detect_ssl (logic, ec, buffers);
        }

        void on_async_detect (LogicType& logic, error_code const& ec,
            ConstBuffers const& buffers, HandlerCall const& origHandler)
        {
            m_owner->on_async_detect_ssl (logic, ec, buffers, origHandler);
        }

    #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
        void on_async_detect (LogicType& logic, error_code const& ec,
            ConstBuffers const& buffers, HandlerCall const& origHandler)
        {
            m_owner->on_async_detect_ssl (logic, ec, buffers, origHandler);
        }
    #endif

    private:
        MultiSocketType <StreamSocket>* m_owner;
    };

    //--------------------------------------------------------------------------
    //
    // Utilities
    //

    bool is_server () const noexcept
    {
        return m_flags.set (Flag::server_role);
    }

    bool is_client () const noexcept
    {
        return m_flags.set (Flag::client_role);
    }

    bool is_peer () const noexcept
    {
        bassert (is_client () || is_server () || (m_flags == Flag (Flag::peer)));
        return m_flags == Flag (Flag::peer);
    }

private:
    Flag m_flags;
    bool m_needs_handshake;
    bool m_needs_shutdown;
    bool m_needs_shutdown_stream;
    StreamSocket m_next_layer;
    ScopedPointer <Socket> m_stream;
    boost::asio::io_service::strand m_strand;
    HandlerCall::Context m_context;
};

#endif
