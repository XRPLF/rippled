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

#ifndef BEAST_SOCKET_H_INCLUDED
#define BEAST_SOCKET_H_INCLUDED

/** A high level socket abstraction.

    This combines the capabilities of multiple socket interfaces such
    as listening, connecting, streaming, and handshaking. It brings
    everything together into a single abstract interface.

    When member functions are called and the underlying implementation does
    not support the operation, a fatal error is generated.
*/
class Socket
    : public SocketBase
    , public boost::asio::ssl::stream_base
    , public boost::asio::socket_base
{
protected:
    typedef boost::system::error_code error_code;

public:
    virtual ~Socket ();

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //

    virtual boost::asio::io_service& get_io_service ();

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //

    /** Retrieve the lowest layer object.
        Note that you must know the type name for this to work, or
        else a fatal error will occur.
    */
    /** @{ */
    template <class Object>
    Object& lowest_layer ()
    {
        Object* object (this->lowest_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object const& lowest_layer () const
    {
        Object const* object (this->lowest_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object* lowest_layer_ptr ()
    {
        return static_cast <Object*> (
            this->lowest_layer (typeid (Object).name ()));
    }

    template <class Object>
    Object const* lowest_layer_ptr () const
    {
        return static_cast <Object const*> (
            this->lowest_layer (typeid (Object).name ()));
    }
    /** @} */

    virtual void* lowest_layer (char const* type_name) const;

    /** Retrieve the underlying object.
        Note that you must know the type name for this to work, or
        else a fatal error will occur.
    */
    /** @{ */
    template <class Object>
    Object& native_handle ()
    {
        Object* object (this->native_handle_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object const& native_handle () const
    {
        Object const* object (this->native_handle_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object* native_handle_ptr ()
    {
        return static_cast <Object*> (
            this->native_handle (typeid (Object).name ()));
    }

    template <class Object>
    Object const* native_handle_ptr () const
    {
        return static_cast <Object const*> (
            this->native_handle (typeid (Object).name ()));
    }
    /** @} */

    virtual void* native_handle (char const* type_name) const;

    void cancel ()
    {
        error_code ec;
        throw_error (cancel (ec));
    }

    virtual error_code cancel (error_code& ec);

    void shutdown (shutdown_type what)
    {
        error_code ec;
        throw_error (shutdown (what, ec));
    }

    virtual error_code shutdown (shutdown_type what,
        error_code& ec);

    void close ()
    {
        error_code ec;
        throw_error (close (ec));
    }

    virtual error_code close (error_code& ec);

    //--------------------------------------------------------------------------
    //
    // basic_socket_acceptor
    //

    virtual error_code accept (Socket& peer, error_code& ec);

    template <class AcceptHandler>
    BEAST_ASIO_INITFN_RESULT_TYPE(AcceptHandler, void (error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler)
    {
        return async_accept (peer, HandlerCall (HandlerCall::Accept (),
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code))
    async_accept (Socket& peer, HandlerCall const& handler);

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    // SyncReadStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncReadStream.html
    template <class MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers,
        error_code& ec)
    {
        return read_some (MutableBuffers (buffers), ec);
    }

    virtual std::size_t read_some (MutableBuffers const& buffers, error_code& ec);

    // SyncWriteStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncWriteStream.html
    template <class ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, error_code &ec)
    {
        return write_some (ConstBuffers (buffers), ec);
    }

    virtual std::size_t write_some (ConstBuffers const& buffers, error_code& ec);

    // AsyncReadStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncReadStream.html
    template <class MutableBufferSequence, class ReadHandler>
    BEAST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler)
    {
        return async_read_some (MutableBuffers (buffers),
            HandlerCall (HandlerCall::Transfer (),
                BOOST_ASIO_MOVE_CAST(ReadHandler)(handler))); 
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, HandlerCall const& handler);

    // AsyncWriteStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncWriteStream.html
    template <class ConstBufferSequence, class WriteHandler>
    BEAST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        return async_write_some (ConstBuffers (buffers),
            HandlerCall (HandlerCall::Transfer (),
                BOOST_ASIO_MOVE_CAST(WriteHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, HandlerCall const& handler);

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    /** Determines if the underlying stream requires a handshake.

        If needs_handshake is true, it will be necessary to call handshake or
        async_handshake after the connection is established. Furthermore it
        will be necessary to call the shutdown member from the
        HandshakeInterface to close the connection. Do not close the underlying
        socket or else the closure will not be graceful. Only one side should
        initiate the handshaking shutdon. The other side should observe it.
        Which side does what is up to the user.

        The default version returns false.
    */
    virtual bool needs_handshake ();

    // ssl::stream::handshake (1 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload1.html
    //
    void handshake (handshake_type type)
    {
        error_code ec;
        throw_error (handshake (type, ec));
    }

    // ssl::stream::handshake (2 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload2.html
    //
    virtual error_code handshake (handshake_type type,
        error_code& ec);

    // ssl::stream::async_handshake (1 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload1.html
    //
    template <typename HandshakeHandler>
    BEAST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        return async_handshake (type, HandlerCall (HandlerCall::Error (),
                BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code))
    async_handshake (handshake_type type, HandlerCall const& handler);

    //--------------------------------------------------------------------------

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // ssl::stream::handshake (3 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload3.html
    //
    template <class ConstBufferSequence>
    void handshake (handshake_type type, ConstBufferSequence const& buffers)
    {
        error_code ec;
        throw_error (handshake (type, buffers, ec));
    }

    // ssl::stream::handshake (4 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload4.html
    //
    template <class ConstBufferSequence>
    error_code handshake (handshake_type type,
        ConstBufferSequence const& buffers, error_code& ec)
    {
        return handshake (type, ConstBuffers (buffers), ec);
    }

    virtual error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec);

    // ssl::stream::async_handshake (2 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload2.html
    //
    template <class ConstBufferSequence, class BufferedHandshakeHandler>
    BEAST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (error_code, std::size_t))
    async_handshake (handshake_type type, ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        return async_handshake (type, ConstBuffers (buffers),
            HandlerCall (HandlerCall::Transfer (),
                BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers, HandlerCall const& handler);
#endif

    //--------------------------------------------------------------------------

    // ssl::stream::shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/shutdown.html
    //
    void shutdown ()
    {
        error_code ec;
        throw_error (shutdown (ec));
    }

    virtual error_code shutdown (error_code& ec);

    // ssl::stream::async_shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_shutdown.html
    //
    template <class ShutdownHandler>
    void async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler)
    {
        return async_shutdown (HandlerCall (HandlerCall::Error (),
            BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(HandlerCall, void (error_code))
    async_shutdown (HandlerCall const& handler);
};

#endif
