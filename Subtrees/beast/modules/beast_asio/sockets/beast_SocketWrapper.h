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

    Examples of the type of WrappedObject:

    asio::ip::tcp::socket
        arg must be an io_context
        SocketWrapper will create and take ownership of the tcp::socket
        WrappedObjectType will be tcp::socket
        next_layer () returns a asio::ip::tcp::socket&
        lowest_layer () returns a asio::ip::tcp::socket&

    asio::ip::tcp::socket&
        arg must be an existing socket&
        The caller owns the underlying socket object
        WrappedObjectType will be tcp::socket
        next_layer () returns a asio::ip::tcp::socket&
        lowest_layer () returns a asio::ip::tcp::socket&

    asio::ssl::stream <asio::ip::tcp::socket>
        arg must be an io_context
        SocketWrapper creates and takes ownership of the ssl::stream
        WrappedObjecType will be asio::ssl::stream <asio::ip::tcp::socket>
        next_layer () returns a asio::ip::tcp::socket&
        lowest_layer () returns a asio::ip::tcp::socket&

    asio::ssl::stream <asio::ip::tcp::socket&>
        arg must be an existing socket&
        The caller owns the socket, but SocketWrapper owns the ssl::stream
        WrappedObjectType will be asio::ssl::stream <asio::ip::tcp::socket&>
        next_layer () returns a asio::ip::tcp::socket&
        lowest_layer () returns a asio::ip::tcp::socket&

    asio::ssl::stream <asio::buffered_stream <asio::ip::tcp::socket> > >
        This makes my head explode
*/

//------------------------------------------------------------------------------

namespace SocketWrapperMemberChecks
{
    template <bool Enable>
    struct EnableIf : boost::false_type { };

    template <>
    struct EnableIf <true> : boost::true_type { };

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_get_io_service, get_io_service);

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_lowest_layer, lowest_layer);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_cancel, cancel);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_shutdown, shutdown);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_close, close);

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_accept, accept);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_accept, async_accept);

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_read_some, read_some);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_write_some, write_some);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_read_some, async_read_some);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_write_some, async_write_some);

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_handshake, handshake);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_handshake, async_handshake);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_shutdown, async_shutdown);

    // Extracts the underlying socket type from the protocol of another asio object
    template <typename T, typename Enable = void>
    struct native_socket
    {
        typedef void* socket_type;
        inline native_socket (Socket&) : m_socket (nullptr) { SocketBase::pure_virtual (); }
        inline socket_type& get () { SocketBase::pure_virtual (); return m_socket; }
        inline socket_type& operator-> () { return get (); }
    private:
        socket_type m_socket;
    };

    // Enabled if T::protocol_type::socket exists as a type
    template <typename T>
    struct native_socket <T, typename boost::enable_if <boost::is_class <
        typename T::protocol_type::socket> >::type>
    {
        typedef typename T::protocol_type::socket socket_type;
        inline native_socket (Socket& peer) : m_socket_ptr (&peer.native_handle <socket_type> ()) { }
        inline socket_type& get () noexcept { return *m_socket_ptr; }
        inline socket_type& operator-> () noexcept { return get (); }
    private:
        socket_type* m_socket_ptr;
    };
};

template <typename WrappedObject>
class SocketWrapper
    : public Socket
    , public Uncopyable
{
private:
    typedef typename boost::remove_reference <WrappedObject>::type wrapped_type;

public:
    typedef typename boost::remove_reference <WrappedObject>::type WrappedObjectType;

    template <typename Arg>
    explicit SocketWrapper (Arg& arg)
        : m_object (arg)
    {
    }

    template <typename Arg1, typename Arg2>
    SocketWrapper (Arg1& arg1, Arg2& arg2)
        : m_object (arg1, arg2)
    {
    }

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //

    boost::asio::io_service& get_io_service ()
    {
        using namespace SocketWrapperMemberChecks;
#if 0
        // This is the one that doesn't work, (void) arg lists
        return get_io_service (
            EnableIf <has_get_io_service <wrapped_type,
                io_service ()>::value> ());
#else
        return get_io_service (boost::true_type ());
#endif
    }

    boost::asio::io_service& get_io_service (
        boost::true_type)
    {
        return m_object.get_io_service ();
    }

    boost::asio::io_service& get_io_service (
        boost::false_type)
    {
        pure_virtual ();
        return *static_cast <boost::asio::io_service*>(nullptr);
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //

#if 0
    // This is a potential work-around for the problem with
    // the has_type_lowest_layer_type template, but requires
    // Boost 1.54 or later.
    //
    // This include will be needed:
    //
    // #include <boost/tti/has_type.hpp>
    //
    //
    BOOST_TTI_HAS_TYPE(lowest_layer_type)
#endif

    template <class T>
    struct has_type_lowest_layer_type
    {
        typedef char yes; 
        typedef struct {char dummy[2];} no;
        template <class C> static yes f(typename C::lowest_layer_type*);
        template <class C> static no f(...);
#ifdef _MSC_VER
        static bool const value = sizeof(f<T>(0)) == 1;
#else
        // This line fails to compile under Visual Studio 2012
        static bool const value = sizeof(has_type_lowest_layer_type<T>::f<T>(0)) == 1;
#endif
    }; 

    void* lowest_layer (char const* type_name) const
    {
        using namespace SocketWrapperMemberChecks;
        return lowest_layer (type_name,
            EnableIf <has_type_lowest_layer_type <wrapped_type>::value> ());
    }

    void* lowest_layer (char const* type_name,
        boost::true_type) const
    {
        char const* const name (typeid (typename wrapped_type::lowest_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object.lowest_layer ()));
        return nullptr;
    }

    void* lowest_layer (char const*,
        boost::false_type) const
    {
        pure_virtual ();
        return nullptr;
    }

    //--------------------------------------------------------------------------

    void* native_handle (char const* type_name) const
    {
        char const* const name (typeid (wrapped_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object));
        return nullptr;
    }

    //--------------------------------------------------------------------------

    error_code cancel (error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return cancel (ec,
            EnableIf <has_cancel <wrapped_type,
                error_code (error_code&)>::value> ());
    }
   
    error_code cancel (error_code& ec,
        boost::true_type)
    {
        return m_object.cancel (ec);
    }

    error_code cancel (error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------

    error_code shutdown (shutdown_type what, error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return shutdown (what, ec,
            EnableIf <has_shutdown <wrapped_type,
                error_code (shutdown_type, error_code&)>::value> ());
    }


    error_code shutdown (shutdown_type what, error_code& ec,
        boost::true_type)
    {
        return m_object.shutdown (what, ec);
    }

    error_code shutdown (shutdown_type, error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------

    error_code close (error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return close (ec,
            EnableIf <has_close <wrapped_type,
                error_code (error_code&)>::value> ());
    }

    error_code close (error_code& ec,
        boost::true_type)
    {
        return m_object.close (ec);
    }

    error_code close (error_code& ec,
        boost::false_type)
    {
       return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket_acceptor
    //

    error_code accept (Socket& peer, error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        typedef typename native_socket <wrapped_type>::socket_type socket_type;
        return accept (peer, ec,
            EnableIf <has_accept <wrapped_type,
                error_code (socket_type&, error_code&)>::value> ());
    }

    error_code accept (Socket& peer, error_code& ec,
        boost::true_type)
    {
        using namespace SocketWrapperMemberChecks;
        return m_object.accept (
            native_socket <wrapped_type> (peer).get (), ec);
    }

    error_code accept (Socket&, error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        using namespace SocketWrapperMemberChecks;
        typedef typename native_socket <wrapped_type>::socket_type socket_type;
        return async_accept (peer, BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            EnableIf <has_async_accept <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
                    (socket_type&, BOOST_ASIO_MOVE_ARG(TransferCall))>::value> ());
    }

    template <typename AcceptHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ErrorCall, void (error_code))
    async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler,
        boost::true_type)
    {
        using namespace SocketWrapperMemberChecks;
        return m_object.async_accept (
            native_socket <wrapped_type> (peer).get (),
                BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler));
    }

    template <typename AcceptHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(AcceptHandler, void (error_code))
    async_accept (Socket&, BOOST_ASIO_MOVE_ARG(AcceptHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            AcceptHandler, void (error_code)> init(
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler));
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler), ec));
        return init.result.get();
#else
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(AcceptHandler)(handler), ec));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    std::size_t read_some (MutableBuffers const& buffers, error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return read_some (buffers, ec,
            EnableIf <has_read_some <wrapped_type,
                std::size_t (MutableBuffers const&, error_code&)>::value> ());
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, error_code& ec,
        boost::true_type)
    {
        return m_object.read_some (buffers, ec);
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const&, error_code& ec,
        boost::false_type)
    {
        pure_virtual (ec);
        return 0;
    }

    //--------------------------------------------------------------------------

    std::size_t write_some (ConstBuffers const& buffers, error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return write_some (buffers, ec,
            EnableIf <has_write_some <wrapped_type,
                std::size_t (ConstBuffers const&, error_code&)>::value> ());
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, error_code& ec,
        boost::true_type)
    {
        return m_object.write_some (buffers, ec);
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const&, error_code& ec,
        boost::false_type)
    {
        pure_virtual (ec);
        return 0;
    }

    //--------------------------------------------------------------------------

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_read_some (MutableBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        using namespace SocketWrapperMemberChecks;
        return async_read_some (buffers, BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            EnableIf <has_async_read_some <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
                    (MutableBuffers const&, BOOST_ASIO_MOVE_ARG(TransferCall))>::value> ());
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (error_code, std::size_t))
    async_read_some (MutableBufferSequence const& buffers, BOOST_ASIO_MOVE_ARG(ReadHandler) handler,
        boost::true_type)
    {
        return m_object.async_read_some (buffers,
            BOOST_ASIO_MOVE_CAST(ReadHandler)(handler));
    }

    template <typename MutableBufferSequence, typename ReadHandler>
    BOOST_ASIO_INITFN_RESULT_TYPE(ReadHandler, void (error_code, std::size_t))
    async_read_some (MutableBufferSequence const&, BOOST_ASIO_MOVE_ARG(ReadHandler) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ReadHandler, void (error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(ReadHandler)(handler));
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (BOOST_ASIO_MOVE_CAST(ReadHandler)(handler), ec, 0));
        return init.result.get();
#else
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (BOOST_ASIO_MOVE_CAST(ReadHandler)(handler), ec, 0));
#endif
    }

    //--------------------------------------------------------------------------

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        using namespace SocketWrapperMemberChecks;
        return async_write_some (buffers, BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            EnableIf <has_async_write_some <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
                    (ConstBuffers const&, BOOST_ASIO_MOVE_ARG(TransferCall))>::value> ());
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_write_some (ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler,
        boost::true_type)
    {
        return m_object.async_write_some (buffers,
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_write_some (ConstBuffers const&, BOOST_ASIO_MOVE_ARG(TransferCall) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            TransferCall, void (error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), ec, 0));
        return init.result.get();
#else
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), ec, 0));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    bool requires_handshake ()
    {
        using namespace SocketWrapperMemberChecks;
        return
            has_handshake <wrapped_type,
                error_code (handshake_type, error_code&)>::value ||
            has_async_handshake <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
                    (handshake_type, BOOST_ASIO_MOVE_ARG(ErrorCall))>::value;
    }

    //--------------------------------------------------------------------------

    error_code handshake (handshake_type type, error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return handshake (type, ec,
            EnableIf <has_handshake <wrapped_type,
                error_code (handshake_type, error_code&)>::value> ());
    }

    error_code handshake (handshake_type type, error_code& ec,
        boost::true_type)
    {
        return m_object.handshake (type, ec);
    }

    error_code handshake (handshake_type, error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        using namespace SocketWrapperMemberChecks;
        return async_handshake (type, BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            EnableIf <has_async_handshake <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
                    (handshake_type, BOOST_ASIO_MOVE_ARG(ErrorCall))>::value> ());
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_handshake (handshake_type type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler,
        boost::true_type)
    {
        return m_object.async_handshake (type,
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_handshake (handshake_type, BOOST_ASIO_MOVE_ARG(ErrorCall) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ErrorCall, void (error_code)> init(
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec));
        return init.result.get();
#else
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec));
#endif
    }

    //--------------------------------------------------------------------------

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE

    error_code handshake (handshake_type type, ConstBuffers const& buffers, error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return handshake (type, buffers, ec,
            EnableIf <has_handshake <wrapped_type,
                error_code (handshake_type, ConstBuffers const&, error_code&)>::value> ());
    }

    error_code handshake (handshake_type type, ConstBuffers const& buffers, error_code& ec,
        boost::true_type)
    {
        return m_object.handshake (type, buffers, ec);
    }

    error_code handshake (handshake_type, ConstBuffers const&, error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(TransferCall) handler)
    {
        using namespace SocketWrapperMemberChecks;
        return async_handshake (type, buffers, BOOST_ASIO_MOVE_CAST(TransferCall)(handler),
            EnableIf <has_async_handshake <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
                    (handshake_type, ConstBuffers const&, error_code&)>::value> ());
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_handshake (handshake_type type, ConstBuffers const& buffers, BOOST_ASIO_MOVE_ARG(TransferCall) handler,
        boost::true_type)
    {
        return m_object.async_handshake (type, buffers,
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(TransferCall, void (error_code, std::size_t))
    async_handshake (handshake_type, ConstBuffers const&, BOOST_ASIO_MOVE_ARG(TransferCall) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            TransferCall, void (error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler));
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), ec, 0));
        return init.result.get();
#else
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(TransferCall)(handler), ec, 0));
#endif
    }
#endif

    //--------------------------------------------------------------------------

    error_code shutdown (error_code& ec)
    {
        using namespace SocketWrapperMemberChecks;
        return shutdown (ec,
            EnableIf <has_shutdown <wrapped_type,
                error_code (error_code&)>::value> ());
    }

    error_code shutdown (error_code& ec,
        boost::true_type)
    {
        return m_object.shutdown (ec);
    }

    error_code shutdown (error_code& ec,
        boost::false_type)
    {
        return pure_virtual (ec);
    }

    //--------------------------------------------------------------------------

    void async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler)
    {
        using namespace SocketWrapperMemberChecks;
        return async_shutdown (BOOST_ASIO_MOVE_CAST(ErrorCall)(handler),
            EnableIf <has_async_shutdown <wrapped_type,
                BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
                    (BOOST_ASIO_MOVE_ARG(ErrorCall))>::value> ());
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler,
        boost::true_type)
    {
        return m_object.async_shutdown (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
    }

    BEAST_ASIO_INITFN_RESULT_TYPE_MEMBER(ErrorCall, void (error_code))
    async_shutdown (BOOST_ASIO_MOVE_ARG(ErrorCall) handler,
        boost::false_type)
    {
#if BEAST_ASIO_HAS_FUTURE_RETURNS
        boost::asio::detail::async_result_init<
            ErrorCall, void (error_code, std::size_t)> init(
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler));
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec));
        return init.result.get();
#else
        error_code ec;
        ec = pure_virtual (ec);
        get_io_service ().post (boost::bind (
            BOOST_ASIO_MOVE_CAST(ErrorCall)(handler), ec));
#endif
    }

private:
    WrappedObject m_object;
};

#endif
