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
    typedef boost::system::error_code error_code;

public:
    typedef typename boost::remove_reference <StreamSocket>::type next_layer_type;
    typedef typename boost::add_reference <next_layer_type>::type next_layer_type_ref;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
    typedef typename boost::asio::ssl::stream <next_layer_type_ref> ssl_stream_type;

    template <class Arg>
    explicit MultiSocketType (Arg& arg, int flags = 0)
        : m_flags (flags)
        , m_state (stateNone)
        , m_stream (nullptr)
        , m_needsShutdown (false)
        , m_next_layer (arg)
        , m_strand (get_io_service ())
    {
        // See if our flags allow us to go directly
        // into the ready state with an active stream.
        initState ();
    }

protected:
    // Tells us what to do next
    //
    enum State
    {
        stateNone,              // Uninitialized, unloved.
        stateHandshake,         // We need a call to handshake() to proceed
        stateExpectPROXY,       // We expect to see a proxy handshake
        stateDetectSSL,         // We should detect SSL
        stateHandshakeFinal,    // Final call to underlying stream handshake()
        stateReady              // Stream is set and ready to go
    };

    //--------------------------------------------------------------------------

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

    void async_read_some (MutableBuffers const& buffers, SharedHandlerPtr handler)
    {
#if 0
        stream ().async_read_some (buffeers,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
#else
        stream ().async_read_some (buffers, m_strand.wrap (
            boost::bind (handler, boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
#endif
    }

    void async_write_some (ConstBuffers const& buffers, SharedHandlerPtr handler)
    {
#if 0
        stream ().async_write_some (buffers,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
#else
        stream ().async_write_some (buffers, m_strand.wrap (
            boost::bind ( handler, boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    /** Determine if the caller needs to call a handshaking function.
        This is also used to determine if the handshaking shutdown()
        has to be called.
    */
    bool needs_handshake ()
    {
        return m_state == stateHandshake || 
               m_state == stateHandshakeFinal ||
               m_needsShutdown;
    }

    //--------------------------------------------------------------------------

    error_code handshake (Socket::handshake_type type, error_code& ec)
    {
        return handshake (type, ConstBuffers (), ec);
    }

    // We always offer the buffered handshake version even pre-boost 1.54 since
    // we need the ability to re-use data for multiple handshake stages anyway.
    //
    error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec)
    {
        return do_handshake (type, buffers, ec);
    }

    //--------------------------------------------------------------------------

    void async_handshake (handshake_type type, SharedHandlerPtr handler)
    {
        do_async_handshake (type, ConstBuffers (), handler);
    }

    void async_handshake (handshake_type type,
        ConstBuffers const& buffers, SharedHandlerPtr handler)
    {
        do_async_handshake (type, buffers, handler);
    }

    void do_async_handshake (handshake_type type,
        ConstBuffers const& buffers, SharedHandlerPtr const& handler)
    {
        return get_io_service ().dispatch (SharedHandlerPtr (
            new AsyncOp (this, type, buffers, handler)));
    }

    //--------------------------------------------------------------------------

    error_code shutdown (error_code& ec)
    {
        if (m_needsShutdown)
        {
            // Only do the shutdown if the stream really needs it.
            if (stream ().needs_handshake ())
                return ec = stream ().shutdown (ec);

            return ec = error_code ();
        }

        return ec = handshake_error ();
    }

    void async_shutdown (SharedHandlerPtr handler)
    {
        error_code ec;

        if (m_needsShutdown)
        {
            if (stream ().needs_handshake ())
            {
                stream ().async_shutdown (
                    BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));

                return;
            }
        }
        else
        {
            // Our interface didn't require a shutdown but someone called
            // it anyone so generate an error code.
            bassertfalse;
            ec = handshake_error ();
        }

        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (ec);
    }

    //--------------------------------------------------------------------------
    //
    // Utilities
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

    // Bottleneck to indicate a failed handshake.
    //
    error_code handshake_error ()
    {
        // VFALCO TODO maybe use a ripple error category?
        // set this to something custom that we can recognize later?
        //
        m_needsShutdown = false;
        return boost::asio::error::invalid_argument;
    }

    error_code handshake_error (error_code& ec)
    {
        return ec = handshake_error ();
    }

    //--------------------------------------------------------------------------
    //
    // State Machine
    //
    //--------------------------------------------------------------------------

    // Initialize the current state based on the flags. This is
    // called from the constructor. It is possible that a state
    // cannot be determined until the handshake type is known,
    // in which case we will leave the state at stateNone and the
    // current stream set to nullptr
    //
    void initState ()
    {
        // Clean our flags up
        m_flags = cleaned_flags (m_flags);

        if (is_client ())
        {
            if (m_flags.set (Flag::proxy))
            {
                if (m_flags.set (Flag::ssl))
                    m_state = stateHandshake;
                else
                    m_state = stateReady;

                // Client sends PROXY in the plain so make
                // sure they have an underlying stream right away.
                m_stream = new_plain_stream ();
            }
            else if (m_flags.set (Flag::ssl))
            {
                m_state = stateHandshakeFinal;
                m_stream = nullptr;
            }
            else
            {
                m_state = stateReady;
                m_stream = new_plain_stream ();
            }
        }
        else if (is_server ())
        {
            if (m_flags.set (Flag::proxy))
            {
                // We expect a PROXY handshake.
                // Create the plain stream at handshake time.
                m_state = stateHandshake;
                m_stream = nullptr;
            }
            else if (m_flags.set (Flag::ssl_required))
            {
                // We require an SSL handshake.
                // Create the stream at handshake time.
                m_state = stateHandshakeFinal;
                m_stream = nullptr;
            }
            else if (m_flags.set (Flag::ssl))
            {
                // We will use the SSL detector at handshake
                // time decide which type of stream to create.
                m_state = stateHandshake;
                m_stream = nullptr;
            }
            else
            {
                // No handshaking required.
                m_state = stateReady;
                m_stream = new_plain_stream ();
            }
        }
        else
        {
            m_state = stateReady;
            m_stream = new_plain_stream ();
        }

        // We only set this to true in stateHandshake and
        // after the handshake completes without an error.
        //
        m_needsShutdown = false;
    }

    //--------------------------------------------------------------------------

    // Used for the non-buffered handshake functions.
    //
    error_code initHandshake (handshake_type type)
    {
        return initHandshake (type, ConstBuffers ());
    }

    // Updates the state based on the now-known handshake type. The
    // buffers parameter contains bytes that have already been received.
    // This can come from the results of SSL detection, or from the buffered
    // handshake API calls added in Boost v1.54.0.
    //
    error_code initHandshake (handshake_type type, ConstBuffers const& buffers)
    {
        switch (m_state)
        {
        case stateExpectPROXY:
        case stateDetectSSL:
            m_state = stateHandshake;
        case stateHandshake:
        case stateHandshakeFinal:
            break;
        case stateNone:
        case stateReady:
        default:
            // Didn't need handshake, but someone called us anyway?
            fatal_error ("invalid state");
            return handshake_error ();
        }

        // Peer roles cannot handshake.
        if (m_flags == Flag (Flag::peer))
            return handshake_error ();

        // Handshake type must match the role flags
        if ( (type == client && ! is_client ()) ||
             (type == server && ! is_server ()))
            return handshake_error ();

        if (is_client ())
        {
            // if PROXY flag is set, then it should have already
            // been sent in the clear before calling handshake()
            // so strip the flag away.
            //
            m_flags = m_flags.without (Flag::proxy);

            // Someone forgot to call needs_handshake
            if (! m_flags.set (Flag::ssl))
                return handshake_error ();

            m_state = stateHandshakeFinal;
            m_stream = new_ssl_stream (buffers);
        }
        else
        {
            bassert (is_server ());

            if (m_flags.set (Flag::proxy))
            {
                // We will expect and consume a PROXY handshake,
                // then come back here with the flag cleared.
                m_state = stateExpectPROXY;
                m_stream = new_plain_stream ();
            }
            else if (m_flags.set (Flag::ssl_required))
            {
                // WE will perform a required final SSL handshake.
                m_state = stateHandshakeFinal;
                m_stream = new_ssl_stream (buffers);
            }
            else if (m_flags.set (Flag::ssl))
            {
                // We will use the SSL detector to update
                // our flags and come back through here.
                m_state = stateDetectSSL;
                m_stream = nullptr;
            }
            else
            {
                // Done with auto-detect
                m_state = stateReady;
                m_stream = new_plain_stream (buffers);
            }
        }

        return error_code ();
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

    //--------------------------------------------------------------------------

    void setProxyInfo (HandshakeDetectLogicPROXY::ProxyInfo const proxyInfo)
    {
        // Do something with it
    }

    //--------------------------------------------------------------------------
    //
    // Synchronous handshake operation
    //

    error_code do_handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec)
    {
        ec = initHandshake (type);
        if (ec)
            return ec;

        // How can we be ready if a handshake is needed?
        bassert (m_state != stateReady);

        // Prepare our rolling detect buffer with any input
        boost::asio::streambuf buffer;
        buffer.commit (boost::asio::buffer_copy (
            buffer.prepare (buffers.size ()), buffers));

        // Run a loop of processing and detecting handshakes
        // layer after layer until we arrive at the ready state
        // with a final stream.
        //
        do
        {
            switch (m_state)
            {
            case stateHandshakeFinal:
                {
                    // A 'real' final handshake on the stream is needed.
                    m_state = stateReady;
                    stream ().handshake (type, ec);
                }
                break;

            case stateExpectPROXY:
                {
                    typedef HandshakeDetectorType <
                        next_layer_type, HandshakeDetectLogicPROXY> op_type;

                    op_type op;

                    ec = op.detect (m_next_layer, buffer);

                    if (! ec)
                    {
                        bassert (op.getLogic ().finished ());

                        if (op.getLogic ().success ())
                        {
                            setProxyInfo (op.getLogic ().getInfo ());

                            // Strip off the PROXY flag.
                            m_flags = m_flags.without (Flag::proxy);

                            // Update handshake state with the leftover bytes.
                            ec = initHandshake (type, buffer.data ());

                            // buffer input sequence intentionally untouched
                        }
                        else
                        {
                            // Didn't get the PROXY handshake we needed
                            ec = handshake_error ();
                        }
                    }
                }
                break;

            case stateDetectSSL:
                {
                    typedef HandshakeDetectorType <
                        next_layer_type, HandshakeDetectLogicSSL3> op_type;

                    op_type op;

                    ec = op.detect (m_next_layer, buffer);

                    if (! ec)
                    {
                        bassert (op.getLogic ().finished ());

                        // Was it SSL?
                        if (op.getLogic ().success ())
                        {
                            // Convert the ssl flag to ssl_required
                            m_flags = m_flags.with (Flag::ssl_required).without (Flag::ssl);
                        }
                        else
                        {
                            // Not SSL, strip the ssl flag
                            m_flags = m_flags.without (Flag::ssl);
                        }

                        // Update handshake state with the leftover bytes.
                        ec = initHandshake (type, buffer.data ());

                        // buffer input sequence intentionally untouched
                    }
                }
                break;

            case stateNone:
            case stateReady:
            case stateHandshake:
            default:
                fatal_error ("invalid state");
                ec = handshake_error ();
                break;
            }

            if (ec)
                break;
        }
        while (m_state != stateReady);

        if (! ec)
        {
            // We should be in the ready state now.
            bassert (m_state == stateReady);

            // Always need shutdown if handshake successful.
            m_needsShutdown = true;
        }

        return ec;
    }

    //--------------------------------------------------------------------------
    //
    // Composed asynchronous handshake operator
    //

    struct AsyncOp : ComposedAsyncOperation
    {
        typedef SharedHandlerAllocator <char> Allocator;
        
        AsyncOp (MultiSocketType <StreamSocket>* socket,
            handshake_type type, ConstBuffers const& buffers,
                SharedHandlerPtr const& handler)
            : ComposedAsyncOperation (sizeof (*this), handler)
            , m_handler (handler)
            , m_socket (socket)
            , m_type (type)
            , m_buffer (std::numeric_limits <std::size_t>::max(), handler)
            , m_running (false)
        {
            // Prepare our rolling detect buffer with any input
            //
            // NOTE We have to do this in reverse order from the synchronous
            // version because the callers buffers won't be in scope inside
            // our call.
            //
            m_buffer.commit (boost::asio::buffer_copy (
                m_buffer.prepare (buffers.size ()), buffers));
        }

        // Set breakpoint to prove it gets destroyed
        ~AsyncOp ()
        {
        }

        // This is the entry point into the composed operation
        //
        void operator() ()
        {
            m_running = true;

            error_code ec (m_socket->initHandshake (m_type));

            if (! ec)
            {
                if (m_socket->m_state != stateReady)
                {
                    (*this) (ec);
                    return;
                }

                // Always need shutdown if handshake successful.
                m_socket->m_needsShutdown = true;
            }

            // Call the original handler with the error code and end.
            m_socket->get_io_service ().wrap (
                BOOST_ASIO_MOVE_CAST (SharedHandlerPtr)(m_handler))
                    (ec);
        }

        // This implements the asynchronous version of the loop found
        // in do_handshake. It gets itself called repeatedly until the
        // state resolves to a final handshake or an error occurs.
        //
        void operator() (error_code const& ec_)
        {
            error_code ec (ec_);

            do
            {
                if (m_socket->m_state == stateReady)
                {
                    // Always need shutdown if handshake successful.
                    m_socket->m_needsShutdown = true;
                    break;
                }

                switch (m_socket->m_state)
                {
                case stateHandshakeFinal:
                    {
                        // Have to set this beforehand even
                        // though we might get an error.
                        //
                        m_socket->m_state = stateReady;
                        m_socket->stream ().async_handshake (m_type,
                            SharedHandlerPtr (this));
                    }
                    return;

                case stateExpectPROXY:
                    {
                        if (m_proxy.getLogic ().finished ())
                        {
                            if (m_proxy.getLogic ().success ())
                            {
                                m_socket->setProxyInfo (m_proxy.getLogic ().getInfo ());

                                // Strip off the PROXY flag.
                                m_socket->m_flags = m_socket->m_flags.without (Flag::proxy);

                                // Update handshake state with the leftover bytes.
                                ec = m_socket->initHandshake (m_type, m_buffer.data ());
                                break;
                            }

                            // Didn't get the PROXY handshake we needed
                            ec = m_socket->handshake_error ();
                            break;
                        }

                        m_proxy.async_detect (m_socket->next_layer (),
                            m_buffer, SharedHandlerPtr (this));
                    }
                    return;

                case stateDetectSSL:
                    {
                        if (m_ssl.getLogic ().finished ())
                        {
                            // Was it SSL?
                            if (m_ssl.getLogic ().success ())
                            {
                                // Convert the ssl flag to ssl_required
                                m_socket->m_flags = m_socket->m_flags.with (
                                    Flag::ssl_required).without (Flag::ssl);
                            }
                            else
                            {
                                // Not SSL, strip the ssl flag
                                m_socket->m_flags = m_socket->m_flags.without (Flag::ssl);
                            }

                            // Update handshake state with the leftover bytes.
                            ec = m_socket->initHandshake (m_type, m_buffer.data ());
                            break;
                        }

                        m_ssl.async_detect (m_socket->next_layer (),
                            m_buffer, SharedHandlerPtr (this));
                    }
                    return;

                case stateNone:
                case stateReady:
                case stateHandshake:
                default:
                    fatal_error ("invalid state");
                    ec = m_socket->handshake_error ();
                }
            }
            while (! ec);

            bassert (ec || (m_socket->m_state == stateReady && m_socket->m_needsShutdown));

            // Call the original handler with the error code and end.
            m_socket->get_io_service ().wrap (
                BOOST_ASIO_MOVE_CAST (SharedHandlerPtr)(m_handler))
                    (ec);
        }

        bool is_continuation ()
        {
            return m_running || m_handler->is_continuation ();
        }

    private:
        SharedHandlerPtr m_handler;
        MultiSocketType <StreamSocket>* m_socket;
        handshake_type const m_type;
        boost::asio::basic_streambuf <Allocator> m_buffer;
        HandshakeDetectorType <next_layer_type, HandshakeDetectLogicPROXY> m_proxy;
        HandshakeDetectorType <next_layer_type, HandshakeDetectLogicSSL3> m_ssl;
        bool m_running;
    };

private:
    Flag m_flags;
    State m_state;
    ScopedPointer <Socket> m_stream;
    bool m_needsShutdown;
    StreamSocket m_next_layer;
    boost::asio::io_service::strand m_strand;
};

#endif
