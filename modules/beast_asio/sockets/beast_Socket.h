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

    Must satisfy these requirements:
    
        DefaultConstructible, MoveConstructible, CopyConstructible,
        MoveAssignable, CopyAssignable, Destructible

    Meets the requirements of these boost concepts:

        SyncReadStream, SyncWriteStream, AsyncReadStream, AsyncWriteStream,

    @see SharedObjectPtr
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
    // General
    //
    //--------------------------------------------------------------------------

    virtual boost::asio::io_service& get_io_service () = 0;

    /** Determines if the underlying stream requires a handshake.

        If requires_handshake is true, it will be necessary to call handshake or
        async_handshake after the connection is established. Furthermore it
        will be necessary to call the shutdown member from the
        HandshakeInterface to close the connection. Do not close the underlying
        socket or else the closure will not be graceful. Only one side should
        initiate the handshaking shutdon. The other side should observe it.
        Which side does what is up to the user.

        The default version returns false
    */
    virtual bool requires_handshake ();

    /** Retrieve the underlying object.
        Returns nullptr if the implementation doesn't match. Usually
        you will use this if you need to get at the underlying boost::asio
        object. For example:

        @code

        void set_options (Socket& socket)
        {
            typedef bost::boost::asio::ip::tcp Protocol;
            typedef Protocol::socket;
            Protocol::socket* const sock =
                socket.this_layer <Protocol::socket> ();

            if (sock != nullptr)
                sock->set_option (
                    Protocol::no_delay (true));
        }

        @endcode
    */
    template <class Object>
    Object& this_layer ()
    {
        Object* object (this_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object const& this_layer () const
    {
        Object const* object (this_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object* this_layer_ptr ()
    {
        return static_cast <Object*> (
            this_layer_raw (typeid (Object).name ()));
    }

    template <class Object>
    Object const* this_layer_ptr () const
    {
        return static_cast <Object const*> (
            this_layer_raw (typeid (Object).name ()));
    }

    // Shouldn't call this directly, use this_layer<> instead
    virtual void* this_layer_raw (char const* type_name) const = 0;

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Close
    //
    //--------------------------------------------------------------------------

    void close ()
    {
        boost::system::error_code ec;
        throw_error (close (ec));
    }

    virtual boost::system::error_code close (boost::system::error_code& ec);

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Acceptor
    //
    //--------------------------------------------------------------------------

    virtual boost::system::error_code accept (Socket& peer, boost::system::error_code& ec);

    template <class AcceptHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(AcceptHandler, void (boost::system::error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler)
    {
        return async_accept (peer, ErrorCall (
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(ErrorCall) handler);

    //--------------------------------------------------------------------------
    //
    // SocketInterface::LowestLayer
    //
    //--------------------------------------------------------------------------

    template <class Object>
    Object& lowest_layer ()
    {
        Object* object (lowest_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object const& lowest_layer () const
    {
        Object const* object (lowest_layer_ptr <Object> ());
        if (object == nullptr)
            Throw (std::bad_cast (), __FILE__, __LINE__);
        return *object;
    }

    template <class Object>
    Object* lowest_layer_ptr ()
    {
        return static_cast <Object*> (
            lowest_layer_raw (typeid (Object).name ()));
    }

    template <class Object>
    Object const* lowest_layer_ptr () const
    {
        return static_cast <Object const*> (
            lowest_layer_raw (typeid (Object).name ()));
    }

    // Shouldn't call this directly, use lowest_layer<> instead
    virtual void* lowest_layer_raw (char const* type_name) const;

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Socket
    //
    //--------------------------------------------------------------------------

    void cancel ()
    {
        boost::system::error_code ec;
        throw_error (cancel (ec));
    }

    virtual boost::system::error_code cancel (boost::system::error_code& ec);

    void shutdown (shutdown_type what)
    {
        boost::system::error_code ec;
        throw_error (shutdown (what, ec));
    }

    virtual boost::system::error_code shutdown (shutdown_type what,
        boost::system::error_code& ec);

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Stream
    //
    //--------------------------------------------------------------------------

    // SyncReadStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncReadStream.html
    //
    template <class MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers,
        boost::system::error_code& ec)
    {
        return read_some (MutableBuffers (buffers), ec);
    }

    virtual std::size_t read_some (MutableBuffers const& buffers, boost::system::error_code& ec);

    // SyncWriteStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/SyncWriteStream.html
    //
    template <class ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, boost::system::error_code &ec)
    {
        return write_some (BOOST_ASIO_MOVE_CAST(ConstBuffers)(ConstBuffers (buffers)), ec);
    }

    virtual std::size_t write_some (ConstBuffers const& buffers, boost::system::error_code& ec);

    // AsyncReadStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncReadStream.html
    //
    template <class MutableBufferSequence, class ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler)
    {
        return async_read_some (MutableBuffers (buffers),
            TransferCall (BOOST_ASIO_MOVE_CAST(ReadHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler);

    // AsyncWriteStream
    //
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/AsyncWriteStream.html
    //
    template <class ConstBufferSequence, class WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler)
    {
        return async_write_some (ConstBuffers (buffers),
            TransferCall(BOOST_ASIO_MOVE_CAST(WriteHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler);

    //--------------------------------------------------------------------------
    //
    // SocketInterface::Handshake
    //
    //--------------------------------------------------------------------------

    // ssl::stream::handshake (1 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload1.html
    //
    void handshake (handshake_type type)
    {
        boost::system::error_code ec;
        throw_error (handshake (type, ec));
    }

    // ssl::stream::handshake (2 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload2.html
    //
    virtual boost::system::error_code handshake (handshake_type type,
        boost::system::error_code& ec);

    // ssl::stream::async_handshake (1 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload1.html
    //
    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler)
    {
        return async_handshake (type,
            ErrorCall (BOOST_ASIO_MOVE_CAST(HandshakeHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler);

    //--------------------------------------------------------------------------

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
    // ssl::stream::handshake (3 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload3.html
    //
    template <class ConstBufferSequence>
    void handshake (handshake_type type, ConstBufferSequence const& buffers)
    {
        boost::system::error_code ec;
        throw_error (handshake (type, buffers, ec));
    }

    // ssl::stream::handshake (4 of 4)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/handshake/overload4.html
    //
    template <class ConstBufferSequence>
    boost::system::error_code handshake (handshake_type type,
        ConstBufferSequence const& buffers, boost::system::error_code& ec)
    {
        return handshake (type, ConstBuffers (buffers), ec);
    }

    virtual boost::system::error_code handshake (handshake_type type,
        ConstBuffers const& buffers, boost::system::error_code& ec);

    // ssl::stream::async_handshake (2 of 2)
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_handshake/overload2.html
    //
    template <class ConstBufferSequence, class BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, ConstBufferSequence const& buffers,
        BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler)
    {
        return async_handshake (type, ConstBuffers (buffers),
            TransferCall (BOOST_ASIO_MOVE_CAST(BufferedHandshakeHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) handler);
#endif

    //--------------------------------------------------------------------------

    // ssl::stream::shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/shutdown.html
    //
    void shutdown ()
    {
        boost::system::error_code ec;
        throw_error (shutdown (ec));
    }

    virtual boost::system::error_code shutdown (boost::system::error_code& ec);

    // ssl::stream::async_shutdown
    // http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference/ssl__stream/async_shutdown.html
    //
    template <class ShutdownHandler>
    void async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler)
    {
        return async_shutdown (ErrorCall (BOOST_ASIO_MOVE_CAST(ShutdownHandler)(handler)));
    }

    virtual
    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler);
};

#endif
