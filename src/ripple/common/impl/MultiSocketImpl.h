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

#ifndef RIPPLE_COMMON_MULTISOCKETIMPL_H_INCLUDED
#define RIPPLE_COMMON_MULTISOCKETIMPL_H_INCLUDED

#include <ripple/common/MultiSocket.h>
#include <beast/boost/get_pointer.h>
#include <beast/asio/wrap_handler.h>
#include <beast/asio/placeholders.h>
#include <beast/cxx14/type_traits.h>
#include <boost/asio/ssl/stream.hpp>
#include <cassert>

#ifndef RIPPLE_MULTISOCKET_NO_ALLOCATOR
#define RIPPLE_MULTISOCKET_NO_ALLOCATOR 0
#endif

namespace ripple {

/** Template for producing instances of MultiSocket */
template <class StreamSocket>
class MultiSocketImpl
    : public MultiSocket
{
private:
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

    Flag m_flags;
    State m_state;
    boost::asio::ssl::context& m_ssl_context;
    int m_verify_mode;
    std::unique_ptr <
        beast::asio::abstract_socket> m_stream;
    std::unique_ptr <
        beast::asio::abstract_socket
            > m_ssl_stream; // the ssl portion of our stream if it exists
    bool m_needsShutdown;
    StreamSocket m_next_layer;
    ProxyInfo m_proxyInfo;
    bool m_proxyInfoSet;
    SSL* m_native_ssl_handle;
    Flag m_origFlags;

protected:
    typedef boost::system::error_code error_code;

public:
    typedef std::remove_reference_t <StreamSocket> next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

    template <class... Args>
    MultiSocketImpl (boost::asio::ssl::context& ssl_context, int flags,
        Args&&... args)
        : m_flags (flags)
        , m_state (stateNone)
        , m_ssl_context (ssl_context)
        , m_verify_mode (0)
        , m_stream (nullptr)
        , m_needsShutdown (false)
        , m_next_layer (std::forward <Args> (args)...)
        , m_proxyInfoSet (false)
        , m_native_ssl_handle (nullptr)
        , m_origFlags (cleaned_flags (flags))
    {
        // See if our flags allow us to go directly
        // into the ready state with an active stream.
        initState ();
    }

protected:
    //--------------------------------------------------------------------------
    //
    // MultiSocket
    //
    //--------------------------------------------------------------------------

    Flag
    getFlags () override
    {
        return m_origFlags;
    }

    beast::IP::Endpoint
    local_endpoint() override
    {
        return beast::IPAddressConversion::from_asio (
            m_next_layer.local_endpoint());
    }

    beast::IP::Endpoint
    remote_endpoint() override
    {
        if (m_proxyInfoSet)
        {
            if (m_proxyInfo.protocol == "TCP4")
            {
                return beast::IP::Endpoint (
                    beast::IP::AddressV4 (
                        m_proxyInfo.destAddress.value [0],
                        m_proxyInfo.destAddress.value [1],
                        m_proxyInfo.destAddress.value [2],
                        m_proxyInfo.destAddress.value [3])
                    , m_proxyInfo.destPort);
            }

            // VFALCO TODO IPv6 support
            bassertfalse;
            return beast::IP::Endpoint();
        }

        try 
        {
            return beast::IPAddressConversion::from_asio (
                m_next_layer.remote_endpoint());
        }
        catch (...)
        {
            return beast::IP::Endpoint ();
        }
    }

    ProxyInfo
    getProxyInfo () override
    {
        return m_proxyInfo;
    }

    SSL*
    ssl_handle () override
    {
        return m_native_ssl_handle;
    }

    //--------------------------------------------------------------------------
    //
    // MultiSocketImpl
    //
    //--------------------------------------------------------------------------

    /** The current stream we are passing everything through.
        This object gets dynamically created and replaced with other
        objects as we process the various flags for handshaking.
    */
    beast::asio::abstract_socket&
    stream () const noexcept
    {
        bassert (m_stream != nullptr);
        return *m_stream;
    }

    //--------------------------------------------------------------------------
    //
    // abstract_socket
    //
    //--------------------------------------------------------------------------

    // We will return the underlying StreamSocket
    void* this_layer_ptr (char const* type_name) const override
    {
        char const* const name (typeid (next_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (
                static_cast <void const*> (&m_next_layer));
        return nullptr;
    }

    // What would we return from here?
    bool native_handle (char const*, void*) override
    {
        pure_virtual_called ();
        return false;
    }

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //
    //--------------------------------------------------------------------------

    boost::asio::io_service& get_io_service () override
    {
        return m_next_layer.get_io_service ();
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //
    //--------------------------------------------------------------------------

    /** Always the lowest_layer of the underlying StreamSocket. */
    void* lowest_layer_ptr (char const* type_name) const override
    {
        char const* const name (typeid (lowest_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (
                static_cast <void const*>(&m_next_layer.lowest_layer ()));
        return nullptr;
    }

    error_code cancel (error_code& ec) override
    {
        return m_next_layer.lowest_layer ().cancel (ec);
    }

    error_code shutdown (abstract_socket::shutdown_type what,
        error_code& ec) override
    {
        return m_next_layer.lowest_layer ().shutdown (what, ec);
    }

    error_code close (error_code& ec) override
    {
        return m_next_layer.lowest_layer ().close (ec);
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //
    //--------------------------------------------------------------------------

    std::size_t read_some (beast::asio::mutable_buffers buffers,
        error_code& ec) override
    {
        return stream ().read_some (buffers, ec);
    }

    std::size_t write_some (beast::asio::const_buffers buffers,
        error_code& ec) override
    {
        return stream ().write_some (buffers, ec);
    }

    void async_read_some (beast::asio::mutable_buffers buffers,
        transfer_handler handler) override
    {
        stream ().async_read_some (buffers, handler);
    }

    void async_write_some (beast::asio::const_buffers buffers,
        transfer_handler handler) override
    {
        stream ().async_write_some (buffers, handler);
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //
    //--------------------------------------------------------------------------

    /** Retrieve the next_layer.
        The next_layer_type is always the StreamSocket, regardless of what
        we have put around it in terms of SSL or buffering.
    */
    void* next_layer_ptr (char const* type_name) const override
    {
        char const* const name (typeid (next_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (
                static_cast <void const*> (&m_next_layer));
        return nullptr;

    }

    /** Determine if the caller needs to call a handshaking function.
        This is also used to determine if the handshaking shutdown()
        has to be called.
    */
    bool needs_handshake () override
    {
        return m_state == stateHandshake || 
               m_state == stateHandshakeFinal ||
               m_needsShutdown;
    }

    void set_verify_mode (int verify_mode) override
    {
        if (m_ssl_stream != nullptr)
            m_ssl_stream->set_verify_mode (verify_mode);
        else
            m_verify_mode = verify_mode;
    }

    error_code handshake (abstract_socket::handshake_type type,
        error_code& ec) override
    {
        return handshake (type, beast::asio::const_buffers (), ec);
    }

    error_code handshake (handshake_type type,
        beast::asio::const_buffers buffers, error_code& ec) override
    {
        return do_handshake (type, buffers, ec);
    }

    //--------------------------------------------------------------------------

    void
    async_handshake (handshake_type type, error_handler handler) override
    {
        typedef AsyncOp <next_layer_type> Op;
        auto const op (std::make_shared <Op> (
            std::ref (*this), std::ref (m_next_layer), type,
                beast::asio::const_buffers(), handler));
        op->start();
    }

#if 0
    void async_handshake (handshake_type type,
        beast::asio::const_buffers buffers,
            transfer_handler handler) override;
#endif

    error_code shutdown (error_code& ec) override
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

    void async_shutdown (error_handler handler) override
    {
        error_code ec;

        if (m_needsShutdown)
        {
            if (stream ().needs_handshake ())
            {
                stream ().async_shutdown (handler);
                return;
            }
        }
        else
        {
            // Our interface didn't require a shutdown but someone called
            // it anyone so generate an error code.
            ec = handshake_error ();
        }

        get_io_service ().post (bind_handler (handler, ec));
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
        assert (! flags.set (
            Flag::client_role | Flag::server_role));

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

        return flags;
    }

    bool is_client () const noexcept
    {
        return m_flags.set (Flag::client_role);
    }

    bool is_server () const noexcept
    {
        return m_flags.set (Flag::server_role);
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
                m_stream.reset (new_plain_stream ());
            }
            else if (m_flags.set (Flag::ssl))
            {
                m_state = stateHandshakeFinal;
                m_stream.reset (nullptr);
            }
            else
            {
                m_state = stateReady;
                m_stream.reset (new_plain_stream ());
            }
        }
        else if (is_server ())
        {
            if (m_flags.set (Flag::proxy))
            {
                // We expect a PROXY handshake.
                // Create the plain stream at handshake time.
                m_state = stateHandshake;
                m_stream.reset (nullptr);
            }
            else if (m_flags.set (Flag::ssl_required))
            {
                // We require an SSL handshake.
                // Create the stream at handshake time.
                m_state = stateHandshakeFinal;
                m_stream.reset (nullptr);
            }
            else if (m_flags.set (Flag::ssl))
            {
                // We will use the SSL detector at handshake
                // time decide which type of stream to create.
                m_state = stateHandshake;
                m_stream.reset (nullptr);
            }
            else
            {
                // No handshaking required.
                m_state = stateReady;
                m_stream.reset (new_plain_stream ());
            }
        }
        else
        {
            // We will determine client/server mode
            // at the time handshake is called.

            bassert (! m_flags.set (Flag::proxy));
            m_flags = m_flags.without (Flag::proxy);

            if (m_flags.any_set (Flag::ssl | Flag::ssl_required))
            {
                // We will decide stream type at handshake time
                m_state = stateHandshake;
                m_stream.reset (nullptr);
            }
            else
            {
                m_state = stateReady;
                m_stream.reset (new_plain_stream ());
            }
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
        return initHandshake (type, beast::asio::const_buffers ());
    }

    // Updates the state based on the now-known handshake type. The
    // buffers parameter contains bytes that have already been received.
    // This can come from the results of SSL detection, or from the buffered
    // handshake API calls added in Boost v1.54.0.
    //
    error_code initHandshake (handshake_type type,
        beast::asio::const_buffers const& buffers)
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

        // Set flags based on handshake if necessary
        if (! m_flags.any_set (Flag::client_role | Flag::server_role))
        {
            switch (type)
            {
            case client:
                m_flags = m_flags.with (Flag::client_role);
                break;

            case server:
                m_flags = m_flags.with (Flag::server_role);
                break;

            default:
                fatal_error ("invalid tyoe");
                break;
            }

            m_flags = cleaned_flags (m_flags);
        }

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
            m_stream.reset (new_ssl_stream (buffers));
        }
        else
        {
            bassert (is_server ());

            if (m_flags.set (Flag::proxy))
            {
                // We will expect and consume a PROXY handshake,
                // then come back here with the flag cleared.
                m_state = stateExpectPROXY;
                m_stream.reset (new_plain_stream ());
            }
            else if (m_flags.set (Flag::ssl_required))
            {
                // WE will perform a required final SSL handshake.
                m_state = stateHandshakeFinal;
                m_stream.reset (new_ssl_stream (buffers));
            }
            else if (m_flags.set (Flag::ssl))
            {
                // We will use the SSL detector to update
                // our flags and come back through here.
                m_state = stateDetectSSL;
                m_stream.reset (nullptr);
            }
            else
            {
                // Done with auto-detect
                m_state = stateReady;
                m_stream.reset (new_plain_stream (buffers));
            }
        }

        return error_code ();
    }

    //--------------------------------------------------------------------------

    template <typename Stream>
    void set_ssl_stream (boost::asio::ssl::stream <Stream>& ssl_stream)
    {
        m_ssl_stream.reset (new beast::asio::socket_wrapper <
            boost::asio::ssl::stream <Stream>&> (ssl_stream));
        m_ssl_stream->set_verify_mode (m_verify_mode);
        m_native_ssl_handle = ssl_stream.native_handle ();
    }

    //--------------------------------------------------------------------------

    // VFALCO TODO These should return std::unique_ptr

    // Create a plain stream that just wraps the next layer.
    //
    beast::asio::abstract_socket*
    new_plain_stream ()
    {
        typedef beast::asio::socket_wrapper <next_layer_type&> Wrapper;
        Wrapper* const socket (new Wrapper (m_next_layer));
        return socket;
    }

    // Create a plain stream but front-load it with some bytes.
    // A copy of the buffers is made.
    //
    template <typename ConstBufferSequence>
    beast::asio::abstract_socket*
    new_plain_stream (ConstBufferSequence const& buffers)
    {
        if (boost::asio::buffer_size (buffers) > 0)
        {
            typedef beast::asio::socket_wrapper <
                beast::asio::PrefilledReadStream <next_layer_type&> > Wrapper;
            Wrapper* const socket (new Wrapper (m_next_layer));
            socket->this_layer ().fill (buffers);
            return socket;
        }
        return new_plain_stream ();
    }

    // Creates an ssl stream
    //
    beast::asio::abstract_socket*
    new_ssl_stream ()
    {
        typedef typename boost::asio::ssl::stream <next_layer_type&> SslStream;
        typedef beast::asio::socket_wrapper <SslStream> Wrapper;
        Wrapper* const socket = new Wrapper (m_next_layer, m_ssl_context);
        set_ssl_stream (socket->this_layer ());
        return socket;
    }

    // Creates an ssl stream, but front-load it with some bytes.
    // A copy of the buffers is made.
    //
    template <typename ConstBufferSequence>
    beast::asio::abstract_socket*
    new_ssl_stream (ConstBufferSequence const& buffers)
    {
        if (boost::asio::buffer_size (buffers) > 0)
        {
            typedef boost::asio::ssl::stream <
                beast::asio::PrefilledReadStream <next_layer_type&> > SslStream;
            typedef beast::asio::socket_wrapper <SslStream> Wrapper;
            Wrapper* const socket = new Wrapper (m_next_layer, m_ssl_context);
            socket->this_layer ().next_layer().fill (buffers);
            set_ssl_stream (socket->this_layer ());
            return socket;
        }
        return new_ssl_stream ();
    }

    //--------------------------------------------------------------------------
    //
    // Synchronous handshake operation
    //

    error_code
    do_handshake (handshake_type type,
        beast::asio::const_buffers const& buffers, error_code& ec)
    {
        ec = initHandshake (type);
        if (ec)
            return ec;

        // How can we be ready if a handshake is needed?
        bassert (m_state != stateReady);

        // Prepare our rolling detect buffer with any input
        boost::asio::streambuf buffer;
        buffer.commit (boost::asio::buffer_copy (
            buffer.prepare (boost::asio::buffer_size (buffers)),
                buffers));

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
                    typedef beast::asio::HandshakeDetectorType <next_layer_type,
                        beast::asio::HandshakeDetectLogicPROXY> op_type;

                    op_type op;

                    ec = op.detect (m_next_layer, buffer);

                    if (! ec)
                    {
                        bassert (op.getLogic ().finished ());

                        if (op.getLogic ().success ())
                        {
                            m_proxyInfo = op.getLogic ().getInfo ();
                            m_proxyInfoSet = true;

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
                    typedef beast::asio::HandshakeDetectorType <next_layer_type,
                        beast::asio::HandshakeDetectLogicSSL3> op_type;

                    op_type op;

                    ec = op.detect (m_next_layer, buffer);

                    if (! ec)
                    {
                        bassert (op.getLogic ().finished ());

                        // Was it SSL?
                        if (op.getLogic ().success ())
                        {
                            // Convert the ssl flag to ssl_required
                            m_flags = m_flags.with (
                                Flag::ssl_required).without (Flag::ssl);
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
    // Composed async handshake operator
    //

    template <typename Stream>
    class AsyncOp
        : public std::enable_shared_from_this <AsyncOp <Stream>>
    {
    public:
    #if RIPPLE_MULTISOCKET_NO_ALLOCATOR
        typedef std::allocator <char> Allocator;
    #else
        typedef beast::asio::shared_handler_allocator <char> Allocator;
    #endif

        error_handler m_handler;
        MultiSocketImpl <StreamSocket>& m_owner;
        Stream& m_stream;
        handshake_type const m_type;
        boost::asio::basic_streambuf <Allocator> m_buffer;
        beast::asio::HandshakeDetectorType <next_layer_type,
            beast::asio::HandshakeDetectLogicPROXY> m_proxy;
        beast::asio::HandshakeDetectorType <next_layer_type,
            beast::asio::HandshakeDetectLogicSSL3> m_ssl;
        bool m_first_time;

        AsyncOp (MultiSocketImpl <StreamSocket>& owner, Stream& stream,
            handshake_type type, beast::asio::const_buffers const& buffers,
                error_handler const& handler)
            : m_handler (handler)
            , m_owner (owner)
            , m_stream (stream)
            , m_type (type)
        #if RIPPLE_MULTISOCKET_NO_ALLOCATOR
            , m_buffer (std::numeric_limits <std::size_t>::max())
        #else
            , m_buffer (std::numeric_limits <std::size_t>::max(), handler)
        #endif
            , m_first_time (true)
        {
            // Prepare our rolling detect buffer with any input
            //
            // NOTE We have to do this in reverse order from the synchronous
            // version because the callers buffers won't be in scope inside
            // our call.
            //
            m_buffer.commit (boost::asio::buffer_copy (
                m_buffer.prepare (boost::asio::buffer_size (buffers)),
                    buffers));
        }

        // Set breakpoint to prove it gets destroyed
        ~AsyncOp ()
        {
        }

        // Start the composed asynchronous operation
        //
        void start ()
        {
            error_code ec (m_owner.initHandshake (m_type));

            if (! ec)
            {
                if (m_owner.m_state != stateReady)
                {
                    async_handshake (ec);
                    return;
                }

                // Always need shutdown if handshake successful.
                m_owner.m_needsShutdown = true;
            }

            // Post to the io_service, otherwise we would allow the
            // handler to be invoked from within an initiating function.
            // We can't use io_service::wrap because that calls dispatch
            // instead of post.
            //
            m_owner.get_io_service().post (beast::asio::wrap_handler (
                std::bind (&AsyncOp::on_final, this->shared_from_this(),
                    ec), m_handler));
        }

    private:
        void on_complete (error_code ec)
        {
            m_first_time = false;
            async_handshake(ec);
        }

        void on_final (error_code ec)
        {
            m_handler (ec);
        }

        // This implements the asynchronous version of the loop found
        // in do_handshake. It gets itself called repeatedly until the
        // state resolves to a final handshake or an error occurs.
        //
        void async_handshake (error_code ec)
        {
            while (!ec)
            {
                if (m_owner.m_state == stateReady)
                {
                    // Always need shutdown if handshake successful.
                    m_owner.m_needsShutdown = true;
                    break;
                }

                switch (m_owner.m_state)
                {
                case stateHandshakeFinal:
                    {
                        // Have to set this beforehand even
                        // though we might get an error.
                        //
                        m_owner.m_state = stateReady;
                        m_owner.stream ().async_handshake (m_type,
                            beast::asio::wrap_handler (std::bind (
                                &AsyncOp::on_complete, this->shared_from_this(),
                                    beast::asio::placeholders::error),
                                        m_handler, m_first_time));
                    }
                    return;

                case stateExpectPROXY:
                    {
                        if (m_proxy.getLogic ().finished ())
                        {
                            if (m_proxy.getLogic ().success ())
                            {
                                m_owner.m_proxyInfo =
                                    m_proxy.getLogic ().getInfo ();
                                m_owner.m_proxyInfoSet = true;

                                // Strip off the PROXY flag.
                                m_owner.m_flags = m_owner.m_flags.without (
                                    Flag::proxy);

                                // Update handshake state with the
                                // leftover bytes.
                                ec = m_owner.initHandshake (m_type,
                                    m_buffer.data ());
                                break;
                            }

                            // Didn't get the PROXY handshake we needed
                            ec = m_owner.handshake_error ();
                            break;
                        }

                        m_proxy.async_detect (m_stream, m_buffer,
                            beast::asio::wrap_handler (std::bind (
                                &AsyncOp::on_complete, this->shared_from_this(),
                                    beast::asio::placeholders::error),
                                        m_handler, m_first_time));
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
                                m_owner.m_flags = m_owner.m_flags.with (
                                    Flag::ssl_required).without (Flag::ssl);
                            }
                            else
                            {
                                // Not SSL, strip the ssl flag
                                m_owner.m_flags = m_owner.m_flags.without (
                                    Flag::ssl);
                            }

                            // Update handshake state with the leftover bytes.
                            ec = m_owner.initHandshake (m_type,
                                m_buffer.data ());
                            break;
                        }

                        m_ssl.async_detect (m_stream, m_buffer,
                            beast::asio::wrap_handler (std::bind (
                                &AsyncOp::on_complete, this->shared_from_this(),
                                    beast::asio::placeholders::error),
                                        m_handler, m_first_time));
                    }
                    return;

                case stateNone:
                case stateReady:
                case stateHandshake:
                default:
                    fatal_error ("invalid state");
                    ec = m_owner.handshake_error ();
                }
            }

            bassert (ec || (m_owner.m_state == stateReady &&
                m_owner.m_needsShutdown));

            on_final (ec);
        }
    };
};

}

#endif
