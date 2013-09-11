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

Socket::~Socket ()
{
}

#if ! BEAST_COMPILER_CHECKS_SOCKET_OVERRIDES

//-----------------------------------------------------------------------------
//
// Socket
//

void* Socket::this_layer_ptr (char const*) const
{
    pure_virtual_called (__FILE__, __LINE__);
    return nullptr;
}

//-----------------------------------------------------------------------------
//
// native_handle
//

bool Socket::native_handle (char const*, void*)
{
    pure_virtual_called (__FILE__, __LINE__);
    return false;
}

//-----------------------------------------------------------------------------
//
// basic_io_object
//

boost::asio::io_service& Socket::get_io_service ()
{
    pure_virtual_called (__FILE__, __LINE__);
    return *static_cast <boost::asio::io_service*>(nullptr);
}

//-----------------------------------------------------------------------------
//
// basic_socket
//

void* Socket::lowest_layer_ptr (char const*) const
{
    pure_virtual_called (__FILE__, __LINE__);
    return nullptr;
}

boost::system::error_code Socket::cancel (boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

boost::system::error_code Socket::shutdown (shutdown_type, boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

boost::system::error_code Socket::close (boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

//------------------------------------------------------------------------------
//
// basic_socket_acceptor
//

boost::system::error_code Socket::accept (Socket&, boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

void Socket::async_accept (Socket&, SharedHandlerPtr handler)
{
    get_io_service ().wrap (
        BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
            (pure_virtual_error ());
}

//------------------------------------------------------------------------------
//
// basic_stream_socket
//

std::size_t Socket::read_some (MutableBuffers const&, boost::system::error_code& ec)
{
    pure_virtual_called (__FILE__, __LINE__);
    ec = pure_virtual_error ();
    return 0;
}

std::size_t Socket::write_some (ConstBuffers const&, boost::system::error_code& ec)
{
    pure_virtual_called (__FILE__, __LINE__);
    ec = pure_virtual_error ();
    return 0;
}

void Socket::async_read_some (MutableBuffers const&, SharedHandlerPtr handler)
{
    get_io_service ().wrap (
        BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
            (pure_virtual_error (), 0);
}

void Socket::async_write_some (ConstBuffers const&, SharedHandlerPtr handler)
{
    get_io_service ().wrap (
        BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
            (pure_virtual_error (), 0);
}

//--------------------------------------------------------------------------
//
// ssl::stream
//

void* Socket::next_layer_ptr (char const*) const
{
    pure_virtual_called (__FILE__, __LINE__);
    return nullptr;
}

bool Socket::needs_handshake ()
{
    return false;
}

void Socket::set_verify_mode (int)
{
    pure_virtual_called (__FILE__, __LINE__);
}

boost::system::error_code Socket::handshake (handshake_type, boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

void Socket::async_handshake (handshake_type, SharedHandlerPtr handler)
{
    get_io_service ().wrap (
        BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
            (pure_virtual_error ());
}

#if BEAST_ASIO_HAS_BUFFEREDHANDSHAKE

boost::system::error_code Socket::handshake (handshake_type,
    ConstBuffers const&, boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

void Socket::async_handshake (handshake_type, ConstBuffers const&, SharedHandlerPtr handler)
{
    get_io_service ().wrap (
        BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
            (pure_virtual_error (), 0);
}

#endif

boost::system::error_code Socket::shutdown (boost::system::error_code& ec)
{
    return pure_virtual_error (ec, __FILE__, __LINE__);
}

void Socket::async_shutdown (SharedHandlerPtr handler)
{
    get_io_service ().wrap (
        BOOST_ASIO_MOVE_CAST(SharedHandlerPtr)(handler))
            (pure_virtual_error ());
}

//------------------------------------------------------------------------------

#endif
