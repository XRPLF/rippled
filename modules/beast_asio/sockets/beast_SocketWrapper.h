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
    : public SocketInterface
    , public virtual Socket
{
public:
    typedef Object ObjectType;
    typedef typename boost::remove_reference <Object>::type ObjectT;

    SocketWrapper (Object& object) noexcept
        : m_impl (&object)
        //, m_next_layer (object.next_layer ())
        //, m_lowest_layer (object.lowest_layer ())
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

    boost::asio::io_service& get_io_service () noexcept
    {
        return get_object ().get_io_service ();
    }

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

    // General

    bool is_handshaked ()
    {
        return HasInterface <ObjectT, SocketInterface::Handshake>::value;
    }

    void* native_object_raw ()
    {
        return m_impl;
    }

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Socket
    //
    //--------------------------------------------------------------------------

public:
    boost::system::error_code cancel (boost::system::error_code& ec)
    {
        return cancel (ec,
            HasInterface <ObjectT, SocketInterface::Socket> ());
    }
   
private:
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

public:
    boost::system::error_code shutdown (shutdown_type what, boost::system::error_code& ec)
    {
        return shutdown (what, ec,
            HasInterface <ObjectT, SocketInterface::Socket> ());
    }

private:
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

public:
    boost::system::error_code close (boost::system::error_code& ec)
    {
        return close (ec,
            HasInterface <ObjectT, SocketInterface::Socket> ());
    }

private:
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
    // SocketInterface::Stream
    //
    //--------------------------------------------------------------------------

public:
    std::size_t read_some (BOOST_ASIO_MOVE_ARG(MutableBuffers) buffers,
        boost::system::error_code& ec)
    {
        return read_some (buffers, ec,
            HasInterface <ObjectT, SocketInterface::SyncStream> ());
    }

private:
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

public:
    std::size_t write_some (BOOST_ASIO_MOVE_ARG(ConstBuffers) buffers, boost::system::error_code& ec)
    {
        return write_some (buffers, ec,
            HasInterface <ObjectT, SocketInterface::SyncStream> ());
    }

private:
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

public:
    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_read_some (BOOST_ASIO_MOVE_ARG(MutableBuffers) buffers, BOOST_ASIO_MOVE_ARG(TransferCall) call)
    {
        return async_read_some (buffers, call,
            HasInterface <ObjectT, SocketInterface::AsyncStream> ());
    }

private:
    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler,
        boost::true_type)
    {
        return get_object ().async_read_some (buffers, handler);
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const&, BOOST_ASIO_MOVE_ARG(ReadHandler) handler,
        boost::false_type)
    {
#if BOOST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ReadHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(ReadHandler)(handler));
        system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec, 0));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec, 0));
#endif
    }

public:
    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_write_some (BOOST_ASIO_MOVE_ARG(ConstBuffers) buffers, BOOST_ASIO_MOVE_ARG(TransferCall) call)
    {
        return async_write_some (buffers, call,
            HasInterface <ObjectT, SocketInterface::AsyncStream> ());
    }

private:
    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler,
        boost::true_type)
    {
        return get_object ().async_write_some (buffers, handler);
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const&, BOOST_ASIO_MOVE_ARG(WriteHandler) handler,
        boost::false_type)
    {
#if BOOST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            WriteHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(WriteHandler)(handler));
        system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec, 0));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec, 0));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // Handshake
    //
    //--------------------------------------------------------------------------

public:
    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec)
    {
        return handshake (type, ec,
            HasInterface <ObjectT, SocketInterface::SyncHandshake> ());
    }

private:
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

public:
    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) call)
    {
        return async_handshake (type, call,
            HasInterface <ObjectT, SocketInterface::AsyncHandshake> ());
    }

private:
    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler,
        boost::true_type)
    {
        return get_object ().async_handshake (type, handler);
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler,
        boost::false_type)
    {
#if BOOST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            HandshakeHandler, void (boost::system::error_code)> init(
            BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler));
        system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec));
#endif
    }

public:
#if BOOST_ASIO_HAS_BUFFEREDHANDSHAKE
    boost::system::error_code handshake (handshake_type type,
        BOOST_ASIO_MOVE_ARG(ConstBuffers) buffers, boost::system::error_code& ec)
    {
        return handshake (type, buffers, ec,
            HasInterface <ObjectT, SocketInterface::SyncBufferedHandshake> ());
    }

private:
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

public:
    BOOST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ConstBuffers) buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) call)
    {
        return async_handshake (type, buffers, call,
            HasInterface <ObjectT, SocketInterface::AsyncBufferedHandshake> ());
    }

private:
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, const ConstBufferSequence& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler,
        boost::true_type)
    {
        return get_object ().async_handshake (type, buffers, handler);
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type, const ConstBufferSequence&,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler,
        boost::false_type)
    {
#if BOOST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            BufferedHandshakeHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler));
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec, 0));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec, 0));
#endif
    }
#endif

public:
    boost::system::error_code shutdown (boost::system::error_code& ec)
    {
        return shutdown (ec,
            HasInterface <ObjectT, SocketInterface::SyncHandshake> ());
    }

private:
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

public:
    void async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) call)
    {
        async_shutdown (call,
            HasInterface <ObjectT, SocketInterface::AsyncHandshake> ());
    }

private:
    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler,
        boost::true_type)
    {
        return get_object ().async_shutdown (handler);
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler,
        boost::false_type)
    {
#if BOOST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ShutdownHandler, void (boost::system::error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler));
        system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec));
        return init.result.get();
#else
        boost::system::error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (handler, ec));
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

private:
    static void pure_virtual ()
    {
        fatal_error ("A beast::Socket function was called on an object that doesn't support the interface");
    }

    static boost::system::error_code pure_virtual (boost::system::error_code& ec)
    {
        pure_virtual ();
        return ec = boost::system::errc::make_error_code (
            boost::system::errc::function_not_supported);
    }

private:
    Object* m_impl;
};

#endif
