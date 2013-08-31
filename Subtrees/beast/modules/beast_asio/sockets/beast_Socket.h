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

#ifndef BEAST_ASIO_SOCKETS_SOCKET_H_INCLUDED
#define BEAST_ASIO_SOCKETS_SOCKET_H_INCLUDED

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
public:
    virtual ~Socket ();

    //--------------------------------------------------------------------------
    //
    // Socket
    //

    /** Retrieve the underlying object.

        @note If the type doesn't match, nullptr is returned or an
              exception is thrown if trying to acquire a reference.
    */
    /** @{ */
    template <typename Object>
    Object& this_layer ()
    {
        Object* object (this->this_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <typename Object>
    Object const& this_layer () const
    {
        Object const* object (this->this_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <typename Object>
    Object* this_layer_ptr ()
    {
        return static_cast <Object*> (
            this->this_layer_ptr (typeid (Object).name ()));
    }

    template <typename Object>
    Object const* this_layer_ptr () const
    {
        return static_cast <Object const*> (
            this->this_layer_ptr (typeid (Object).name ()));
    }
    /** @} */

    virtual void* this_layer_ptr (char const* type_name) const
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // native_handle
    //

    /** Retrieve the native representation of the object.

        Since we dont know the return type, and because almost every
        asio implementation passes the result by value, you need to provide
        a pointer to a default-constructed object of the matching type.

        @note If the type doesn't match, an exception is thrown.
    */
    template <typename Handle>
    void native_handle (Handle* dest)
    {
        if (! native_handle (typeid (Handle).name (), dest))
            Throw (std::bad_cast (), __FILE__, __LINE__);
    }

    virtual bool native_handle (char const* type_name, void* dest)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //

    virtual boost::asio::io_service& get_io_service ()
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //

    /** Retrieve the lowest layer object.

        @note If the type doesn't match, nullptr is returned or an
              exception is thrown if trying to acquire a reference.
    */
    /** @{ */
    template <typename Object>
    Object& lowest_layer ()
    {
        Object* object (this->lowest_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <typename Object>
    Object const& lowest_layer () const
    {
        Object const* object (this->lowest_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <typename Object>
    Object* lowest_layer_ptr ()
    {
        return static_cast <Object*> (
            this->lowest_layer_ptr (typeid (Object).name ()));
    }

    template <typename Object>
    Object const* lowest_layer_ptr () const
    {
        return static_cast <Object const*> (
            this->lowest_layer_ptr (typeid (Object).name ()));
    }
    /** @} */

    virtual void* lowest_layer_ptr (char const* type_name) const
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------

    void cancel ()
    {
        error_code ec;
        throw_error (cancel (ec), __FILE__, __LINE__);
    }

    virtual error_code cancel (error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    void shutdown (shutdown_type what)
    {
        error_code ec;
        throw_error (shutdown (what, ec), __FILE__, __LINE__);
    }

    virtual error_code shutdown (shutdown_type what,
        error_code& ec)
            BEAST_SOCKET_VIRTUAL;

    void close ()
    {
        error_code ec;
        throw_error (close (ec), __FILE__, __LINE__);
    }

    virtual error_code close (error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_socket_acceptor
    //

    virtual error_code accept (Socket& peer, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    template <typename AcceptHandler>
    void async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler)
    {
        return async_accept (peer,
            newAcceptHandler (BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler)));
    }

    virtual void async_accept (Socket& peer, SharedHandlerPtr handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    // SyncReadStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncReadStream.html
    //
    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers,
        error_code& ec)
    {
        return read_some (MutableBuffers (buffers), ec);
    }

    virtual std::size_t read_some (MutableBuffers const& buffers, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    // SyncWriteStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncWriteStream.html
    //
    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, error_code &ec)
    {
        return write_some (ConstBuffers (buffers), ec);
    }

    virtual std::size_t write_some (ConstBuffers const& buffers, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    // AsyncReadStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncReadStream.html
    //
    template <typename MutableBufferSequence, typename ReadHandler>
    void async_read_some (MutableBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(ReadHandler) handler)
    {
        return async_read_some (MutableBuffers (buffers),
            newReadHandler (BOOST_ASIO_MOVE_CAST(ReadHandler)(handler)));
    }

    virtual void async_read_some (MutableBuffers const& buffers, SharedHandlerPtr handler)
        BEAST_SOCKET_VIRTUAL;

    // AsyncWriteStream
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncWriteStream.html
    //
    template <typename ConstBufferSequence, typename WriteHandler>
    void async_write_some (ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        return async_write_some (ConstBuffers (buffers),
            newWriteHandler (BOOST_ASIO_MOVE_CAST(WriteHandler)(handler)));
    }

    virtual void async_write_some (ConstBuffers const& buffers, SharedHandlerPtr handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    /** Retrieve the next layer object.

        @note If the type doesn't match, nullptr is returned or an
              exception is thrown if trying to acquire a reference.
    */
    /** @{ */
    template <typename Object>
    Object& next_layer ()
    {
        Object* object (this->next_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <typename Object>
    Object const& next_layer () const
    {
        Object const* object (this->next_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <typename Object>
    Object* next_layer_ptr ()
    {
        return static_cast <Object*> (
            this->next_layer_ptr (typeid (Object).name ()));
    }

    template <typename Object>
    Object const* next_layer_ptr () const
    {
        return static_cast <Object const*> (
            this->next_layer_ptr (typeid (Object).name ()));
    }
    /** @} */

    virtual void* next_layer_ptr (char const* type_name) const
        BEAST_SOCKET_VIRTUAL;

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
    virtual bool needs_handshake ()
        BEAST_SOCKET_VIRTUAL;

    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__verify_mode.html
    //
    virtual void set_verify_mode (int verify_mode) = 0;

    // ssl::stream::handshake (1 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload1.html
    //
    void handshake (handshake_type type)
    {
        error_code ec;
        throw_error (handshake (type, ec), __FILE__, __LINE__);
    }

    // ssl::stream::handshake (2 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload2.html
    //
    virtual error_code handshake (handshake_type type, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    // ssl::stream::async_handshake (1 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload1.html
    //
    template <typename HandshakeHandler>
    void async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        return async_handshake (type,
                newHandshakeHandler (BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler)));
    }

    virtual void async_handshake (handshake_type type, SharedHandlerPtr handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // ssl::stream::handshake (3 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload3.html
    //
    template <typename ConstBufferSequence>
    void handshake (handshake_type type, ConstBufferSequence const& buffers)
    {
        error_code ec;
        throw_error (handshake (type, buffers, ec), __FILE__, __LINE__);
    }

    // ssl::stream::handshake (4 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload4.html
    //
    template <typename ConstBufferSequence>
    error_code handshake (handshake_type type,
        ConstBufferSequence const& buffers, error_code& ec)
    {
        return handshake (type, ConstBuffers (buffers), ec);
    }

    virtual error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec)
            BEAST_SOCKET_VIRTUAL;

    // ssl::stream::async_handshake (2 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload2.html
    //
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    void async_handshake (handshake_type type, ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        return async_handshake (type, ConstBuffers (buffers),
            newBufferedHandshakeHandler (BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler)));
    }

    virtual void async_handshake (handshake_type type, ConstBuffers const& buffers,
        SharedHandlerPtr handler)
            BEAST_SOCKET_VIRTUAL;
#endif

    //--------------------------------------------------------------------------

    // ssl::stream::shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/shutdown.html
    //
    void shutdown ()
    {
        error_code ec;
        throw_error (shutdown (ec), __FILE__, __LINE__);
    }

    virtual error_code shutdown (error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    // ssl::stream::async_shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_shutdown.html
    //
    template <typename ShutdownHandler>
    void async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler)
    {
        return async_shutdown (
            newShutdownHandler (BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler)));
    }

    virtual void async_shutdown (SharedHandlerPtr handler)
        BEAST_SOCKET_VIRTUAL;
};

#endif
