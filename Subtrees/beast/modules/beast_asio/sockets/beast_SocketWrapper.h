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

#ifndef BEAST_SOCKETWRAPPER_H_INCLUDED
#define BEAST_SOCKETWRAPPER_H_INCLUDED

/** Wraps a reference to any object and exports all availble interfaces.

    If the object does not support an interface, calling those
    member functions will behave as if a pure virtual was called.

    Note that only a reference to the underlying is stored. Management
    of the lifetime of the object is controlled by the caller.

    Supports these concepts:

        CopyConstructible, CopyAssignable, Destructible
*/
template <class Object>
class SocketWrapper
    : public virtual Socket
    , protected SocketWrapperBasics
{
public:
    typedef typename boost::remove_reference <Object>::type ObjectType;

    SocketWrapper (Object& object) noexcept
        : m_impl (&object)
    {
    }

    SocketWrapper (SocketWrapper const& other) noexcept
        : m_impl (other.m_impl)
    {
    }

    SocketWrapper& operator= (SocketWrapper const& other) noexcept
    {
        m_impl = other.m_impl;
    }

    // Retrieve the underlying object
    Object& get_object () const noexcept
    {
        fatal_assert (m_impl != nullptr);
        return *m_impl;
    }

    // Retrieves a reference to the underlying socket.
    // usually asio::basic_socket or asio::basic_stream_socket
    // It must be compatible with our Protocol and SocketService
    // or else a std::bad cast will be thrown.
    //
    // The reason its a template class and not a function is
    // because it would otherwise generate a compile error
    // if Object did not have a declaration for
    // protocol_type::socket
    //
    template <typename AsioObject, class Enable = void>
    struct native_socket
    {
        typedef void* native_socket_type;
        native_socket (Socket&) { pure_virtual (); }
        native_socket_type& get () { pure_virtual (); return m_socket; }
        native_socket_type& operator-> () noexcept { return get(); }
    private:
        native_socket_type m_socket;
    };

    template <typename AsioObject>
    struct native_socket <AsioObject, typename boost::enable_if <boost::is_class <
        typename AsioObject::protocol_type::socket> >::type>
    {
        typedef typename AsioObject::protocol_type::socket native_socket_type;
        native_socket (Socket& peer)
            : m_socket (&peer.this_layer <native_socket_type> ()) { }
        native_socket_type& get () noexcept { return *m_socket; }
        native_socket_type& operator-> () noexcept { return *m_socket; }
    private:
        native_socket_type* m_socket;
    };

    //--------------------------------------------------------------------------

#if 0
    typedef typename boost::remove_reference <Object>::type next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;

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

#endif
    //--------------------------------------------------------------------------
    //
    // General
    //
    //--------------------------------------------------------------------------

    boost::asio::io_service& get_io_service ()
    {
        return get_object ().get_io_service ();
    }

    bool requires_handshake ()
    {
        return Has <SocketInterface::AnyHandshake>::value;
    }

    void* this_layer_raw (char const* type_name) const
    {
        char const* const this_type_name (typeid (ObjectType).name ());
        if (strcmp (type_name, this_type_name) == 0)
            return const_cast <void*> (static_cast <void const*>(m_impl));
        return nullptr;
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Close
    //
    //--------------------------------------------------------------------------

    boost::system::error_code close (boost::system::error_code& ec)
    {
        return close (ec,
            Has <SocketInterface::Close> ());
    }

    boost::system::error_code close (boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().close (ec);
    }

    boost::system::error_code close (boost::system::error_code& ec,
        boost::false_type)
    {
       return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Acceptor
    //
    //--------------------------------------------------------------------------

    boost::system::error_code accept (Socket& peer, boost::system::error_code& ec)
    {
        return accept (peer, ec,
            Has <SocketInterface::Acceptor> ());
    }

    boost::system::error_code accept (Socket& peer, boost::system::error_code& ec,
        boost::true_type)
    {
#if 1
        return get_object ().accept (
            native_socket <Object> (peer).get (), ec);
#else
        typedef ObjectType::protocol_type::socket socket_type;
        socket_type& socket (peer.this_layer <socket_type> ());
        return get_object ().accept (socket, ec);
#endif
    }

    boost::system::error_code accept (Socket&, boost::system::error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        return async_accept (peer, BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            Has <SocketInterface::Acceptor> ());
    }

    template <typename AcceptHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ErrorCall, void (boost::system::error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler,
        boost::true_type)
    {
#if 1
        return get_object ().async_accept (
            native_socket <Object> (peer).get (), handler);
#else
        typedef ObjectType::protocol_type::socket socket_type;
        socket_type& socket (peer.this_layer <socket_type> ());
        return get_object ().async_accept (socket, handler);
#endif
    }

    template <typename AcceptHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(AcceptHandler, void (boost::system::error_code))
    async_accept (Socket&, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            AcceptHandler, void (boost::system::error_code)> init(
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler), ec));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler), ec));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::LowestLayer
    //
    //--------------------------------------------------------------------------

    void* lowest_layer_raw (char const* type_name) const
    {
        return lowest_layer_raw (type_name,
            Has <SocketInterface::LowestLayer> ());
    }

    void* lowest_layer_raw (char const* type_name,
        boost::true_type) const
    {
        typedef typename ObjectType::lowest_layer_type lowest_layer_type;
        char const* const lowest_layer_type_name (typeid (lowest_layer_type).name ());
        if (strcmp (type_name, lowest_layer_type_name) == 0)
            return const_cast <void*> (static_cast <void const*>(&get_object ().lowest_layer ()));
        return nullptr;
    }

    void* lowest_layer_raw (char const*,
        boost::false_type) const
    {
        pure_virtual ();
        return nullptr;
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Socket
    //
    //--------------------------------------------------------------------------

    boost::system::error_code cancel (boost::system::error_code& ec)
    {
        return cancel (ec,
            Has <SocketInterface::Socket> ());
    }
   
    boost::system::error_code cancel (boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().cancel (ec);
    }

    boost::system::error_code cancel (boost::system::error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    boost::system::error_code shutdown (shutdown_type what, boost::system::error_code& ec)
    {
        return shutdown (what, ec,
            Has <SocketInterface::Socket> ());
    }

    boost::system::error_code shutdown (Socket::shutdown_type what, boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().shutdown (what, ec);
    }

    boost::system::error_code shutdown (Socket::shutdown_type, boost::system::error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Stream
    //
    //--------------------------------------------------------------------------

    std::size_t read_some (MutableBuffers const& buffers, boost::system::error_code& ec)
    {
        return read_some (buffers, ec,
            Has <SocketInterface::SyncStream> ());
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().read_some (buffers, ec);
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const&, boost::system::error_code& ec,
        boost::false_type)
    {
        pure_virtual (ec);
        return 0;
    }

    std::size_t write_some (ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        return write_some (buffers, ec,
            Has <SocketInterface::SyncStream> ());
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().write_some (buffers, ec);
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const&, boost::system::error_code& ec,
        boost::false_type)
    {
        pure_virtual (ec);
        return 0;
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return async_read_some (buffers, BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            Has <SocketInterface::AsyncStream> ());
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler,
        boost::true_type)
    {
        return get_object ().async_read_some (buffers,
            BOOST_ASIO_MOVE_CAST(ReadHandler)(handler));
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const&, BOOST_ASIO_MOVE_ARG(ReadHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ReadHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(ReadHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (BOOST_ASIO_MOVE_CAST(ReadHandler)(handler), ec, 0));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (BOOST_ASIO_MOVE_CAST(ReadHandler)(handler), ec, 0));
#endif
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return async_write_some (buffers, BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            Has <SocketInterface::AsyncStream> ());
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler,
        boost::true_type)
    {
        return get_object ().async_write_some (buffers,
            BOOST_ASIO_MOVE_CAST(WriteHandler)(handler));
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const&, BOOST_ASIO_MOVE_ARG(WriteHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            WriteHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(WriteHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(WriteHandler)(handler), ec, 0));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(WriteHandler)(handler), ec, 0));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Handshake
    //
    //--------------------------------------------------------------------------

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec)
    {
        return handshake (type, ec,
            Has <SocketInterface::SyncHandshake> ());
    }

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().handshake (type, ec);
    }

    boost::system::error_code handshake (handshake_type, boost::system::error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        return async_handshake (type, BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            Has <SocketInterface::AsyncHandshake> ());
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler,
        boost::true_type)
    {
        return get_object ().async_handshake (type,
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            HandshakeHandler, void (boost::system::error_code)> init(
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler), ec));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler), ec));
#endif
    }

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    boost::system::error_code handshake (handshake_type type,
        ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        return handshake (type, buffers, ec,
            Has <SocketInterface::BufferedSyncHandshake> ());
    }

    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type type,
        ConstBufferSequence const& buffers, boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().handshake (type, buffers, ec);
    }

    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type,
        ConstBufferSequence const&, boost::system::error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        return async_handshake (type, buffers,
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            Has <SocketInterface::BufferedAsyncHandshake> ());
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, const ConstBufferSequence& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler,
        boost::true_type)
    {
        return get_object ().async_handshake (type, buffers,
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type, const ConstBufferSequence&,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            BufferedHandshakeHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler), ec, 0));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler), ec, 0));
#endif
    }
#endif

    boost::system::error_code shutdown (boost::system::error_code& ec)
    {
        return shutdown (ec,
            Has <SocketInterface::SyncHandshake> ());
    }

    boost::system::error_code shutdown (boost::system::error_code& ec,
        boost::true_type)
    {
        return get_object ().shutdown (ec);
    }

    boost::system::error_code shutdown (boost::system::error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    void async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        async_shutdown (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            Has <SocketInterface::AsyncHandshake> ());
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler,
        boost::true_type)
    {
        return get_object ().async_shutdown (
            BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler));
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ShutdownHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler), ec));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler), ec));
#endif
    }

protected:
    explicit SocketWrapper (Object* object = nullptr) noexcept
        : m_impl (object)
    {
    }

    void set (Object* ptr) noexcept
    {
        m_impl = ptr;
    }

    template <typename Interface>
    struct Has : HasInterface <ObjectType, Interface> { };

public:
    Object* m_impl;
};

#endif
