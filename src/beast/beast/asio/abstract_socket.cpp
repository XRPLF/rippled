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

#include <beast/asio/abstract_socket.h>
#include <beast/asio/bind_handler.h>

namespace beast {
namespace asio {

#if ! BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES

//------------------------------------------------------------------------------
//
// Socket
//
//------------------------------------------------------------------------------

void* abstract_socket::this_layer_ptr (char const*) const
{
    pure_virtual_called ();
    return nullptr;
}

//------------------------------------------------------------------------------
//
// native_handle
//
//------------------------------------------------------------------------------

bool abstract_socket::native_handle (char const*, void*)
{
    pure_virtual_called ();
    return false;
}

//------------------------------------------------------------------------------
//
// basic_io_object
//
//------------------------------------------------------------------------------

boost::asio::io_service& abstract_socket::get_io_service ()
{
    pure_virtual_called ();
    return *static_cast <boost::asio::io_service*>(nullptr);
}

//------------------------------------------------------------------------------
//
// basic_socket
//
//------------------------------------------------------------------------------

void*
abstract_socket::lowest_layer_ptr (char const*) const
{
    pure_virtual_called ();
    return nullptr;
}

auto
abstract_socket::cancel (boost::system::error_code& ec) -> error_code
{
    return pure_virtual_error (ec);
}

auto
abstract_socket::shutdown (shutdown_type, boost::system::error_code& ec) -> error_code
{
    return pure_virtual_error (ec);
}

auto
abstract_socket::close (boost::system::error_code& ec) -> error_code
{
    return pure_virtual_error (ec);
}

//------------------------------------------------------------------------------
//
// basic_socket_acceptor
//
//------------------------------------------------------------------------------

auto 
abstract_socket::accept (abstract_socket&, error_code& ec) -> error_code
{
    return pure_virtual_error (ec);
}

void
abstract_socket::async_accept (abstract_socket&, error_handler handler)
{
    get_io_service ().post (bind_handler (
        handler, pure_virtual_error()));
}

//------------------------------------------------------------------------------
//
// basic_stream_socket
//
//------------------------------------------------------------------------------

std::size_t
abstract_socket::read_some (mutable_buffers, error_code& ec)
{
    ec = pure_virtual_error ();
    return 0;
}

std::size_t
abstract_socket::write_some (const_buffers, error_code& ec)
{
    ec = pure_virtual_error ();
    return 0;
}

void
abstract_socket::async_read_some (mutable_buffers, transfer_handler handler)
{
    get_io_service ().post (bind_handler (
        handler, pure_virtual_error(), 0));
}

void
abstract_socket::async_write_some (const_buffers, transfer_handler handler)
{
    get_io_service ().post (bind_handler (
        handler, pure_virtual_error(), 0));
}

//------------------------------------------------------------------------------
//
// ssl::stream
//
//------------------------------------------------------------------------------

void*
abstract_socket::next_layer_ptr (char const*) const
{
    pure_virtual_called ();
    return nullptr;
}

bool
abstract_socket::needs_handshake ()
{
    return false;
}

void
abstract_socket::set_verify_mode (int)
{
    pure_virtual_called ();
}

auto
abstract_socket::handshake (handshake_type, error_code& ec) -> error_code
{
    return pure_virtual_error (ec);
}

void
abstract_socket::async_handshake (handshake_type, error_handler handler)
{
    get_io_service ().post (bind_handler (
        handler, pure_virtual_error()));
}

auto
abstract_socket::handshake (handshake_type, const_buffers, error_code& ec) ->
    error_code
{
    return pure_virtual_error (ec);
}

void
abstract_socket::async_handshake (handshake_type, const_buffers,
    transfer_handler handler)
{
    get_io_service ().post (bind_handler (
        handler, pure_virtual_error(), 0));
}

auto
abstract_socket::shutdown (error_code& ec) -> error_code
{
    return pure_virtual_error (ec);
}

void
abstract_socket::async_shutdown (error_handler handler)
{
    get_io_service ().post (bind_handler (
        handler, pure_virtual_error()));
}

#endif

}
}
