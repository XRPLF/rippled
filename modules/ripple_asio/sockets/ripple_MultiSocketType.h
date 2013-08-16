//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MULTISOCKETTYPE_H_INCLUDED
#define RIPPLE_MULTISOCKETTYPE_H_INCLUDED

//------------------------------------------------------------------------------

// should really start using this
namespace detail
{
}

//------------------------------------------------------------------------------

class ssl_detector_base
{
protected:
    typedef boost::system::error_code error_code;

public:
    /** Called when the state is known.
        
        This could be called from any thread, most likely an io_service
        thread but don't rely on that.

        The Callback must be allocated via operator new.
    */
    struct Callback
    {
        virtual ~Callback () { }

        /** Called for synchronous ssl detection.

            Note that the storage for the buffers passed to the
            callback is owned by the detector class and becomes
            invalid when the detector class is destroyed, which is
            a common thing to do from inside your callback.

            @param ec A modifiable error code that becomes the return
                      value of handshake.
            @param buffers The bytes that were read in.
            @param is_ssl True if the sequence is an ssl handshake.
        */
        virtual void on_detect (error_code& ec,
            ConstBuffers const& buffers, bool is_ssl) = 0;

        virtual void on_async_detect (error_code const& ec,
            ConstBuffers const& buffers, ErrorCall const& origHandler,
                bool is_ssl) = 0;

    #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
        virtual void on_async_detect (error_code const& ec,
            ConstBuffers const& buffers, TransferCall const& origHandler,
                bool is_ssl) = 0;
    #endif

    };
};

/** A stream that can detect an SSL handshake.
*/
template <typename Stream>
class ssl_detector
    : public ssl_detector_base
    , public boost::asio::ssl::stream_base
    , public boost::asio::socket_base
{
private:
    typedef ssl_detector <Stream> this_type;
    typedef boost::asio::streambuf buffer_type;
    typedef typename boost::remove_reference <Stream>::type stream_type;

public:
    /** This takes ownership of the callback.
        The callback must be allocated with operator new.
    */
    template <typename Arg>
    ssl_detector (Callback* callback, Arg& arg)
        : m_callback (callback)
        , m_next_layer (arg)
        , m_stream (m_next_layer)
    {
    }

    // basic_io_object

    boost::asio::io_service& get_io_service ()
    {
        return m_next_layer.get_io_service ();
    }

    // basic_socket

    typedef typename stream_type::protocol_type protocol_type;
    typedef typename stream_type::lowest_layer_type lowest_layer_type;

    lowest_layer_type& lowest_layer ()
    {
        return m_next_layer.lowest_layer ();
    }

    lowest_layer_type const& lowest_layer () const
    {
        return m_next_layer.lowest_layer ();
    }

    // ssl::stream

    error_code handshake (handshake_type type, error_code& ec)
    {
        return do_handshake (type, ec, ConstBuffers ());
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ErrorCall, void (error_code)> init(
                BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
        // init.handler is copied
        m_origHandler = ErrorCall (HandshakeHandler(init.handler));
        bassert (m_origBufferedHandler.isNull ());
        async_do_handshake (type, ConstBuffers ());
        return init.result.get();
#else
        m_origHandler = ErrorCall (handler);
        bassert (m_origBufferedHandler.isNull ());
        async_do_handshake (type, ConstBuffers ());
#endif
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence>
    error_code handshake (handshake_type type,
        const ConstBufferSequence& buffers, error_code& ec)
    {
        return do_handshake (type, ec, ConstBuffers (buffers));;
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (error_code, std::size_t))
    async_handshake(handshake_type type, const ConstBufferSequence& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            BufferedHandshakeHandler, void (error_code, std::size_t)> init(
                BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
        // init.handler is copied
        m_origBufferedHandler = TransferCall (BufferedHandshakeHandler(init.handler));
        bassert (m_origHandler.isNull ());
        async_do_handshake (type, ConstBuffers (buffers));
        return init.result.get();
#else
        m_origBufferedHandler = TransferCall (handler);
        bassert (m_origHandler.isNull ());
        async_do_handshake (type, ConstBuffers (buffers));
#endif
    }
#endif

    //--------------------------------------------------------------------------

    error_code do_handshake (handshake_type, error_code& ec, ConstBuffers const& buffers)
    {
        ec = error_code ();

        // Transfer caller data to our buffer.
        m_buffer.commit (boost::asio::buffer_copy (m_buffer.prepare (
            boost::asio::buffer_size (buffers)), buffers));

        do
        {
            std::size_t const available = m_buffer.size ();
            std::size_t const needed = m_detector.max_needed ();
            if (available < needed)
            {
                buffer_type::mutable_buffers_type buffers (
                    m_buffer.prepare (needed - available));
                m_buffer.commit (m_next_layer.read_some (buffers, ec));
            }

            if (! ec)
            {
                m_detector.analyze (m_buffer.data ());

                if (m_detector.finished ())
                {
                    m_callback->on_detect (ec,
                        ConstBuffers (m_buffer.data ()),
                            m_detector.success ());
                    break;
                }

                // If this fails it means we will never finish
                check_postcondition (available < needed);
            }
        }
        while (! ec);

        return ec;
    }

    //--------------------------------------------------------------------------

    void async_do_handshake (handshake_type type, ConstBuffers const& buffers)
    {
        // Transfer caller data to our buffer.
        std::size_t const bytes_transferred (boost::asio::buffer_copy (
            m_buffer.prepare (boost::asio::buffer_size (buffers)), buffers));

        // bootstrap the asynchronous loop
        on_async_read_some (error_code (), bytes_transferred);
    }

    // asynchronous version of the synchronous loop found in handshake ()
    //
    void on_async_read_some (error_code const& ec, std::size_t bytes_transferred)
    {
        if (! ec)
        {
            m_buffer.commit (bytes_transferred);

            std::size_t const available = m_buffer.size ();
            std::size_t const needed = m_detector.max_needed ();

            m_detector.analyze (m_buffer.data ());

            if (m_detector.finished ())
            {
            #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
                if (! m_origBufferedHandler.isNull ())
                {
                    bassert (m_origHandler.isNull ());
                    // continuation?
                    m_callback->on_async_detect (ec, ConstBuffers (m_buffer.data ()),
                        m_origBufferedHandler, m_detector.success ());
                    return;
                }
            #endif

                bassert (! m_origHandler.isNull ())
                // continuation?
                m_callback->on_async_detect (ec,
                    ConstBuffers (m_buffer.data ()), m_origHandler,
                        m_detector.success ());
                return;
            }

            // If this fails it means we will never finish
            check_postcondition (available < needed);

            buffer_type::mutable_buffers_type buffers (m_buffer.prepare (
                needed - available));

            // need a continuation hook here?
            m_next_layer.async_read_some (buffers, boost::bind (
                &this_type::on_async_read_some, this, boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

            return;
        }

    #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
        if (! m_origBufferedHandler.isNull ())
        {
            bassert (m_origHandler.isNull ());
            // continuation?
            m_callback->on_async_detect (ec, ConstBuffers (m_buffer.data ()),
                m_origBufferedHandler, false);
            return;
        }
    #endif

        bassert (! m_origHandler.isNull ())
        // continuation?
        m_callback->on_async_detect (ec,
            ConstBuffers (m_buffer.data ()), m_origHandler,
                false);
    }

private:
    ScopedPointer <Callback> m_callback;
    Stream m_next_layer;
    buffer_type m_buffer;
    boost::asio::buffered_read_stream <stream_type&> m_stream;
    HandshakeDetectorType <SSL3> m_detector;
    ErrorCall m_origHandler;
    TransferCall m_origBufferedHandler;
};

//------------------------------------------------------------------------------

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

    // These types are used internaly
    //

    // The type of stream that the ssl_detector uses
    typedef next_layer_type& ssl_detector_stream_type;

    template <class Arg>
    explicit MultiSocketType (Arg& arg, int flags = 0)
        : m_flags (cleaned_flags (flags))
        , m_next_layer (arg)
        , m_stream (new_stream ())
        , m_strand (get_io_service ())
    {
    }

protected:
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

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return stream ().async_read_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return stream ().async_write_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    bool requires_handshake ()
    {
        if (m_stream != nullptr)
            return stream ().requires_handshake ();
        return true;
    }

    error_code handshake (Socket::handshake_type type, error_code& ec)
    {
        if (set_handshake_stream (type, ec))
            return stream ().handshake (type, ec);
        return ec;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        error_code ec;
        if (set_handshake_stream (type, ec))
            return stream ().async_handshake (type,
                BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
        return get_io_service ().post (CompletionCall (handler, ec));
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec)
    {
        if (set_handshake_stream (type, ec))
            return stream ().handshake (type, buffers, ec);
        return ec;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        error_code ec;
        if (set_handshake_stream (type, ec))
            return stream ().async_handshake (type, buffers,
                BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
        return get_io_service ().post (CompletionCall (handler, ec, 0));
    }
#endif

    error_code shutdown (error_code& ec)
    {
        bassert (requires_handshake ());

        if (stream ().requires_handshake ())
            return stream ().shutdown (ec);

        return handshake_error (ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        bassert (requires_handshake ());

        if (stream ().requires_handshake ())
            return stream ().async_shutdown (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));

        error_code ec;
        handshake_error (ec);
        return get_io_service ().post (CompletionCall (handler, ec));
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
        using namespace boost;
        if (asio::buffer_size (buffers) > 0)
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
        using namespace boost;
        typedef typename asio::ssl::stream <next_layer_type&> this_type;
        return new SocketWrapper <this_type> (
            m_next_layer, MultiSocket::getRippleTlsBoostContext ());
    }

    // Creates an ssl stream, but front-load it with some bytes.
    // A copy of the buffers is made.
    //
    template <typename ConstBufferSequence>
    Socket* new_ssl_stream (ConstBufferSequence const& buffers)
    {
        using namespace boost;
        if (asio::buffer_size (buffers) > 0)
        {
            bassert (asio::buffer_size (buffers) > 0);
            typedef PrefilledReadStream <next_layer_type&> next_type;
            typedef asio::ssl::stream <next_type> this_type;
            SocketWrapper <this_type>* const socket = new SocketWrapper <this_type> (
                m_next_layer, MultiSocket::getRippleTlsBoostContext ());
            socket->this_layer().next_layer().fill (buffers);
            return socket;
        }
        return new_ssl_stream ();
    }

    Socket* new_detect_ssl_stream ()
    {
        ssl_detector_callback* callback = new ssl_detector_callback (this);
        typedef ssl_detector <ssl_detector_stream_type> this_type;
        return new SocketWrapper <this_type> (callback, m_next_layer);
    }

    Socket* new_proxy_stream ()
    {
        return nullptr;
    }

    //--------------------------------------------------------------------------

    // Returns a stream appropriate for the current settings. If the
    // return value is nullptr, it means that the stream cannot be determined
    // until handshake or async_handshake is called. At that point, it will
    // be necessary to use new_handshake_stream
    //
    Socket* new_stream ()
    {
        Socket* socket = nullptr;

        if (is_client ())
        {
            if (m_flags.set (Flag::proxy))
            {
                // sending PROXY unimplemented for now
                SocketBase::pure_virtual ();
                socket = nullptr;
            }
            else if (! m_flags.any_set (
                Flag::ssl))
            {
                socket = new_plain_stream ();
            }
            else
            {
                // A call to handshake or async_handshake is
                // required to determine the type of stream to create.
                //
                socket = nullptr;
            }
        }
        else if (is_server ())
        {
            // As long as no flags indicate a handshake may be
            // required, we can start out with a plain stream.
            //
            if (! m_flags.any_set (
                Flag::ssl | Flag::ssl_required | Flag::proxy))
            {
                socket = new_plain_stream ();
            }
            else
            {
                // A call to handshake or async_handshake is
                // required to determine the type of stream to create.
                //
                socket = nullptr;
            }
        }
        else
        {
            // peer, plain stream, no handshake
            socket = new_plain_stream ();
        }

        return socket;
    }

    //--------------------------------------------------------------------------

    // Bottleneck to indicate a failed handshake.
    //
    error_code handshake_error (error_code& ec)
    {
        // VFALCO TODO maybe use a ripple error category?
        // set this to something custom that we can recognize later?
        //
        return ec = boost::asio::error::invalid_argument;
    }

    // Sanity checks and cleans up our flags based on the desired handshake
    // type and correctly sets the current stream based on the settings.
    // If the requested handshake type is incompatible with the socket flags
    // set on construction, an error is returned.
    //
    // Returns true if the stream was set
    //
    bool set_handshake_stream (handshake_type type, error_code& ec)
    {
        // We better require a handshake if this is getting called
        bassert (requires_handshake ());

        // If a stream already exists, some logic went wrong.
        bassert (m_stream == nullptr);

        ec = error_code ();

        // If we constructed as a peer role then return
        // an error since there is no handshake required.
        //
        if (m_flags == Flag (Flag::peer))
        {
            handshake_error (ec);
            return false;
        }

        // If we constructed in a server or client role and
        // the handshake type doesn't match, return an error.
        //
        if ( (type == client && ! m_flags.set (Flag::client_role)) ||
             (type == server && ! m_flags.set (Flag::server_role)))
        {
            handshake_error (ec);
            return false;
        }

        Socket* socket = nullptr;

        if (m_flags.set (Flag::client_role))
        {
            // should have filtered this out already
            bassert (! m_flags.set (Flag::proxy));

            if (m_flags.set (Flag::ssl))
            {
                socket = new_ssl_stream ();
            }
            else
            {
                // bad
            }
        }
        else
        {
            if (m_flags.set (Flag::proxy))
            {
                socket = new_proxy_stream ();
            }                
            else if (m_flags.set (Flag::ssl_required))
            {
                socket = new_ssl_stream ();
            }
            else if (m_flags.set (Flag::ssl))
            {
                socket = new_detect_ssl_stream ();
            }
            else
            {
                // bad
            }
        }

        check_postcondition (socket != nullptr);

        m_stream = socket;

        return true;
    }

    //--------------------------------------------------------------------------

    void on_detect_ssl (error_code& ec, ConstBuffers const& buffers, bool is_ssl)
    {
        bassert (is_server ());

        if (! ec)
        {
            if (is_ssl)
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
                stream ().handshake (Socket::server, ec);
                return;
            }

            m_stream = new_plain_stream (buffers);
        }
    }

    // origHandler cannot be a reference since it gets move-copied
    void on_async_detect_ssl (error_code const& ec,
        ConstBuffers const& buffers, ErrorCall origHandler, bool is_ssl)
    {
        if (! ec)
        {
            if (is_ssl)
            {
                m_stream = new_ssl_stream (buffers);
                // Is this a continuation?
                stream ().async_handshake (Socket::server, origHandler);
                return;
            }

            // Note, this assignment will destroy the
            // detector, and our allocated callback along with it.
            m_stream = new_plain_stream (buffers);
        }

        // Is this a continuation?
        get_io_service ().post (CompletionCall (
            origHandler, ec));
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // origHandler cannot be a reference since it gets move-copied
    void on_async_detect_ssl (error_code const& ec,
        ConstBuffers const& buffers, TransferCall origHandler, bool is_ssl)
    {
        std::size_t bytes_transferred = 0;

        if (! ec)
        {
            if (is_ssl)
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
        get_io_service ().post (CompletionCall (
            origHandler, ec, bytes_transferred));
    }
#endif

    //--------------------------------------------------------------------------
    //
    // ssl_detector calback
    //
    class ssl_detector_callback : public ssl_detector_base::Callback
    {
    public:
        explicit ssl_detector_callback (MultiSocketType <StreamSocket>* owner)
            : m_owner (owner)
        {
        }

        void on_detect (error_code& ec,
            ConstBuffers const& buffers, bool is_ssl)
        {
            m_owner->on_detect_ssl (ec, buffers, is_ssl);
        }

        void on_async_detect (error_code const& ec,
            ConstBuffers const& buffers, ErrorCall const& origHandler,
                bool is_ssl)
        {
            m_owner->on_async_detect_ssl (ec, buffers, origHandler, is_ssl);
        }

    #if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
        void on_async_detect (error_code const& ec,
            ConstBuffers const& buffers, TransferCall const& origHandler,
                bool is_ssl)
        {
            m_owner->on_async_detect_ssl (ec, buffers, origHandler, is_ssl);
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

private:
    Flag m_flags;
    StreamSocket m_next_layer;
    ScopedPointer <Socket> m_stream;
    boost::asio::io_service::strand m_strand;
};

#endif
