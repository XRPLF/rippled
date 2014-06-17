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

#ifndef BEAST_ASIO_ABSTRACT_SOCKET_H_INCLUDED
#define BEAST_ASIO_ABSTRACT_SOCKET_H_INCLUDED

#include <beast/asio/buffer_sequence.h>
#include <beast/asio/shared_handler.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/ssl/stream_base.hpp>

// Checking overrides replaces unimplemented stubs with pure virtuals
#ifndef BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
# define BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES 1
#endif

#if BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES
# define BEAST_SOCKET_VIRTUAL = 0
#else
# define BEAST_SOCKET_VIRTUAL
#endif

namespace beast {
namespace asio {

/** A high level socket abstraction.

    This combines the capabilities of multiple socket interfaces such
    as listening, connecting, streaming, and handshaking. It brings
    everything together into a single abstract interface.

    When member functions are called and the underlying implementation does
    not support the operation, a fatal error is generated.
*/
class abstract_socket
    : public boost::asio::ssl::stream_base
    , public boost::asio::socket_base
{
protected:
    typedef boost::system::error_code error_code;

    typedef asio::shared_handler <void (void)> post_handler;

    typedef asio::shared_handler <void (error_code)> error_handler;

    typedef asio::shared_handler <
        void (error_code, std::size_t)> transfer_handler;

    static
    void
    pure_virtual_called()
    {
        throw std::runtime_error ("pure virtual called");
    }

    static
    error_code
    pure_virtual_error ()
    {
        pure_virtual_called();
        return boost::system::errc::make_error_code (
            boost::system::errc::function_not_supported);
    }

    static
    error_code
    pure_virtual_error (error_code& ec)
    {
        return ec = pure_virtual_error();
    }

    static
    void
    throw_if (error_code const& ec)
    {
        if (ec)
            throw boost::system::system_error (ec);
    }

public:
    virtual ~abstract_socket ()
    {
    }

    //--------------------------------------------------------------------------
    //
    // abstract_socket
    //
    //--------------------------------------------------------------------------

    /** Retrieve the underlying object.

        @note If the type doesn't match, nullptr is returned or an
              exception is thrown if trying to acquire a reference.
    */
    /** @{ */
    template <class Object>
    Object& this_layer ()
    {
        Object* object (this->this_layer_ptr <Object> ());
        if (object == nullptr)
            throw std::bad_cast ();
        return *object;
    }

    template <class Object>
    Object const& this_layer () const
    {
        Object const* object (this->this_layer_ptr <Object> ());
        if (object == nullptr)
            throw std::bad_cast ();
        return *object;
    }

    template <class Object>
    Object* this_layer_ptr ()
    {
        return static_cast <Object*> (
            this->this_layer_ptr (typeid (Object).name ()));
    }

    template <class Object>
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
    //--------------------------------------------------------------------------

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
            throw std::bad_cast ();
    }

    virtual bool native_handle (char const* type_name, void* dest)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_io_object
    //
    //--------------------------------------------------------------------------

    virtual boost::asio::io_service& get_io_service ()
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_socket
    //
    //--------------------------------------------------------------------------

    /** Retrieve the lowest layer object.

        @note If the type doesn't match, nullptr is returned or an
              exception is thrown if trying to acquire a reference.
    */
    /** @{ */
    template <class Object>
    Object& lowest_layer ()
    {
        Object* object (this->lowest_layer_ptr <Object> ());
        if (object == nullptr)
            throw std::bad_cast ();
        return *object;
    }

    template <class Object>
    Object const& lowest_layer () const
    {
        Object const* object (this->lowest_layer_ptr <Object> ());
        if (object == nullptr)
            throw std::bad_cast ();
        return *object;
    }

    template <class Object>
    Object* lowest_layer_ptr ()
    {
        return static_cast <Object*> (
            this->lowest_layer_ptr (typeid (Object).name ()));
    }

    template <class Object>
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
        cancel (ec);
        throw_if (ec);
    }

    virtual error_code cancel (error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    void shutdown (shutdown_type what)
    {
        error_code ec;
        shutdown (what, ec);
        throw_if (ec);
    }

    virtual error_code shutdown (shutdown_type what,
        error_code& ec)
            BEAST_SOCKET_VIRTUAL;

    void close ()
    {
        error_code ec;
        close (ec);
        throw_if (ec);
    }

    virtual error_code close (error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_socket_acceptor
    //
    //--------------------------------------------------------------------------

    virtual error_code accept (abstract_socket& peer, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    virtual void async_accept (abstract_socket& peer, error_handler handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // basic_stream_socket
    //
    //--------------------------------------------------------------------------

    virtual std::size_t read_some (mutable_buffers buffers, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    virtual std::size_t write_some (const_buffers buffers, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    virtual void async_read_some (mutable_buffers buffers,
        transfer_handler handler)
        BEAST_SOCKET_VIRTUAL;

    virtual void async_write_some (const_buffers buffers,
        transfer_handler handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------
    //
    // ssl::stream
    //
    //--------------------------------------------------------------------------

    /** Retrieve the next layer object.

        @note If the type doesn't match, nullptr is returned or an
              exception is thrown if trying to acquire a reference.
    */
    /** @{ */
    template <class Object>
    Object& next_layer ()
    {
        Object* object (this->next_layer_ptr <Object> ());
        if (object == nullptr)
            throw std::bad_cast ();
        return *object;
    }

    template <class Object>
    Object const& next_layer () const
    {
        Object const* object (this->next_layer_ptr <Object> ());
        if (object == nullptr)
            throw std::bad_cast ();
        return *object;
    }

    template <class Object>
    Object* next_layer_ptr ()
    {
        return static_cast <Object*> (
            this->next_layer_ptr (typeid (Object).name ()));
    }

    template <class Object>
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

    virtual void set_verify_mode (int verify_mode)
        BEAST_SOCKET_VIRTUAL;

    void handshake (handshake_type type)
    {
        error_code ec;
        handshake (type, ec);
        throw_if (ec);
    }

    virtual error_code handshake (handshake_type type, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    virtual void async_handshake (handshake_type type, error_handler handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------

    virtual error_code handshake (handshake_type type,
        const_buffers buffers, error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    virtual void async_handshake (handshake_type type,
        const_buffers buffers, transfer_handler handler)
        BEAST_SOCKET_VIRTUAL;

    //--------------------------------------------------------------------------

    void shutdown ()
    {
        error_code ec;
        shutdown (ec);
        throw_if (ec);
    }

    virtual error_code shutdown (error_code& ec)
        BEAST_SOCKET_VIRTUAL;

    virtual void async_shutdown (error_handler handler)
        BEAST_SOCKET_VIRTUAL;
};

}
}

#endif
