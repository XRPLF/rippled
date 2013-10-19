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

#ifndef BEAST_ASIO_SOCKETS_SOCKETWRAPPER_H_INCLUDED
#define BEAST_ASIO_SOCKETS_SOCKETWRAPPER_H_INCLUDED

/** Wraps a reference to any object and exports all availble interfaces.

    If the object does not support an interface, calling those
    member functions will behave as if a pure virtual was called.

    Note that only a reference to the underlying is stored. Management
    of the lifetime of the object is controlled by the caller.

    Examples of the type of Object:

    asio::ip::tcp::socket
    asio::ip::tcp::socket&
    asio::ssl::stream <asio::ip::tcp::socket>
    asio::ssl::stream <asio::ip::tcp::socket&>
        explain arg must be an io_context
        explain SocketWrapper will create and take ownership of the tcp::socket
        explain this_layer_type will be tcp::socket
        explain next_layer () returns a asio::ip::tcp::socket&
        explain lowest_layer () returns a asio::ip::tcp::socket&

    asio::ssl::stream <asio::buffered_stream <asio::ip::tcp::socket> > >
        This makes my head explode
*/

//------------------------------------------------------------------------------

namespace detail
{

namespace SocketWrapperMemberChecks
{
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

    BEAST_DEFINE_IS_CALL_POSSIBLE(has_set_verify_mode, set_verify_mode);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_handshake, handshake);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_handshake, async_handshake);
    BEAST_DEFINE_IS_CALL_POSSIBLE(has_async_shutdown, async_shutdown);

    // Extracts the underlying socket type from the protocol of another asio object
    template <typename T, typename Enable = void>
    struct native_socket
    {
        typedef void* socket_type;
        inline native_socket (Socket&)
            : m_socket (nullptr)
        {
            SocketBase::pure_virtual_called (__FILE__, __LINE__);
        }
        inline socket_type& get ()
        {
            SocketBase::pure_virtual_called (__FILE__, __LINE__);
            return m_socket;
        }
        inline socket_type& operator-> ()
        {
            return get ();
        }
    private:
        socket_type m_socket;
    };

    // Enabled if T::protocol_type::socket exists as a type
    template <typename T>
    struct native_socket <T, typename boost::enable_if <boost::is_class <
        typename T::protocol_type::socket> >::type>
    {
        typedef typename T::protocol_type::socket socket_type;
        inline native_socket (Socket& peer)
            : m_socket_ptr (&peer.this_layer <socket_type> ())
        {
        }
        inline socket_type& get () noexcept
        {
            return *m_socket_ptr;
        }
        inline socket_type& operator-> () noexcept
        {
            return get ();
        }
    private:
        socket_type* m_socket_ptr;
    };
}

}

//------------------------------------------------------------------------------

template <typename Object>
class SocketWrapper
    : public Socket
    , public Uncopyable
{
public:
    // Converts a static bool constexpr member named 'value' into
    // an IntegralConstant for SFINAE overload resolution.
    //
    template <class Cond>
    struct Enabled : public IntegralConstant <bool, Cond::value> { };

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
    // SocketWrapper
    //

    /** The type of the object being wrapped. */
    typedef typename boost::remove_reference <Object>::type this_layer_type;

    /** Get a reference to this layer. */
    this_layer_type& this_layer () noexcept
    {
        return m_object;
    }

    /** Get a const reference to this layer. */
    this_layer_type const& this_layer () const noexcept
    {
        return m_object;
    }

    //--------------------------------------------------------------------------
    //
    // Socket
    //

    void* this_layer_ptr (char const* type_name) const
    {
        char const* const name (typeid (this_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object));
        return nullptr;
    }

    //--------------------------------------------------------------------------
    //
    // native_handle
    //

#if 0
    // This is a potential work-around for the problem with
    // the has_type_native_handle_type template, but requires
    // Boost 1.54 or later.
    //
    // This include will be needed:
    //
    // boost/tti/has_type.hpp
    //
    //
    BOOST_TTI_HAS_TYPE(native_handle_type)

#else
    template <class T>
    struct has_type_native_handle_type
    {
        typedef char yes; 
        typedef struct {char dummy[2];} no;
        template <class C> static yes f(typename C::native_handle_type*);
        template <class C> static no f(...);
#ifdef _MSC_VER
        static bool const value = sizeof(f<T>(0)) == 1;
#else
        // This line fails to compile under Visual Studio 2012
        static bool const value = sizeof(has_type_native_handle_type<T>::f<T>(0)) == 1;
#endif
    }; 

#endif

    template <typename T, bool Exists = has_type_native_handle_type <T>::value >
    struct extract_native_handle_type
    {
        typedef typename T::native_handle_type type;
    };

    template <typename T>
    struct extract_native_handle_type <T, false>
    {
        typedef void type;
    };

    // This will be void if native_handle_type doesn't exist in Object
    typedef typename extract_native_handle_type <this_layer_type>::type native_handle_type;

    bool native_handle (char const* type_name, void* dest)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return native_handle (type_name, dest,
            Enabled <has_type_native_handle_type <this_layer_type> > ());
    }

    bool native_handle (char const* type_name, void* dest,
        TrueType)
    {
        char const* const name (typeid (typename this_layer_type::native_handle_type).name ());
        if (strcmp (name, type_name) == 0)
        {
            native_handle_type* const p (reinterpret_cast <native_handle_type*> (dest));
            *p = m_object.native_handle ();
            return true;
        }
        return false;
    }

    bool native_handle (char const*, void*,
        FalseType)
    {
        pure_virtual_called (__FILE__, __LINE__);
        return false;
    }

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //

    boost::asio::io_service& get_io_service ()
    {
#if 0
        // Apparently has_get_io_service always results in false
        using namespace detail::SocketWrapperMemberChecks;
        return get_io_service (
            Enabled <has_get_io_service <this_layer_type,
                boost::asio::io_service&()> > ());
#else
        return get_io_service (TrueType ());
#endif
    }

    boost::asio::io_service& get_io_service (
        TrueType)
    {
        return m_object.get_io_service ();
    }

    boost::asio::io_service& get_io_service (
        FalseType)
    {
        pure_virtual_called (__FILE__, __LINE__);
        return *static_cast <boost::asio::io_service*>(nullptr);
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //

    /*
    To forward the lowest_layer_type type, we need to make sure it
    exists in Object. This is a little more tricky than just figuring
    out if Object has a particular member function.

    The problem is boost::asio::basic_socket_acceptor, which doesn't
    have lowest_layer () or lowest_layer_type ().
    */

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

    template <typename T, bool Exists = has_type_lowest_layer_type <T>::value >
    struct extract_lowest_layer_type
    {
        typedef typename T::lowest_layer_type type;
    };

    template <typename T>
    struct extract_lowest_layer_type <T, false>
    {
        typedef void type;
    };

    // This will be void if lowest_layer_type doesn't exist in Object
    typedef typename extract_lowest_layer_type <this_layer_type>::type lowest_layer_type;

    void* lowest_layer_ptr (char const* type_name) const
    {
        using namespace detail::SocketWrapperMemberChecks;
        return lowest_layer_ptr (type_name,
            Enabled <has_type_lowest_layer_type <this_layer_type> > ());
    }

    void* lowest_layer_ptr (char const* type_name,
        TrueType) const
    {
        char const* const name (typeid (typename this_layer_type::lowest_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object.lowest_layer ()));
        return nullptr;
    }

    void* lowest_layer_ptr (char const*,
        FalseType) const
    {
        pure_virtual_called (__FILE__, __LINE__);
        return nullptr;
    }

    //--------------------------------------------------------------------------

    error_code cancel (error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return cancel (ec,
            Enabled <has_cancel <this_layer_type,
                error_code (error_code&)> > ());
    }
   
    error_code cancel (error_code& ec,
        TrueType)
    {
        return m_object.cancel (ec);
    }

    error_code cancel (error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    error_code shutdown (shutdown_type what, error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return shutdown (what, ec,
            Enabled <has_shutdown <this_layer_type,
                error_code (shutdown_type, error_code&)> > ());
    }


    error_code shutdown (shutdown_type what, error_code& ec,
        TrueType)
    {
        return m_object.shutdown (what, ec);
    }

    error_code shutdown (shutdown_type, error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    error_code close (error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return close (ec,
            Enabled <has_close <this_layer_type,
                error_code (error_code&)> > ());
    }

    error_code close (error_code& ec,
        TrueType)
    {
        return m_object.close (ec);
    }

    error_code close (error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket_acceptor
    //

    error_code accept (Socket& peer, error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        typedef typename native_socket <this_layer_type>::socket_type socket_type;
        return accept (peer, ec,
            Enabled <has_accept <this_layer_type,
                error_code (socket_type&, error_code&)> > ());
    }

    error_code accept (Socket& peer, error_code& ec,
        TrueType)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return m_object.accept (
            native_socket <this_layer_type> (peer).get (), ec);
    }

    error_code accept (Socket&, error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    void async_accept (Socket& peer, SharedHandlerPtr handler)
    {
        using namespace detail::SocketWrapperMemberChecks;
        typedef typename native_socket <this_layer_type>::socket_type socket_type;
        async_accept (peer, BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler),
            Enabled <has_async_accept <this_layer_type,
                void (socket_type&, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))> > ());
    }

    void async_accept (Socket& peer, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
        TrueType)
    {
        using namespace detail::SocketWrapperMemberChecks;
        m_object.async_accept (
            native_socket <this_layer_type> (peer).get (),
                BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
    }

    void async_accept (Socket&, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
        FalseType)
    {
        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (pure_virtual_error ());
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //

    std::size_t read_some (MutableBuffers const& buffers, error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return read_some (buffers, ec,
            Enabled <has_read_some <this_layer_type,
                std::size_t (MutableBuffers const&, error_code&)> > ());
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const& buffers, error_code& ec,
        TrueType)
    {
        return m_object.read_some (buffers, ec);
    }

    template <typename MutableBufferSequence>
    std::size_t read_some (MutableBufferSequence const&, error_code& ec,
        FalseType)
    {
        pure_virtual_called (__FILE__, __LINE__);
        ec = pure_virtual_error ();
        return 0;
    }

    //--------------------------------------------------------------------------

    std::size_t write_some (ConstBuffers const& buffers, error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return write_some (buffers, ec,
            Enabled <has_write_some <this_layer_type,
                std::size_t (ConstBuffers const&, error_code&)> > ());
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const& buffers, error_code& ec,
        TrueType)
    {
        return m_object.write_some (buffers, ec);
    }

    template <typename ConstBufferSequence>
    std::size_t write_some (ConstBufferSequence const&, error_code& ec,
        FalseType)
    {
        pure_virtual_called (__FILE__, __LINE__);
        ec = pure_virtual_error ();
        return 0;
    }

    //--------------------------------------------------------------------------

    void async_read_some (MutableBuffers const& buffers, SharedHandlerPtr handler)
    {
        using namespace detail::SocketWrapperMemberChecks;
        async_read_some (buffers, BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler),
            Enabled <has_async_read_some <this_layer_type,
                void (MutableBuffers const&, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))> > ());
    }

    void async_read_some (MutableBuffers const& buffers,
            BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
                TrueType)
    {
        m_object.async_read_some (buffers,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
    }

    void async_read_some (MutableBuffers const&,
        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
            FalseType)
    {
        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (pure_virtual_error (), 0);
    }

    //--------------------------------------------------------------------------

    void async_write_some (ConstBuffers const& buffers, SharedHandlerPtr handler)
    {
        using namespace detail::SocketWrapperMemberChecks;
        async_write_some (buffers, BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler),
            Enabled <has_async_write_some <this_layer_type,
                void (ConstBuffers const&, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))> > ());
    }

    void async_write_some (ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
            TrueType)
    {
        m_object.async_write_some (buffers,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
    }

    void async_write_some (ConstBuffers const&,
        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
            FalseType)
    {
        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (pure_virtual_error (), 0);
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //

    template <class T>
    struct has_type_next_layer_type
    {
        typedef char yes; 
        typedef struct {char dummy[2];} no;
        template <class C> static yes f(typename C::next_layer_type*);
        template <class C> static no f(...);
#ifdef _MSC_VER
        static bool const value = sizeof(f<T>(0)) == 1;
#else
        // This line fails to compile under Visual Studio 2012
        static bool const value = sizeof(has_type_next_layer_type<T>::f<T>(0)) == 1;
#endif
    }; 

    template <typename T, bool Exists = has_type_next_layer_type <T>::value >
    struct extract_next_layer_type
    {
        typedef typename T::next_layer_type type;
    };

    template <typename T>
    struct extract_next_layer_type <T, false>
    {
        typedef void type;
    };

    // This will be void if next_layer_type doesn't exist in Object
    typedef typename extract_next_layer_type <this_layer_type>::type next_layer_type;

    void* next_layer_ptr (char const* type_name) const
    {
        using namespace detail::SocketWrapperMemberChecks;
        return next_layer_ptr (type_name,
            Enabled <has_type_next_layer_type <this_layer_type> > ());
    }

    void* next_layer_ptr (char const* type_name,
        TrueType) const
    {
        char const* const name (typeid (typename this_layer_type::next_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object.next_layer ()));
        return nullptr;
    }

    void* next_layer_ptr (char const*,
        FalseType) const
    {
        pure_virtual_called (__FILE__, __LINE__);
        return nullptr;
    }

    //--------------------------------------------------------------------------

    bool needs_handshake ()
    {
        using namespace detail::SocketWrapperMemberChecks;
        return
            has_handshake <this_layer_type,
                error_code (handshake_type, error_code&)>::value ||
            has_async_handshake <this_layer_type,
                void (handshake_type, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))>::value;
    }

    //--------------------------------------------------------------------------

    void set_verify_mode (int verify_mode)
    {
        using namespace detail::SocketWrapperMemberChecks;
        set_verify_mode (verify_mode,
            Enabled <has_set_verify_mode <this_layer_type,
                void (int)> > ());
 
    }

    void set_verify_mode (int verify_mode,
        TrueType)
    {
        m_object.set_verify_mode (verify_mode);
    }

    void set_verify_mode (int,
        FalseType)
    {
        pure_virtual_called (__FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    error_code handshake (handshake_type type, error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return handshake (type, ec,
            Enabled <has_handshake <this_layer_type,
                error_code (handshake_type, error_code&)> > ());
    }

    error_code handshake (handshake_type type, error_code& ec,
        TrueType)
    {
        return m_object.handshake (type, ec);
    }

    error_code handshake (handshake_type, error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    void async_handshake (handshake_type type, SharedHandlerPtr handler)
    {
        using namespace detail::SocketWrapperMemberChecks;
        async_handshake (type, BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler),
            Enabled <has_async_handshake <this_layer_type,
                void (handshake_type, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))> > ());
    }

    void async_handshake (handshake_type type,
        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
            TrueType)
    {
        m_object.async_handshake (type,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
    }

    void async_handshake (handshake_type, BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
        FalseType)
    {
        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (pure_virtual_error ());
    }

    //--------------------------------------------------------------------------

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE

    error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return handshake (type, buffers, ec,
            Enabled <has_handshake <this_layer_type,
                error_code (handshake_type, ConstBuffers const&, error_code&)> > ());
    }

    error_code handshake (handshake_type type,
        ConstBuffers const& buffers, error_code& ec,
            TrueType)
    {
        return m_object.handshake (type, buffers, ec);
    }

    error_code handshake (handshake_type, ConstBuffers const&, error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    void async_handshake (handshake_type type,
        ConstBuffers const& buffers, SharedHandlerPtr handler)
    {
        using namespace detail::SocketWrapperMemberChecks;
        async_handshake (type, buffers,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler),
                Enabled <has_async_handshake <this_layer_type,
                    void (handshake_type, ConstBuffers const&,
                        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))> > ());
    }

    void async_handshake (handshake_type type, ConstBuffers const& buffers,
        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
            TrueType)
    {
        m_object.async_handshake (type, buffers,
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
    }

    void async_handshake (handshake_type, ConstBuffers const&,
        BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
            FalseType)
    {
        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (pure_virtual_error (), 0);
    }

#endif

    //--------------------------------------------------------------------------

    error_code shutdown (error_code& ec)
    {
        using namespace detail::SocketWrapperMemberChecks;
        return shutdown (ec,
            Enabled <has_shutdown <this_layer_type,
                error_code (error_code&)> > ());
    }

    error_code shutdown (error_code& ec,
        TrueType)
    {
        return m_object.shutdown (ec);
    }

    error_code shutdown (error_code& ec,
        FalseType)
    {
        return pure_virtual_error (ec, __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    void async_shutdown (SharedHandlerPtr handler)
    {
        using namespace detail::SocketWrapperMemberChecks;
        async_shutdown (BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler),
            Enabled <has_async_shutdown <this_layer_type,
                void (BOOST_ASIO_MOVE_ARG(SharedHandlerPtr))> > ());
    }

    void async_shutdown (BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
        TrueType)
    {
        m_object.async_shutdown (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler));
    }

    void async_shutdown (BOOST_ASIO_MOVE_ARG(SharedHandlerPtr) handler,
        FalseType)
    {
        get_io_service ().wrap (
            BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
                (pure_virtual_error ());
    }

private:
    Object m_object;
};

#endif
