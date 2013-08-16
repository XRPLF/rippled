//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MULTISOCKETTYPE_H_INCLUDED
#define RIPPLE_MULTISOCKETTYPE_H_INCLUDED

//------------------------------------------------------------------------------

template <typename Stream>
class ssl_detector
    : public boost::asio::ssl::stream_base
    , public boost::asio::socket_base
{
private:
    typedef typename boost::remove_reference <Stream>::type stream_type;

public:
    template <typename Arg>
    ssl_detector (Arg& arg)
        : m_next_layer (arg)
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

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec)
    {
        ec = boost::system::error_code ();

        m_buffer.commit (boost::asio::read (m_next_layer,
            m_buffer.prepare (m_detector.max_needed ()), ec));

        if (!ec)
        {
            bool const finished = m_detector.analyze (m_buffer.data ());

            if (finished)
            {
            }
        }

        return ec;
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake(handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    template <typename ConstBufferSequence>
    void handshake (handshake_type type, const ConstBufferSequence& buffers)
    {
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake(handshake_type type, const ConstBufferSequence& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
    }
#endif

private:
    Stream m_next_layer;
    boost::asio::streambuf m_buffer;
    boost::asio::buffered_read_stream <stream_type&> m_stream;
    HandshakeDetectorType <SSL3> m_detector;
};

//------------------------------------------------------------------------------

/** Template for producing instances of MultiSocket
*/
template <class StreamSocket>
class MultiSocketType
    : public MultiSocket
{
public:
    typedef typename boost::remove_reference <StreamSocket>::type next_layer_type;
    typedef typename boost::add_reference <next_layer_type>::type next_layer_type_ref;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
    typedef typename boost::asio::ssl::stream <next_layer_type_ref> ssl_stream_type;

    template <class Arg>
    explicit MultiSocketType (Arg& arg, int flags = 0, Options const& options = Options ())
        : m_options (options)
        , m_flags (cleaned_flags (flags))
        , m_next_layer (arg)
        , m_stream (new_current_stream (true))
        , m_strand (get_io_service ())
    {
    }

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

    boost::system::error_code cancel (boost::system::error_code& ec)
    {
        return lowest_layer ().cancel (ec);
    }

    boost::system::error_code shutdown (Socket::shutdown_type what, boost::system::error_code& ec)
    {
        return lowest_layer ().shutdown (what, ec);
    }

    boost::system::error_code close (boost::system::error_code& ec)
    {
        return lowest_layer ().close (ec);
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    std::size_t read_some (MutableBuffers const& buffers, boost::system::error_code& ec)
    {
        return stream ().read_some (buffers, ec);
    }

    std::size_t write_some (ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        return stream ().write_some (buffers, ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return stream ().async_read_some (buffers, m_strand.wrap (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
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
        return stream ().requires_handshake ();
    }

    boost::system::error_code handshake (Socket::handshake_type type, boost::system::error_code& ec)
    {
        if (! prepare_handshake (type, ec))
            return stream ().handshake (type, ec);

        return ec;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        boost::system::error_code ec;

        if (! prepare_handshake (type, ec))
            return stream ().async_handshake (type, BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));

        return get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec));
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    boost::system::error_code handshake (handshake_type type,
        ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        if (! prepare_handshake (type, ec))
            return stream ().handshake (type, buffers, ec);

        return ec;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        boost::system::error_code ec;

        if (! prepare_handshake (type, ec))
            return stream ().async_handshake (type, buffers,
                BOOST_ASIO_MOVE_CAST(TransferCall)(handler));

        return get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), ec, 0));
    }
#endif

    boost::system::error_code shutdown (boost::system::error_code& ec)
    {
        if (stream ().requires_handshake ())
            return stream ().shutdown (ec);

        return handshake_error (ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        if (stream ().requires_handshake ())
            return stream ().async_shutdown (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));

        boost::system::error_code ec;
        handshake_error (ec);
        return get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec));
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

    Socket* new_plain_stream ()
    {
        return new SocketWrapper <next_layer_type&> (m_next_layer);
    }

    Socket* new_ssl_stream ()
    {
        typedef typename boost::asio::ssl::stream <next_layer_type&> ssl_stream_type;

        return new SocketWrapper <ssl_stream_type> (
            m_next_layer, MultiSocket::getRippleTlsBoostContext ());
    }

    Socket* new_detect_ssl_stream ()
    {
        return new SocketWrapper <ssl_detector <next_layer_type&> > (m_next_layer);
        //return nullptr;
    }

    Socket* new_proxy_stream ()
    {
        return nullptr;
    }

    // Creates a Socket representing what the current stream should
    // be based on the flags and the state of the handshaking engine.
    //
    Socket* new_current_stream (bool createPlain)
    {
        Socket* result = nullptr;

        if (m_flags.set (Flag::client_role))
        {
            if (m_flags.set (Flag::proxy))
            {
                // unsupported function
                SocketBase::pure_virtual ();
            }

            if (m_flags.set (Flag::ssl))
            {
                result = new_ssl_stream ();
            }
            else
            {
                result = nullptr;
            }
        }
        else
        {
            if (m_flags.set (Flag::proxy))
            {
                result = new_proxy_stream ();
            }                
            else if (m_flags.set (Flag::ssl_required))
            {
                result = new_ssl_stream ();
            }
            else if (m_flags.set (Flag::ssl))
            {
                result = new_detect_ssl_stream ();
            }
            else
            {
                result = nullptr;
            }
        }

        if (createPlain)
        {
            if (result == nullptr)
                result = new_plain_stream ();
        }

        return result;
    }

    //--------------------------------------------------------------------------

    // Bottleneck to indicate a failed handshake.
    //
    boost::system::error_code handshake_error (boost::system::error_code& ec)
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
    boost::system::error_code prepare_handshake (handshake_type type,
        boost::system::error_code& ec)
    {
        ec = boost::system::error_code ();

        // If we constructed as 'peer' then become a client
        // or a server as needed according to the handshake type.
        //
        if (m_flags == Flag (Flag::peer))
            m_flags = (type == server) ?
                Flag (Flag::server_role) : Flag (Flag::client_role);

        // If we constructed with a specific role, the handshake
        // must match the role.
        //
        if ( (type == client && ! m_flags.set (Flag::client_role)) ||
             (type == server && ! m_flags.set (Flag::server_role)))
            return handshake_error (ec);

        // Figure out what type of new stream we need.
        // We pass false to avoid creating a redundant plain stream
        Socket* socket = new_current_stream (false);

        if (socket != nullptr)
            m_stream = socket;

        return ec;
    }

private:
    Options m_options; // DEPRECATED

    Flag m_flags;
    StreamSocket m_next_layer;
    ScopedPointer <Socket> m_stream;
    boost::asio::io_service::strand m_strand;
};

#endif
