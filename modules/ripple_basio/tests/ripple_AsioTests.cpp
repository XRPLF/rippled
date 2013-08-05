//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef BOOST_ASIO_INITFN_RESULT_TYPE
#define BOOST_ASIO_INITFN_RESULT_TYPE(expr,val) void
#endif

/** Provides abstract interface for parts of boost::asio */
namespace Asio
{

using namespace boost;

//------------------------------------------------------------------------------

/** A high level socket abstraction.

    This combines the capabilities of multiple socket interfaces such
    as listening, connecting, streaming, and handshaking. It brings
    everything together into a single abstract interface.

    When member functions are called and the underlying implementation does
    not support the operation, a fatal error is generated.

    Must satisfy these requirements:
    
        DefaultConstructible, MoveConstructible, CopyConstructible,
        MoveAssignable, CopyAssignable, Destructible

    Meets the requirements of these boost concepts:

        SyncReadStream, SyncWriteStream, AsyncReadStream, AsyncWriteStream,

    @see SharedObjectPtr
*/
class AbstractSocket
    : public asio::ssl::stream_base
    , public asio::socket_base
{
protected:
    //--------------------------------------------------------------------------
    //
    // Buffers
    //
    //--------------------------------------------------------------------------

    /** Storage for a BufferSequence.

        Meets these requirements:
            BufferSequence
            ConstBufferSequence (when Buffer is mutable_buffer)
            MutableBufferSequence (when Buffer is const_buffer)
    */
    template <class Buffer>
    class Buffers
    {
    public:
        typedef Buffer value_type;
        typedef typename std::vector <Buffer>::const_iterator const_iterator;

        Buffers ()
            : m_size (0)
        {
        }

        template <class OtherBuffers>
        explicit Buffers (OtherBuffers const& buffers)
            : m_size (0)
        {
            m_buffers.reserve (std::distance (buffers.begin (), buffers.end ()));
            BOOST_FOREACH (typename OtherBuffers::value_type buffer, buffers)
            {
                m_size += asio::buffer_size (buffer);
                m_buffers.push_back (buffer);
            }
        }

        /** Determine the total size of all buffers.
            This is faster than calling asio::buffer_size.
        */
        std::size_t size () const noexcept
        {
            return m_size;
        }

        const_iterator begin () const noexcept
        {
            return m_buffers.begin ();
        }

        const_iterator end () const noexcept
        {
            return m_buffers.end ();
        }

        /** Retrieve a consumed BufferSequence. */
        Buffers consumed (std::size_t bytes) const
        {
            Buffers result;
            result.m_buffers.reserve (m_buffers.size ());
            BOOST_FOREACH (Buffer buffer, m_buffers)
            {
                std::size_t const have = asio::buffer_size (buffer);
                std::size_t const reduce = std::min (bytes, have);
                bytes -= reduce;

                if (have > reduce)
                    result.m_buffers.push_back (buffer + reduce);
            }
            return result;
        }

    private:
        std::size_t m_size;
        std::vector <Buffer> m_buffers;
    };

    /** Meets the requirements of ConstBufferSequence */
    typedef Buffers <asio::const_buffer> ConstBuffers;

    /** Meets the requirements of MutableBufferSequence */
    typedef Buffers <asio::mutable_buffer> MutableBuffers;

    //--------------------------------------------------------------------------
    //
    // Handler abstractions
    //
    //--------------------------------------------------------------------------

    //  Meets these requirements:
    //
    //      CompletionHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/CompletionHandler.html
    //
    class CompletionCall
    {
    public:
        typedef void result_type;

        template <class Handler>
        CompletionCall (BOOST_ASIO_MOVE_ARG (Handler) handler)
            : m_call (new CallType <Handler> (handler))
        {
        }

        CompletionCall (CompletionCall const& other)
            : m_call (other.m_call)
        { 
        }

        void operator() ()
        {
            (*m_call) ();
        }

    private:
        struct Call : SharedObject, LeakChecked <Call>
        {
            virtual void operator() () = 0;
        };

        template <class Handler>
        struct CallType : Call
        {
            CallType (BOOST_ASIO_MOVE_ARG (Handler) handler)
                : m_handler (handler)
            {
            }

            void operator() ()
            {
                m_handler ();
            }

            Handler m_handler;
        };

    private:
        SharedObjectPtr <Call> m_call;
    };

    //  Meets these requirements:
    //
    //      AcceptHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AcceptHandler.html
    //
    //      ConnectHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ConnectHandler.html
    //
    //      ShutdownHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ShutdownHandler.html
    //
    //      HandshakeHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/HandshakeHandler.html
    //
    class ErrorCall
    {
    public:
        typedef void result_type;

        template <class Handler>
        ErrorCall (BOOST_ASIO_MOVE_ARG (Handler) handler)
            : m_call (new CallType <Handler> (handler))
        {
        }

        ErrorCall (ErrorCall const& other)
            : m_call (other.m_call)
        { 
        }

        void operator() (system::error_code const& ec)
        {
            (*m_call) (ec);
        }

    private:
        struct Call : SharedObject, LeakChecked <Call>
        {
            virtual void operator() (system::error_code const&) = 0;
        };

        template <class Handler>
        struct CallType : Call
        {
            CallType (BOOST_ASIO_MOVE_ARG (Handler) handler)
                : m_handler (handler)
            {
            }

            void operator() (system::error_code const& ec)
            {
                m_handler (ec);
            }

            Handler m_handler;
        };

    private:
        SharedObjectPtr <Call> m_call;
    };

    //  Meets these requirements
    //
    //      ReadHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ReadHandler.html
    //
    //      WriteHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/WriteHandler.html
    //
    //      BUfferedHandshakeHandler
    //      http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/BufferedHandshakeHandler.html
    //
    class TransferCall
    {
    public:
        typedef void result_type;

        template <class Handler>
        TransferCall (BOOST_ASIO_MOVE_ARG (Handler) handler)
            : m_call (new CallType <Handler> (handler))
        {
        }

        TransferCall (TransferCall const& other)
            : m_call (other.m_call)
        { 
        }

        void operator() (system::error_code const& ec, std::size_t bytes_transferred)
        {
            (*m_call) (ec, bytes_transferred);
        }

    private:
        struct Call : SharedObject, LeakChecked <Call>
        {
            virtual void operator() (system::error_code const&, std::size_t) = 0;
        };

        template <class Handler>
        struct CallType : Call
        {
            CallType (BOOST_ASIO_MOVE_ARG (Handler) handler)
                : m_handler (handler)
            {
            }

            void operator() (system::error_code const& ec, std::size_t bytes_transferred)
            {
                m_handler (ec, bytes_transferred);
            }

            Handler m_handler;
        };

    private:
        SharedObjectPtr <Call> m_call;
    };

public:
    typedef SharedObjectPtr <AbstractSocket> Ptr;

    virtual ~AbstractSocket () { }

    //--------------------------------------------------------------------------
    //
    // General attributes
    //
    //--------------------------------------------------------------------------

    /** Determines if the underlying stream requires a handshake.

        If is_handshaked is true, it will be necessary to call handshake or
        async_handshake after the connection is established. Furthermore it
        will be necessary to call the shutdown member from the
        HandshakeInterface to close the connection. Do not close the underlying
        socket or else the closure will not be graceful. Only one side should
        initiate the handshaking shutdon. The other side should observe it.
        Which side does what is up to the user.
    */
    virtual bool is_handshaked () = 0;

    /** Retrieve the underlying object.
        Returns nullptr if the implementation doesn't match. Usually
        you will use this if you need to get at the underlying boost::asio
        object. For example:

        @code

        void set_options (AbstractSocket& socket)
        {
            bost::asio::ip::tcp::socket* sock =
                socket.native_object <bost::asio::ip::tcp::socket> ();

            if (sock != nullptr)
                sock->set_option (
                    boost::asio::ip::tcp::no_delay (true));
        }

        @endcode
    */
    template <class Object>
    Object* native_object ()
    {
        void* const object = native_object_raw ();
        if (object != nullptr)
            return dynamic_cast <Object> (object);
        return object;
    }

    virtual void* native_object_raw () = 0;

    //--------------------------------------------------------------------------
    //
    // SocketInterface
    //
    //--------------------------------------------------------------------------

    void cancel ()
    {
        system::error_code ec;
        throw_error (cancel (ec));
    }

    virtual system::error_code cancel (system::error_code& ec) = 0;

    void shutdown (shutdown_type what)
    {
        system::error_code ec;
        throw_error (shutdown (what, ec));
    }

    virtual system::error_code shutdown (shutdown_type what, system::error_code& ec) = 0;

    void close ()
    {
        system::error_code ec;
        throw_error (close (ec));
    }

    virtual system::error_code close (system::error_code& ec) = 0;

    //--------------------------------------------------------------------------
    //
    // StreamInterface
    //
    //--------------------------------------------------------------------------

    // SyncReadStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncReadStream.html
    //
    template <class MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, system::error_code& ec)
    {
        return read_some_impl (MutableBuffers (buffers), ec);
    }

    virtual std::size_t read_some_impl (MutableBuffers const& buffers, system::error_code& ec) = 0;

    // SyncWriteStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncWriteStream.html
    //
    template <class ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, system::error_code &ec)
    {
        return write_some_impl (ConstBuffers (buffers), ec);
    }

    virtual std::size_t write_some_impl (ConstBuffers const& buffers, system::error_code& ec) = 0;

    // AsyncReadStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncReadStream.html
    //
    template <class MutableBufferSequence, class ReadHandler>
    void async_read_some (MutableBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(ReadHandler) handler)
    {
        async_read_some_impl (MutableBuffers (buffers), handler);
    }

    virtual void async_read_some_impl (MutableBuffers const& buffers, TransferCall const& call) = 0;

    // AsyncWriteStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncWriteStream.html
    //
    template <class ConstBufferSequence, class WriteHandler>
    void async_write_some (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        async_write_some_impl (ConstBuffers (buffers), handler);
    }

    virtual void async_write_some_impl (ConstBuffers const& buffers, TransferCall const& call) = 0;

    //--------------------------------------------------------------------------
    //
    // HandshakeInterface
    //
    //--------------------------------------------------------------------------

    // ssl::stream::handshake (1 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload1.html
    //
    void handshake (handshake_type role)
    {
        system::error_code ec;
        throw_error (handshake (role, ec));
    }

    // ssl::stream::handshake (2 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload2.html
    //
    virtual system::error_code handshake (handshake_type role, system::error_code& ec) = 0;

#if (BOOST_VERSION / 100) >= 1054
    // ssl::stream::handshake (3 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload3.html
    //
    template <class ConstBufferSequence>
    void handshake (handshake_type role, ConstBufferSequence const& buffers)
    {
        system::error_code ec;
        throw_error (handshake (role, buffers, ec));
    }

    // ssl::stream::handshake (4 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload4.html
    //
    template <class ConstBufferSequence>
    system::error_code handshake (handshake_type role,
        ConstBufferSequence const& buffers, system::error_code& ec)
    {
        return handshake_impl (role, ConstBuffers (buffers), ec);
    }

    virtual system::error_code handshake_impl (handshake_type role,
        ConstBuffers const& buffers, system::error_code& ec) = 0;
#endif

    // ssl::stream::async_handshake (1 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload1.html
    //
    template <class HandshakeHandler>
    void async_handshake (handshake_type role, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        async_handshake_impl (role, handler);
    }

    virtual void async_handshake_impl (handshake_type role, ErrorCall const& calll) = 0;

#if (BOOST_VERSION / 100) >= 1054
    // ssl::stream::async_handshake (2 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload2.html
    //
    template <class ConstBufferSequence, class BufferedHandshakeHandler>
    void async_handshake (handshake_type role, ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        async_handshake_impl (role, ConstBuffers (buffers), handler);
    }

    virtual void async_handshake_impl (handshake_type role,
        ConstBuffers const& buffers, TransferCall const& call) = 0;
#endif

    // ssl::stream::shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/shutdown.html
    //
    void shutdown ()
    {
        system::error_code ec;
        throw_error (shutdown (ec));
    }

    virtual system::error_code shutdown (system::error_code& ec) = 0;

    // ssl::stream::async_shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_shutdown.html
    //
    template <class ShutdownHandler>
    void async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler)
    {
        async_shutdown_impl (handler);
    }

    virtual void async_shutdown_impl (ErrorCall const& call) = 0;

private:
    void throw_error (system::error_code const& ec)
    {
        if (ec)
            Throw (system::system_error (ec), __FILE__, __LINE__);
    }
};

//------------------------------------------------------------------------------

/** Interfaces compatible with some of basic_socket
    http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/basic_stream_socket.html
*/
struct SocketInterface { };

/** Interfaces compatible with some of basic_stream_socket
    http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/basic_stream_socket.html
*/
struct StreamInterface { };

/** Interfaces compatible with some of ssl::stream
    http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream.html
*/
struct HandshakeInterface { };

// Determines the set of interfaces supported by the AsioType
template <typename Object>
struct InterfacesOf
{
    // This should be changed to UnknownConcept. Why bother wrapping
    // our own classes if they can just be derived from the abstract
    // interface? Only classes dervied from the corresponding
    // boost::asio objects should ever be wrapped.
    //
    typedef typename Object::Interfaces type;
    typedef type value;
};

// asio::basic_socket
template <typename Protocol, typename SocketService>
struct InterfacesOf <boost::asio::basic_socket <Protocol, SocketService> >
{
    struct value : SocketInterface { };
    typedef value type;
};

// asio::basic_stream_socket
template <typename Protocol, typename SocketService>
struct InterfacesOf <boost::asio::basic_stream_socket <Protocol, SocketService> >
{
    struct value : SocketInterface, StreamInterface { };
    typedef value type;
};

// asio::ssl::Stream
template <typename Stream>
struct InterfacesOf <boost::asio::ssl::stream <Stream> >
{
    struct value : StreamInterface, HandshakeInterface { };
    typedef value type;
};

// ideas from boost::mpl
template <bool C>
struct Bool { };
typedef Bool <true> True;
typedef Bool <false> False;

// determines if the AsioType supports the specified Interface
template <typename AsioType, typename Interface, class Enable = void>
struct HasInterface : False { };

template <typename AsioType, typename Interface>
struct HasInterface <AsioType, Interface,
    typename boost::enable_if <boost::is_base_of <
    Interface, typename InterfacesOf <AsioType>::type> >::type >
    : True { };

//------------------------------------------------------------------------------

// Common stuff for all Wrapper specializatons
//
template <class Object>
class Wrapper : public AbstractSocket
{
public:
    typedef Object ObjectType;
    typedef typename remove_reference <Object>::type ObjectT;
    typedef typename remove_reference <Object>::type next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
    typedef asio::ssl::stream_base::handshake_type handshake_type;

    Wrapper (Object& object) noexcept
        : m_impl (&object)
    {
    }

    // Retrieve the underlying object
    Object& get_object () const noexcept
    {
        fatal_assert (m_impl != nullptr);
        return *m_impl;
    }

    asio::io_service& get_io_service () noexcept
    {
        return get_object ().get_io_service ();
    }

    next_layer_type& next_layer () noexcept
    {
        return get_object ().next_layer ();
    }

    next_layer_type const& next_layer () const noexcept
    {
        return get_object ().next_layer ();
    }

    lowest_layer_type& lowest_layer () noexcept
    {
        return get_object ().lowest_layer ();
    }

    lowest_layer_type const& lowest_layer () const noexcept
    {
        return get_object ().lowest_layer ();
    }

    // General

    bool to_bool (True) { return true; }
    bool to_bool (False) { return false; }
    bool is_handshaked ()
    {
        return to_bool (HasInterface <ObjectT, HandshakeInterface> ());
    }

    void* native_object_raw ()
    {
        return m_impl;
    }

    // SocketInterface

    system::error_code cancel (system::error_code& ec)
    {
        return cancel (ec, HasInterface <ObjectT, SocketInterface> ());
    }
    
    system::error_code shutdown (shutdown_type what, system::error_code& ec)
    {
        return shutdown (what, ec, HasInterface <ObjectT, SocketInterface> ());
    }

    system::error_code close (system::error_code& ec)
    {
        return close (ec, HasInterface <ObjectT, SocketInterface> ());
    }

    // StreamInterface

    std::size_t read_some_impl (AbstractSocket::MutableBuffers const& buffers,
        system::error_code& ec)
    {
        return read_some (buffers, ec, HasInterface <ObjectT, StreamInterface> ());
    }

    std::size_t write_some_impl (AbstractSocket::ConstBuffers const& buffers,
        system::error_code& ec)
    {
        return write_some (buffers, ec, HasInterface <ObjectT, StreamInterface> ());
    }

    void async_read_some_impl (AbstractSocket::MutableBuffers const& buffers,
        TransferCall const& call)
    {
        async_read_some (buffers, call, HasInterface <ObjectT, StreamInterface> ());
    }

    void async_write_some_impl (AbstractSocket::ConstBuffers const& buffers,
        TransferCall const& call)
    {
        async_write_some (buffers, call, HasInterface <ObjectT, StreamInterface> ());
    }

    // HandshakeInterface

    system::error_code handshake (handshake_type type, system::error_code& ec)
    {
        return handshake (type, ec, HasInterface <ObjectT, HandshakeInterface> ());
    }

#if (BOOST_VERSION / 100) >= 1054
    system::error_code handshake_impl (handshake_type role,
        AbstractSocket::ConstBuffers const& buffers, system::error_code& ec)
    {
        return handshake (role, buffers, ec, HasInterface <ObjectT, HandshakeInterface> ());
    }
#endif

    void async_handshake_impl (handshake_type role, ErrorCall const& call)
    {
        async_handshake (role, call, HasInterface <ObjectT, HandshakeInterface> ());
    }

#if (BOOST_VERSION / 100) >= 1054
    void async_handshake_impl (handshake_type role,
        AbstractSocket::ConstBuffers const& buffers, TransferCall const& call)
    {
        async_handshake (role, buffers, call, HasInterface <ObjectT, HandshakeInterface> ());
    }
#endif

    system::error_code shutdown (system::error_code& ec)
    {
        return shutdown (ec, HasInterface <ObjectT, HandshakeInterface> ());
    }

    void async_shutdown_impl (ErrorCall const& call)
    {
        async_shutdown (call, HasInterface <ObjectT, HandshakeInterface> ());
    }

protected:
    explicit Wrapper (Object* object = nullptr) noexcept
        : m_impl (object)
    {
    }

    void set (Object* ptr) noexcept
    {
        m_impl = ptr;
    }

private:
    static system::error_code fail (system::error_code const& ec = system::error_code ())
    {
        fatal_error ("pure virtual");
        return ec;
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface
    //
    //--------------------------------------------------------------------------

    system::error_code cancel (system::error_code& ec, True)
    {
        return get_object ().cancel (ec);
    }

    system::error_code cancel (system::error_code& ec, False)
    {
        return fail ();
    }

    system::error_code shutdown (AbstractSocket::shutdown_type what, boost::system::error_code& ec, True)
    {
        return get_object ().shutdown (what, ec);
    }

    system::error_code shutdown (AbstractSocket::shutdown_type what, boost::system::error_code& ec, False)
    {
        return fail ();
    }

    system::error_code close (system::error_code& ec, True)
    {
        return get_object ().close (ec);
    }

    system::error_code close (system::error_code& ec, False)
    {
       return fail ();
    }

    //--------------------------------------------------------------------------
    //
    // StreamInterface
    //
    //--------------------------------------------------------------------------

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, system::error_code& ec, True)
    {
        return get_object ().read_some (buffers, ec);
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, system::error_code& ec, False)
    {
        fail ();
        return 0;
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, system::error_code& ec, True)
    {
        return get_object ().write_some (buffers, ec);
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, system::error_code& ec, False)
    {
        fail ();
        return 0;
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler, True)
    {
        get_object ().async_read_some (buffers, handler);
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler, False)
    {
        fail ();
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler, True)
    {
        return get_object ().async_write_some (buffers, handler);
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler, False)
    {
        fail ();
    }

    //--------------------------------------------------------------------------
    //
    // HandshakeInteface
    //
    //--------------------------------------------------------------------------

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec, True)
    {
        return get_object ().handshake (type, ec);
    }

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec, False)
    {
        fail ();
        return boost::system::error_code ();
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type type, ConstBufferSequence const& buffers,
        boost::system::error_code& ec, True)
    {
        return get_object ().handshake (type, buffers, ec);
    }

    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type type, ConstBufferSequence const& buffers,
        boost::system::error_code& ec, False)
    {
        fail ();
        return boost::system::error_code ();
    }
#endif

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler, True)
    {
        return get_object ().async_handshake (type, handler);
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler, False)
    {
        fail ();
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, const ConstBufferSequence& buffers,
    BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler, True)
    {
        return get_object ().async_handshake (type, buffers, handler);
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, const ConstBufferSequence& buffers,
    BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler, False)
    {
        fail ();
    }
#endif

    boost::system::error_code shutdown (boost::system::error_code& ec, True)
    {
        return get_object ().shutdown (ec);
    }

    boost::system::error_code shutdown (boost::system::error_code& ec, False)
    {
        fail ();
        return boost::system::error_code ();
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler, True)
    {
        return get_object ().async_shutdown (handler);
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler, False)
    {
        fail ();
    }

private:
    Object* m_impl;
};

//------------------------------------------------------------------------------

/** A reference counted container for a dynamic object. */
template <class Object>
class SharedObjectBase
{
protected:
    explicit SharedObjectBase (Object* object = nullptr)
        : m_handle ((object == nullptr) ? nullptr : new Handle (object))
    {
    }

    template <class Other>
    explicit SharedObjectBase (SharedObjectBase <Other> const& other) noexcept
        : m_handle (other.m_handle)
    {
    }

    Object* get_object_ptr () const noexcept
    {
        return m_handle->get ();
    }

    template <class Other>
    static inline Other* get_other (SharedObjectBase <Other> const& other)
    {
        return other.m_handle->get ();
    }

    template <class Other>
    void set_other (SharedObjectBase <Other> const& other) noexcept
    {
        m_handle = other.m_handle;
    }

private:
    template <class T>
    friend bool operator== (SharedObjectBase <T> const& lhs, T const& rhs) noexcept;

    template <class T>
    friend bool operator== (T const& lhs, SharedObjectBase <T> const& rhs) noexcept;

    template <class T>
    friend bool operator== (SharedObjectBase <T> const& lhs, SharedObjectBase <T> const& rhs) noexcept;

    template <class T>
    friend bool operator!= (SharedObjectBase <T> const& lhs, T const& rhs) noexcept;

    template <class T>
    friend bool operator!= (T const& lhs, SharedObjectBase <T> const& rhs) noexcept;

    template <class T>
    friend bool operator!= (SharedObjectBase <T> const& lhs, SharedObjectBase <T> const& rhs) noexcept;

    class Handle : public SharedObject
    {
    public:
        typedef SharedObjectPtr <Handle> Ptr;
        explicit Handle (Object* object) noexcept : m_object (object) { }
        Object* get () const noexcept { return m_object.get (); }
    private:
        ScopedPointer <Object> m_object;
    };

    typename Handle::Ptr m_handle;
};

// We explicitly discourage pointer comparisons

template <class Object>
bool operator== (SharedObjectBase <Object> const& lhs, Object const& rhs) noexcept
{
    return lhs.get_object_ptr () == &rhs;
}

template <class Object>
bool operator== (Object const& lhs, SharedObjectBase <Object> const& rhs) noexcept
{
    return &lhs == rhs.get_object_ptr ();
}

template <class Object>
bool operator== (SharedObjectBase <Object> const& lhs, SharedObjectBase <Object> const& rhs) noexcept
{
    return lhs.get_object_ptr () == rhs.get_object_ptr ();
}

template <class Object>
bool operator!= (SharedObjectBase <Object> const& lhs, Object const& rhs) noexcept
{
    return lhs.get_object_ptr () != &rhs;
}

template <class Object>
bool operator!= (Object const& lhs, SharedObjectBase <Object> const& rhs) noexcept
{
    return &lhs != rhs.get_object_ptr ();
}

template <class Object>
bool operator!= (SharedObjectBase <Object> const& lhs, SharedObjectBase <Object> const& rhs) noexcept
{
    return lhs.get_object_ptr () != rhs.get_object_ptr ();
}

//------------------------------------------------------------------------------

/** A reference counted pointer to an object wrapped in an interface.

    This takes control of the underlying object, which must be
    dynamically allocated via operator new.
*/
template <class Object>
class SharedWrapper
    : public Wrapper <Object>
    , public SharedObjectBase <Object>
{
public:
    // Take ownership of existing object
    // If other shared containers have a reference, undefined behavior results.
    explicit SharedWrapper (Object* object = nullptr) noexcept
        : Wrapper <Object> (object)
        , SharedObjectBase <Object> (object)
    {
    }

    // Receive a reference to an existing shared object
    template <class OtherObject>
    SharedWrapper (SharedWrapper <OtherObject> const& other) noexcept
        : Wrapper <Object> (get_other (other))
        , SharedObjectBase <Object> (other)
    {
    }

    // Receive a reference to an existing shared obeject
    template <class Other>
    SharedWrapper& operator= (SharedObjectBase <Other> const& other) noexcept
    {
        set_other (other);
        return *this;
    }

private:
    // disallowed
    SharedWrapper& operator= (Object*) const noexcept;
};

}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

namespace AsioUnitTestsNamespace
{

using namespace Asio;

class SslContext : public Uncopyable
{
public:
    SslContext ()
    {
    }

    virtual ~SslContext () { }

    virtual asio::ssl::context& get_object () = 0;

protected:
};

//------------------------------------------------------------------------------

class RippleSslContext : public SslContext
{
public:
    RippleSslContext ()
        : m_context (asio::ssl::context::sslv23)
    {
        init_ssl_context (m_context);
    }

    asio::ssl::context& get_object ()
    {
        return m_context;
    }

public:
    static char const* get_ciphers ()
    {
        static char const* ciphers = "ALL:!LOW:!EXP:!MD5:@STRENGTH";

        return ciphers;
    }

    static DH* get_dh_params (int /*key_length*/)
    {
        static const unsigned char raw512DHParams [] =
        {
            0x30, 0x46, 0x02, 0x41, 0x00, 0x98, 0x15, 0xd2, 0xd0, 0x08, 0x32, 0xda,
            0xaa, 0xac, 0xc4, 0x71, 0xa3, 0x1b, 0x11, 0xf0, 0x6c, 0x62, 0xb2, 0x35,
            0x8a, 0x10, 0x92, 0xc6, 0x0a, 0xa3, 0x84, 0x7e, 0xaf, 0x17, 0x29, 0x0b,
            0x70, 0xef, 0x07, 0x4f, 0xfc, 0x9d, 0x6d, 0x87, 0x99, 0x19, 0x09, 0x5b,
            0x6e, 0xdb, 0x57, 0x72, 0x4a, 0x7e, 0xcd, 0xaf, 0xbd, 0x3a, 0x97, 0x55,
            0x51, 0x77, 0x5a, 0x34, 0x7c, 0xe8, 0xc5, 0x71, 0x63, 0x02, 0x01, 0x02
        };

        struct ScopedDH
        {
            explicit ScopedDH (DH* dh)
                : m_dh (dh)
            {
            }

            explicit ScopedDH (unsigned char const* rawData, std::size_t bytes)
                : m_dh (d2i_DHparams (nullptr, &rawData, bytes))
            {
            }

            ~ScopedDH ()
            {
                if (m_dh != nullptr)
                    DH_free (m_dh);
            }

            DH* get () const
            {
                return m_dh;
            }

            operator DH* () const
            {
                return get ();
            }

        private:
            DH* m_dh;
        };

        static ScopedDH dh512 (raw512DHParams, sizeof (raw512DHParams));

        return dh512;
    }

    static DH* tmp_dh_handler (SSL* /*ssl*/, int /*is_export*/, int key_length)
    {
        return DHparams_dup (get_dh_params (key_length));
    }

    static void init_ssl_context (asio::ssl::context& context)
    {
        context.set_options (
            asio::ssl::context::default_workarounds |
            asio::ssl::context::no_sslv2 |
            asio::ssl::context::single_dh_use);

        context.set_verify_mode (asio::ssl::verify_none);

        SSL_CTX_set_tmp_dh_callback (context.native_handle (), tmp_dh_handler);

        int const result = SSL_CTX_set_cipher_list (context.native_handle (), get_ciphers ());

        if (result != 1)
            FatalError ("invalid cipher list", __FILE__, __LINE__);
    }

private:
    asio::ssl::context m_context;
};

//------------------------------------------------------------------------------

// A handshaking stream that can distinguish multiple protocols
//
class RippleHandshakeStream
    : public asio::socket_base
    , public asio::ssl::stream_base
{
public:
    struct Interfaces : SocketInterface, StreamInterface, HandshakeInterface { };

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
        , m_next_layer (arg)
        , m_io_service (m_next_layer.get_io_service ())
        , m_strand (m_io_service)
        , m_status (needMore)
        , m_role (AbstractSocket::client)

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

    AbstractSocket& stream () const noexcept
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

    system::error_code shutdown (AbstractSocket::shutdown_type what, boost::system::error_code& ec)
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
                handler, system::error_code (), amount)));
        }
        return stream ().async_read_some (buffers, m_strand.wrap (handler));
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        return stream ().async_write_some (buffers, handler);
    }

    //--------------------------------------------------------------------------

    system::error_code handshake (AbstractSocket::handshake_type role, system::error_code& ec)
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

#if (BOOST_VERSION / 100) >= 1054
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
            return handshakePlainAsync (handler);
            break;

        case actionSsl:
            return handshakeSslAsync (handler);
            break;

        case actionDetect:
            return detectHandshakeAsync (handler);
            break;
        }
    }

#if (BOOST_VERSION / 100) >= 1054
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
            return handshakePlainAsync (buffers, handler);
            break;

        case actionSsl:
            return handshakeSslAsync (buffers, handler);
            break;

        case actionDetect:
            return detectHandshakeAsync (buffers, handler);
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
            m_ssl_stream->async_shutdown (m_strand.wrap (handler));
        }
        else
        {
            system::error_code ec;
            m_next_layer.shutdown (next_layer_type::shutdown_both, ec);
            m_io_service.post (m_strand.wrap (boost::bind (handler, ec)));
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
    Action calcAction (AbstractSocket::handshake_type role)
    {
        m_role = role;

        if (role == AbstractSocket::server)
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
        else if (m_role == AbstractSocket::client)
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
        m_stream = new Wrapper <next_layer_type> (m_next_layer);
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
        return m_io_service.post (m_strand.wrap (boost::bind (handler, system::error_code())));
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    handshakePlainAsync (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG (BufferedHandshakeHandler) handler)
    {
        fatal_assert (asio::buffer_size (buffers) == 0);
        createPlainStream ();
        return m_io_service.post (m_strand.wrap (boost::bind (handler, system::error_code(), 0)));
    }
#endif

    void createSslStream ()
    {
        m_status = ssl;
        m_ssl_stream = new SslStreamType (m_next_layer, m_context.get_object ());
        m_stream = new Wrapper <SslStreamType> (*m_ssl_stream);
    }

    void handshakeSsl (system::error_code& ec)
    {
        createSslStream ();
        m_ssl_stream->handshake (m_role, ec);
    }

#if (BOOST_VERSION / 100) >= 1054
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
        return m_ssl_stream->async_handshake (m_role, handler);
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE (BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    handshakeSslAsync (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG (BufferedHandshakeHandler) handler)
    {
        createSslStream ();
        return m_ssl_stream->async_handshake (m_role, buffers, handler);
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

#if (BOOST_VERSION / 100) >= 1054
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
    void onDetectRead (HandshakeHandler handler,
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
                        handshakePlainAsync (handler);
                        break;
                    case actionSsl:
                        handshakeSslAsync (handler);
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
                m_io_service.post (m_strand.wrap (boost::bind (handler, ec)));
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
            m_strand.wrap (boost::bind (&ThisType::onDetectRead <HandshakeHandler>, this, handler,
            asio::placeholders::error, asio::placeholders::bytes_transferred)));
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    detectHandshakeAsync (ConstBufferSequence const& buffers, BufferedHandshakeHandler handler)
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
    RippleSslContext m_context;
    Stream m_next_layer;
    asio::io_service& m_io_service;
    asio::io_service::strand m_strand;
    Status m_status;
    AbstractSocket::handshake_type m_role;
    ScopedPointer <AbstractSocket> m_stream;
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
                   AbstractSocket::handshake_type role)
            : Thread ((role == AbstractSocket::client) ? "client" : "server")
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
                if (m_role == AbstractSocket::server)
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

        asio::io_service& get_io_service ()
        {
            return m_io_service;
        }

        virtual system::error_code start (system::error_code& ec) = 0;

        virtual void finish () = 0;

    protected:
        UnitTest& m_test;
        Scenario& m_scenario;
        AbstractSocket::handshake_type const m_role;

    private:
        asio::io_service m_io_service;
    };

    //--------------------------------------------------------------------------

    // Common code for synchronous operations
    //
    class BasicSync : public BasicTest
    {
    public:
        BasicSync (UnitTest& test,
                   Scenario& scenario,
                   AbstractSocket::handshake_type role)
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
            : BasicSync (test, scenario, AbstractSocket::server)
        {
        }

        void process (AbstractSocket& socket, system::error_code& ec)
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
            : BasicSync (test, scenario, AbstractSocket::client)
        {
        }

        void process (AbstractSocket& socket, system::error_code& ec)
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
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        SyncServer (UnitTest& test, Scenario& scenario)
            : BasicSyncServer (test, scenario)
            , m_acceptor (get_io_service ())
            , m_socket (get_io_service ())
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return ec;

            if (! check_success (m_acceptor.set_option (typename Socket::reuse_address (true), ec)))
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

            Wrapper <Socket> socket (m_socket);

            process (socket, ec);

            if (! ec)
            {
                if (! thread_success (m_socket.shutdown (Socket::shutdown_both, ec)))
                    return;

                if (! thread_success (m_socket.close (ec)))
                    return;
            }
        }

    protected:
        Acceptor m_acceptor;
        Socket m_socket;
    };

    //--------------------------------------------------------------------------

    // A synchronous client
    //
    template <class Transport>
    class SyncClient : public BasicSyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        SyncClient (UnitTest& test, Scenario& scenario)
            : BasicSyncClient (test, scenario)
            , m_socket (get_io_service ())
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

            Wrapper <Socket> socket (m_socket);

            process (socket, ec);

            if (! ec)
            {
                if (! thread_success (m_socket.shutdown (Socket::shutdown_both, ec)))
                    return;

                if (! thread_success (m_socket.close (ec)))
                    return;
            }
        }

    private:
        Socket m_socket;
    };

    //--------------------------------------------------------------------------

    // A synchronous server that supports a handshake
    //
    template <class Transport, template <class Stream = typename Transport::Protocol::socket&> class HandshakeType>
    class HandshakeSyncServer : public BasicSyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        HandshakeSyncServer (UnitTest& test, Scenario& scenario)
            : BasicSyncServer (test, scenario)
            , m_socket (get_io_service ())
            , m_acceptor (get_io_service ())
            , m_handshake (m_socket, m_scenario.handshakeOptions)
        {
        }

        system::error_code start (system::error_code& ec)
        {
            ec = system::error_code ();

            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return ec;

            if (! check_success (m_acceptor.set_option (typename Socket::reuse_address (true), ec)))
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

            Wrapper <HandshakeType <Socket&> > socket (m_handshake);

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
        Socket m_socket;
        Acceptor m_acceptor;
        HandshakeType <Socket&> m_handshake;
    };

    //--------------------------------------------------------------------------

    // A synchronous client that supports a handshake
    //
    template <class Transport, template <class Stream> class HandshakeType>
    class HandshakeSyncClient : public BasicSyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        HandshakeSyncClient (UnitTest& test, Scenario& scenario)
            : BasicSyncClient (test, scenario)
            , m_socket (get_io_service ())
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

            Wrapper <HandshakeType <Socket&> > socket (m_handshake);

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
        Socket m_socket;
        HandshakeType <Socket&> m_handshake;
    };

    //--------------------------------------------------------------------------

    // Common code for asynchronous operations
    //
    class BasicAsync : public BasicTest
    {
    public:
        BasicAsync (UnitTest& test,
                    Scenario& scenario,
                    AbstractSocket::handshake_type role,
                    AbstractSocket& socket)
            : BasicTest (test, scenario, role)
            , m_socket (socket)
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
        void run ()
        {
            get_io_service ().run ();
            m_done.signal ();
        }

        AbstractSocket& socket ()
        {
            return m_socket;
        }

        virtual void on_start (system::error_code& ec) = 0;

        virtual void on_shutdown (system::error_code const& ec) = 0;

        virtual void closed () = 0;

    protected:
        asio::streambuf m_buf;

    private:
        WaitableEvent m_done;
        AbstractSocket& m_socket;
    };

    //--------------------------------------------------------------------------

    // Common code for asynchronous servers
    //
    class BasicAsyncServer : public BasicAsync
    {
    public:
        BasicAsyncServer (UnitTest& test,
                          Scenario& scenario,
                          AbstractSocket& socket)
            : BasicAsync (test, scenario, AbstractSocket::server, socket)
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
                    if (! thread_success (socket ().shutdown (AbstractSocket::shutdown_both, ec)))
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

                if (! thread_success (socket ().shutdown (AbstractSocket::shutdown_both, ec)))
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
        BasicAsyncClient (UnitTest& test,
                          Scenario& scenario,
                          AbstractSocket& socket)
            : BasicAsync (test, scenario, AbstractSocket::client, socket)
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

                if (! thread_success (socket ().shutdown (AbstractSocket::shutdown_both, ec)))
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

                if (! thread_success (socket ().shutdown (AbstractSocket::shutdown_both, ec)))
                    return;

                if (! thread_success (socket ().close (ec)))
                    return;

                closed ();
            }
        }
    };

    //--------------------------------------------------------------------------

    // An asynchronous server
    //
    template <class Transport>
    class AsyncServer : public BasicAsyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        AsyncServer (UnitTest& test, Scenario& scenario)
            : BasicAsyncServer (test, scenario, m_socketWrapper)
            , m_acceptor (get_io_service ())
            , m_socket (get_io_service ())
            , m_socketWrapper (m_socket)
        {
        }

        void on_start (system::error_code& ec)
        {
            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return;

            if (! check_success (m_acceptor.set_option (typename Socket::reuse_address (true), ec)))
                return;

            if (! check_success (m_acceptor.bind (Transport::server_endpoint (), ec)))
                return;

            if (! check_success (m_acceptor.listen (asio::socket_base::max_connections, ec)))
                return;

            m_acceptor.async_accept (m_socket, boost::bind (
                &BasicAsyncServer::on_accept, this, asio::placeholders::error));
        }

    private:
        void closed ()
        {
            system::error_code ec;
            if (! thread_success (m_acceptor.close (ec)))
                return;
        }

    private:
        Acceptor m_acceptor;
        Socket m_socket;
        asio::streambuf m_buf;
        Wrapper <Socket> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // An asynchronous client
    //
    template <class Transport>
    class AsyncClient : public BasicAsyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;

        AsyncClient (UnitTest& test, Scenario& scenario)
            : BasicAsyncClient (test, scenario, m_socketWrapper)
            , m_socket (get_io_service ())
            , m_socketWrapper (m_socket)
        {
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
        Socket m_socket;
        asio::streambuf m_buf;
        Wrapper <Socket> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // An asynchronous handshaking server
    //
    template <class Transport, template <class Stream> class HandshakeType>
    class HandshakeAsyncServer : public BasicAsyncServer
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;
        typedef HandshakeType <Socket&> StreamType;
        typedef HandshakeAsyncServer <Transport, HandshakeType> ThisType;

        HandshakeAsyncServer (UnitTest& test, Scenario& scenario)
            : BasicAsyncServer (test, scenario, m_socketWrapper)
            , m_acceptor (get_io_service ())
            , m_socket (m_acceptor.get_io_service ())
            , m_stream (m_socket, m_scenario.handshakeOptions)
            , m_socketWrapper (m_stream)
        {
        }

        void on_start (system::error_code& ec)
        {
            if (! check_success (m_acceptor.open (Transport::server_endpoint ().protocol (), ec)))
                return;

            if (! check_success (m_acceptor.set_option (typename Socket::reuse_address (true), ec)))
                return;

            if (! check_success (m_acceptor.bind (Transport::server_endpoint (), ec)))
                return;

            if (! check_success (m_acceptor.listen (asio::socket_base::max_connections, ec)))
                return;

            m_acceptor.async_accept (m_socket, boost::bind (
                &ThisType::on_accept, this, asio::placeholders::error));
        }

    private:
        void on_accept (system::error_code const& ec)
        {
            {
                system::error_code ec;
                if (! thread_success (m_acceptor.close (ec)))
                    return;
            }

            if (thread_success (ec))
            {
                socket ().async_handshake (AbstractSocket::server,
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
        Acceptor m_acceptor;
        Socket m_socket;
        asio::streambuf m_buf;
        StreamType m_stream;
        Wrapper <StreamType> m_socketWrapper;
    };

    //--------------------------------------------------------------------------

    // An asynchronous handshaking client
    //
    template <class Transport, template <class Stream> class HandshakeType>
    class HandshakeAsyncClient : public BasicAsyncClient
    {
    public:
        typedef typename Transport::Protocol    Protocol;
        typedef typename Protocol::socket       Socket;
        typedef typename Protocol::acceptor     Acceptor;
        typedef typename Protocol::endpoint     Endpoint;
        typedef HandshakeType <Socket&> StreamType;
        typedef HandshakeAsyncClient <Transport, HandshakeType> ThisType;

        HandshakeAsyncClient (UnitTest& test, Scenario& scenario)
            : BasicAsyncClient (test, scenario, m_socketWrapper)
            , m_socket (get_io_service ())
            , m_stream (m_socket, m_scenario.handshakeOptions)
            , m_socketWrapper (m_stream)
        {
        }

        void on_start (system::error_code& ec)
        {
            m_socket.async_connect (Transport::client_endpoint (), boost::bind (
                &ThisType::on_connect, this, asio::placeholders::error));
        }

    private:
        void on_connect (system::error_code const& ec)
        {
            if (thread_success (ec))
            {
                socket ().async_handshake (AbstractSocket::client,
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
        Socket m_socket;
        asio::streambuf m_buf;
        StreamType m_stream;
        Wrapper <StreamType> m_socketWrapper;
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
        beginTestCase ("facade");

        typedef asio::ip::tcp Protocol;
        typedef Protocol::socket Socket;

        asio::io_service ios;

        {
            SharedWrapper <Socket> f1 (new Socket (ios));
            SharedWrapper <Socket> f2 (f1);

            expect (f1 == f2);
        }

        {
            SharedWrapper <Socket> f1 (new Socket (ios));
            SharedWrapper <Socket> f2 (new Socket (ios));

            expect (f1 != f2);
            f2 = f1;
            expect (f1 == f2);
        }

        // test typedef inheritance
        {
            typedef Wrapper <Socket> SocketWrapper;
            typedef SocketWrapper::lowest_layer_type lowest_layer_type;
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
        // Synchronous
        testScenario <SyncServer  <Transport>, SyncClient  <Transport> > ();
        testScenario <HandshakeSyncServer <Transport, RippleHandshakeStreamType>,
                      SyncClient <Transport> > ();
        testScenario <SyncServer <Transport>,
                      HandshakeSyncClient <Transport, RippleHandshakeStreamType> > ();
        testScenario <HandshakeSyncServer <Transport, RippleHandshakeStreamType>,
                      HandshakeSyncClient <Transport, RippleHandshakeStreamType> > ();

        // Asynchronous
        testScenario <AsyncServer <Transport>, SyncClient  <Transport> > ();
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
    }

//------------------------------------------------------------------------------

    void runTest ()
    {
        testFacade ();
        testTransport <TcpV4> ();
        //testTransport <TcpV6> ();
    }
};

static AsioUnitTests asioUnitTests;

}
