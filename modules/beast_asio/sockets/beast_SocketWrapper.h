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
    : public SocketWrapperBase
    , public virtual Socket
{
public:
    typedef Object ObjectType;
    typedef typename boost::remove_reference <Object>::type ObjectT;
    typedef typename boost::remove_reference <Object>::type next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
    typedef boost::asio::ssl::stream_base::handshake_type handshake_type;

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

    boost::asio::io_service& get_io_service () noexcept
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

    bool is_handshaked ()
    {
        return HasInterface <ObjectT, SocketInterface::Handshake>::value;
    }

    void* native_object_raw ()
    {
        return m_impl;
    }

    // Socket

    boost::system::error_code cancel (boost::system::error_code& ec)
    {
        return cancel (ec, HasInterface <ObjectT, SocketInterface::Socket> ());
    }
    
    boost::system::error_code shutdown (shutdown_type what, boost::system::error_code& ec)
    {
        return shutdown (what, ec, HasInterface <ObjectT, SocketInterface::Socket> ());
    }

    boost::system::error_code close (boost::system::error_code& ec)
    {
        return close (ec, HasInterface <ObjectT, SocketInterface::Socket> ());
    }

    // Stream

    std::size_t read_some_impl (Socket::MutableBuffers const& buffers,
        boost::system::error_code& ec)
    {
        return read_some (buffers, ec, HasInterface <ObjectT, SocketInterface::Stream> ());
    }

    std::size_t write_some_impl (Socket::ConstBuffers const& buffers,
        boost::system::error_code& ec)
    {
        return write_some (buffers, ec, HasInterface <ObjectT, SocketInterface::Stream> ());
    }

    void async_read_some_impl (Socket::MutableBuffers const& buffers,
        TransferCall const& call)
    {
        async_read_some (buffers, call, HasInterface <ObjectT, SocketInterface::Stream> ());
    }

    void async_write_some_impl (Socket::ConstBuffers const& buffers,
        TransferCall const& call)
    {
        async_write_some (buffers, call, HasInterface <ObjectT, SocketInterface::Stream> ());
    }

    // Handshake

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec)
    {
        return handshake (type, ec, HasInterface <ObjectT, SocketInterface::Handshake> ());
    }

#if (BOOST_VERSION / 100) >= 1054
    boost::system::error_code handshake_impl (handshake_type role,
        Socket::ConstBuffers const& buffers, boost::system::error_code& ec)
    {
        return handshake (role, buffers, ec, HasInterface <ObjectT, SocketInterface::Handshake> ());
    }
#endif

    void async_handshake_impl (handshake_type role, ErrorCall const& call)
    {
        async_handshake (role, call, HasInterface <ObjectT, SocketInterface::Handshake> ());
    }

#if (BOOST_VERSION / 100) >= 1054
    void async_handshake_impl (handshake_type role,
        Socket::ConstBuffers const& buffers, TransferCall const& call)
    {
        async_handshake (role, buffers, call, HasInterface <ObjectT, SocketInterface::Handshake> ());
    }
#endif

    boost::system::error_code shutdown (boost::system::error_code& ec)
    {
        return shutdown (ec, HasInterface <ObjectT, SocketInterface::Handshake> ());
    }

    void async_shutdown_impl (ErrorCall const& call)
    {
        async_shutdown (call, HasInterface <ObjectT, SocketInterface::Handshake> ());
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
    static boost::system::error_code fail (boost::system::error_code const& ec = boost::system::error_code ())
    {
        fatal_error ("pure virtual");
        return ec;
    }

    //--------------------------------------------------------------------------
    //
    // Socket
    //
    //--------------------------------------------------------------------------

    boost::system::error_code cancel (boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().cancel (ec);
    }

    boost::system::error_code cancel (boost::system::error_code&, boost::false_type)
    {
        return fail ();
    }

    boost::system::error_code shutdown (Socket::shutdown_type what, boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().shutdown (what, ec);
    }

    boost::system::error_code shutdown (Socket::shutdown_type, boost::system::error_code&, boost::false_type)
    {
        return fail ();
    }

    boost::system::error_code close (boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().close (ec);
    }

    boost::system::error_code close (boost::system::error_code&, boost::false_type)
    {
       return fail ();
    }

    //--------------------------------------------------------------------------
    //
    // Stream
    //
    //--------------------------------------------------------------------------

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().read_some (buffers, ec);
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const&, boost::system::error_code&, boost::false_type)
    {
        fail ();
        return 0;
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().write_some (buffers, ec);
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const&, boost::system::error_code&, boost::false_type)
    {
        fail ();
        return 0;
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler, boost::true_type)
    {
        get_object ().async_read_some (buffers, handler);
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (boost::system::error_code, std::size_t))
    async_read_some (MutableBufferSequence const&, BOOST_ASIO_MOVE_ARG(ReadHandler), boost::false_type)
    {
        fail ();
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(WriteHandler) handler, boost::true_type)
    {
        return get_object ().async_write_some (buffers, handler);
    }

    template <typename ConstBufferSequence, typename WriteHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(WriteHandler, void (boost::system::error_code, std::size_t))
    async_write_some (ConstBufferSequence const&, BOOST_ASIO_MOVE_ARG(WriteHandler), boost::false_type)
    {
        fail ();
    }

    //--------------------------------------------------------------------------
    //
    // Handshake
    //
    //--------------------------------------------------------------------------

    boost::system::error_code handshake (handshake_type type, boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().handshake (type, ec);
    }

    boost::system::error_code handshake (handshake_type, boost::system::error_code&, boost::false_type)
    {
        fail ();
        return boost::system::error_code ();
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type type, ConstBufferSequence const& buffers,
        boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().handshake (type, buffers, ec);
    }

    template <typename ConstBufferSequence>
    boost::system::error_code handshake (handshake_type, ConstBufferSequence const&,
        boost::system::error_code&, boost::false_type)
    {
        fail ();
        return boost::system::error_code ();
    }
#endif

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(HandshakeHandler) handler, boost::true_type)
    {
        return get_object ().async_handshake (type, handler);
    }

    template <typename HandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(HandshakeHandler, void (boost::system::error_code))
    async_handshake (handshake_type, BOOST_ASIO_MOVE_ARG(HandshakeHandler), boost::false_type)
    {
        fail ();
    }

#if (BOOST_VERSION / 100) >= 1054
    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type type, const ConstBufferSequence& buffers,
    BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler) handler, boost::true_type)
    {
        return get_object ().async_handshake (type, buffers, handler);
    }

    template <typename ConstBufferSequence, typename BufferedHandshakeHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(BufferedHandshakeHandler, void (boost::system::error_code, std::size_t))
    async_handshake (handshake_type, const ConstBufferSequence&,
    BOOST_ASIO_MOVE_ARG(BufferedHandshakeHandler), boost::false_type)
    {
        fail ();
    }
#endif

    boost::system::error_code shutdown (boost::system::error_code& ec, boost::true_type)
    {
        return get_object ().shutdown (ec);
    }

    boost::system::error_code shutdown (boost::system::error_code&, boost::false_type)
    {
        fail ();
        return boost::system::error_code ();
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler) handler, boost::true_type)
    {
        return get_object ().async_shutdown (handler);
    }

    template <typename ShutdownHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ShutdownHandler, void (boost::system::error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ShutdownHandler), boost::false_type)
    {
        fail ();
    }

private:
    Object* m_impl;
};

#endif
