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

#ifndef BEAST_ASIO_SOCKET_WRAPPER_H_INCLUDED
#define BEAST_ASIO_SOCKET_WRAPPER_H_INCLUDED

#include <beast/asio/abstract_socket.h>
#include <beast/asio/bind_handler.h>

#include <beast/utility/noexcept.h>

namespace beast {
namespace asio {

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
        explain socket_wrapper will create and take ownership of the tcp::socket
        explain this_layer_type will be tcp::socket
        explain next_layer () returns a asio::ip::tcp::socket&
        explain lowest_layer () returns a asio::ip::tcp::socket&

    asio::ssl::stream <asio::buffered_stream <asio::ip::tcp::socket> > >
        This makes my head explode
*/
template <typename Object>
class socket_wrapper : public abstract_socket
{
private:
    Object m_object;

public:
    template <class... Args>
    explicit socket_wrapper (Args&&... args)
        : m_object (std::forward <Args> (args)...)
    {
    }

    socket_wrapper (socket_wrapper const&) = delete;
    socket_wrapper& operator= (socket_wrapper const&) = delete;

    //--------------------------------------------------------------------------
    //
    // socket_wrapper
    //
    //--------------------------------------------------------------------------

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
    // abstract_socket
    //
    //--------------------------------------------------------------------------

    void* this_layer_ptr (char const* type_name) const override
    {
        char const* const name (typeid (this_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object));
        return nullptr;
    }

private:
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

    //--------------------------------------------------------------------------
    //
    // Implementation
    //
    //--------------------------------------------------------------------------

    template <class Cond>
    struct Enabled : public std::integral_constant <bool, Cond::value>
    {
    };

    //--------------------------------------------------------------------------
    //
    // native_handle
    //
    //--------------------------------------------------------------------------

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
        static bool const value = sizeof(
            has_type_native_handle_type<T>::f<T>(0)) == 1;
    #endif
    }; 

#endif

    template <typename T,
        bool Exists = has_type_native_handle_type <T>::value
    >
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
    typedef typename extract_native_handle_type <
        this_layer_type>::type native_handle_type;

    //--------------------------------------------------------------------------

    bool native_handle (char const* type_name, void* dest) override
    {
        return native_handle (type_name, dest,
            Enabled <has_type_native_handle_type <this_layer_type> > ());
    }

    bool native_handle (char const* type_name, void* dest,
        std::true_type)
    {
        char const* const name (typeid (
            typename this_layer_type::native_handle_type).name ());
        if (strcmp (name, type_name) == 0)
        {
            native_handle_type* const p (reinterpret_cast <
                native_handle_type*> (dest));
            *p = m_object.native_handle ();
            return true;
        }
        return false;
    }

    bool native_handle (char const*, void*,
        std::false_type)
    {
        pure_virtual_called();
        return false;
    }

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //
    //--------------------------------------------------------------------------

    boost::asio::io_service& get_io_service () override
    {
        return get_io_service (
            Enabled <has_get_io_service <this_layer_type,
                boost::asio::io_service&()> > ());
    }

    boost::asio::io_service& get_io_service (
        std::true_type)
    {
        return m_object.get_io_service ();
    }

    boost::asio::io_service& get_io_service (
        std::false_type)
    {
        pure_virtual_called();
        return *static_cast <boost::asio::io_service*>(nullptr);
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //
    //--------------------------------------------------------------------------

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

    //--------------------------------------------------------------------------

    void* lowest_layer_ptr (char const* type_name) const override
    {
        return lowest_layer_ptr (type_name,
            Enabled <has_type_lowest_layer_type <this_layer_type> > ());
    }

    void* lowest_layer_ptr (char const* type_name,
        std::true_type) const
    {
        char const* const name (typeid (typename this_layer_type::lowest_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object.lowest_layer ()));
        return nullptr;
    }

    void* lowest_layer_ptr (char const*,
        std::false_type) const
    {
        pure_virtual_called();
        return nullptr;
    }

    //--------------------------------------------------------------------------

    error_code cancel (error_code& ec) override
    {
        return cancel (ec,
            Enabled <has_cancel <this_layer_type,
                error_code (error_code&)> > ());
    }
   
    error_code cancel (error_code& ec,
        std::true_type)
    {
        return m_object.cancel (ec);
    }

    error_code cancel (error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------

    error_code shutdown (shutdown_type what, error_code& ec) override
    {
        return shutdown (what, ec,
            Enabled <has_shutdown <this_layer_type,
                error_code (shutdown_type, error_code&)> > ());
    }


    error_code shutdown (shutdown_type what, error_code& ec,
        std::true_type)
    {
        return m_object.shutdown (what, ec);
    }

    error_code shutdown (shutdown_type, error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------

    error_code close (error_code& ec) override
    {
        return close (ec,
            Enabled <has_close <this_layer_type,
                error_code (error_code&)> > ());
    }

    error_code close (error_code& ec,
        std::true_type)
    {
        return m_object.close (ec);
    }

    error_code close (error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------
    //
    // basic_socket_acceptor
    //
    //--------------------------------------------------------------------------

    // Extracts the underlying socket type from the protocol of another asio object
    template <typename T, typename Enable = void>
    struct native_socket
    {
        typedef void* socket_type;
        inline native_socket (abstract_socket&)
            : m_socket (nullptr)
        {
            abstract_socket::pure_virtual_called();
        }
        inline socket_type& get ()
        {
            abstract_socket::pure_virtual_called();
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
        inline native_socket (abstract_socket& peer)
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

    //--------------------------------------------------------------------------

    error_code accept (abstract_socket& peer, error_code& ec) override
    {
        typedef typename native_socket <this_layer_type>::socket_type socket_type;
        return accept (peer, ec,
            Enabled <has_accept <this_layer_type,
                error_code (socket_type&, error_code&)> > ());
    }

    error_code accept (abstract_socket& peer, error_code& ec,
        std::true_type)
    {
        return m_object.accept (
            native_socket <this_layer_type> (peer).get (), ec);
    }

    error_code accept (abstract_socket&, error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------

    void async_accept (abstract_socket& peer, error_handler handler) override
    {
        typedef typename native_socket <this_layer_type>::socket_type socket_type;
        async_accept (peer, handler,
            Enabled <has_async_accept <this_layer_type,
                void (socket_type&, error_handler)> > ());
    }

    void async_accept (abstract_socket& peer, error_handler const& handler,
        std::true_type)
    {
        m_object.async_accept (
            native_socket <this_layer_type> (peer).get (), handler);
    }

    void async_accept (abstract_socket&, error_handler const& handler,
        std::false_type)
    {
        get_io_service ().post (bind_handler (
            handler, pure_virtual_error()));
    }

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //
    //--------------------------------------------------------------------------

    std::size_t
    read_some (mutable_buffers buffers, error_code& ec) override
    {
        return read_some (buffers, ec,
            Enabled <has_read_some <this_layer_type,
                std::size_t (mutable_buffers const&, error_code&)> > ());
    }

    std::size_t
    read_some (mutable_buffers const& buffers, error_code& ec,
        std::true_type)
    {
        return m_object.read_some (buffers, ec);
    }

    std::size_t read_some (mutable_buffers const&, error_code& ec,
        std::false_type)
    {
        ec = pure_virtual_error ();
        return 0;
    }

    //--------------------------------------------------------------------------

    std::size_t
    write_some (const_buffers buffers, error_code& ec) override
    {
        return write_some (buffers, ec,
            Enabled <has_write_some <this_layer_type,
                std::size_t (const_buffers const&, error_code&)> > ());
    }

    std::size_t
    write_some (const_buffers const& buffers, error_code& ec,
        std::true_type)
    {
        return m_object.write_some (buffers, ec);
    }

    std::size_t
    write_some (const_buffers const&, error_code& ec,
        std::false_type)
    {
        ec = pure_virtual_error ();
        return 0;
    }

    //--------------------------------------------------------------------------

    void async_read_some (mutable_buffers buffers,
        transfer_handler handler) override
    {
        async_read_some (buffers, handler,
            Enabled <has_async_read_some <this_layer_type,
                void (mutable_buffers const&, transfer_handler const&)> > ());
    }

    void
    async_read_some (mutable_buffers const& buffers,
            transfer_handler const& handler,
        std::true_type)
    {
        m_object.async_read_some (buffers, handler);
    }

    void
    async_read_some (mutable_buffers const&,
            transfer_handler const& handler,
        std::false_type)
    {
        get_io_service ().post (bind_handler (
            handler, pure_virtual_error(), 0));
    }

    //--------------------------------------------------------------------------

    void
    async_write_some (const_buffers buffers,
        transfer_handler handler) override
    {
        async_write_some (buffers, handler,
            Enabled <has_async_write_some <this_layer_type,
                void (const_buffers const&, transfer_handler const&)> > ());
    }

    void
    async_write_some (const_buffers const& buffers,
            transfer_handler const& handler,
        std::true_type)
    {
        m_object.async_write_some (buffers, handler);
    }

    void
    async_write_some (const_buffers const&,
            transfer_handler const& handler,
        std::false_type)
    {
        get_io_service ().post (bind_handler (
            handler, pure_virtual_error(), 0));
    }

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //
    //--------------------------------------------------------------------------

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

    //--------------------------------------------------------------------------

    void* next_layer_ptr (char const* type_name) const override
    {
        return next_layer_ptr (type_name,
            Enabled <has_type_next_layer_type <this_layer_type> > ());
    }

    void* next_layer_ptr (char const* type_name,
        std::true_type) const
    {
        char const* const name (typeid (typename this_layer_type::next_layer_type).name ());
        if (strcmp (name, type_name) == 0)
            return const_cast <void*> (static_cast <void const*> (&m_object.next_layer ()));
        return nullptr;
    }

    void* next_layer_ptr (char const*,
        std::false_type) const
    {
        pure_virtual_called();
        return nullptr;
    }

    //--------------------------------------------------------------------------

    bool needs_handshake () override
    {
        return
            has_handshake <this_layer_type,
                error_code (handshake_type, error_code&)>::value ||
            has_async_handshake <this_layer_type,
                void (handshake_type, error_handler)>::value;
    }

    //--------------------------------------------------------------------------

    void set_verify_mode (int verify_mode) override
    {
        set_verify_mode (verify_mode,
            Enabled <has_set_verify_mode <this_layer_type,
                void (int)> > ());
 
    }

    void set_verify_mode (int verify_mode,
        std::true_type)
    {
        m_object.set_verify_mode (verify_mode);
    }

    void set_verify_mode (int,
        std::false_type)
    {
        pure_virtual_called();
    }

    //--------------------------------------------------------------------------

    error_code
    handshake (handshake_type type, error_code& ec) override
    {
        return handshake (type, ec,
            Enabled <has_handshake <this_layer_type,
                error_code (handshake_type, error_code&)> > ());
    }

    error_code
    handshake (handshake_type type, error_code& ec,
        std::true_type)
    {
        return m_object.handshake (type, ec);
    }

    error_code
    handshake (handshake_type, error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------

    void async_handshake (handshake_type type, error_handler handler) override
    {
        async_handshake (type, handler,
            Enabled <has_async_handshake <this_layer_type,
                void (handshake_type, error_handler)> > ());
    }

    void async_handshake (handshake_type type, error_handler const& handler,
        std::true_type)
    {
        m_object.async_handshake (type, handler);
    }

    void async_handshake (handshake_type, error_handler const& handler,
        std::false_type)
    {
        get_io_service ().post (bind_handler (
            handler, pure_virtual_error()));
    }

    //--------------------------------------------------------------------------

    error_code
    handshake (handshake_type type, const_buffers buffers,
        error_code& ec) override
    {
        return handshake (type, buffers, ec,
            Enabled <has_handshake <this_layer_type,
                error_code (handshake_type, const_buffers const&, error_code&)> > ());
    }

    error_code
    handshake (handshake_type type, const_buffers const& buffers,
            error_code& ec,
        std::true_type)
    {
        return m_object.handshake (type, buffers, ec);
    }

    error_code
    handshake (handshake_type, const_buffers const&,
            error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------

    void async_handshake (handshake_type type,
        const_buffers buffers, transfer_handler handler) override
    {
        async_handshake (type, buffers, handler,
            Enabled <has_async_handshake <this_layer_type,
                void (handshake_type, const_buffers const&,
                    transfer_handler)> > ());
    }

    void async_handshake (handshake_type type, const_buffers const& buffers,
            transfer_handler const& handler,
        std::true_type)
    {
        m_object.async_handshake (type, buffers, handler);
    }

    void async_handshake (handshake_type, const_buffers const&,
            transfer_handler const& handler,
        std::false_type)
    {
        get_io_service ().post (bind_handler (
            handler, pure_virtual_error(), 0));
    }

    //--------------------------------------------------------------------------

    error_code shutdown (error_code& ec) override
    {
        return shutdown (ec,
            Enabled <has_shutdown <this_layer_type,
                error_code (error_code&)> > ());
    }

    error_code shutdown (error_code& ec,
        std::true_type)
    {
        return m_object.shutdown (ec);
    }

    error_code shutdown (error_code& ec,
        std::false_type)
    {
        return pure_virtual_error (ec);
    }

    //--------------------------------------------------------------------------

    void async_shutdown (error_handler handler) override
    {
        async_shutdown (handler,
            Enabled <has_async_shutdown <this_layer_type,
                void (error_handler)> > ());
    }

    void async_shutdown (error_handler const& handler,
        std::true_type)
    {
        m_object.async_shutdown (handler);
    }

    void async_shutdown (error_handler const& handler,
        std::false_type)
    {
        get_io_service ().post (bind_handler (
            handler, pure_virtual_error()));
    }
};

}
}

#endif
