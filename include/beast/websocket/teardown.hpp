//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_TEARDOWN_HPP
#define BEAST_WEBSOCKET_TEARDOWN_HPP

#include <beast/websocket/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <type_traits>

namespace beast {
namespace websocket {

/** Tear down a connection.

    This tears down a connection. The implementation will call
    the overload of this function based on the `Socket` parameter
    used to consruct the socket. When `Socket` is a user defined
    type, and not a `boost::asio::ip::tcp::socket` or any
    `boost::asio::ssl::stream`, callers are responsible for
    providing a suitable overload of this function.

    @param socket The socket to tear down.

    @param ec Set to the error if any occurred.
*/
template<class Socket>
void
teardown(Socket& socket, error_code& ec) = delete;

/** Start tearing down a connection.

    This begins tearing down a connection asynchronously.
    The implementation will call the overload of this function
    based on the `Socket` parameter used to consruct the socket.
    When `Stream` is a user defined type, and not a
    `boost::asio::ip::tcp::socket` or any `boost::asio::ssl::stream`,
    callers are responsible for providing a suitable overload
    of this function.

    @param socket The socket to tear down.

    @param handler The handler to be called when the request completes.
    Copies will be made of the handler as required. The equivalent
    function signature of the handler must be:
    @code void handler(
        error_code const& error // result of operation
    ); @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using boost::asio::io_service::post().

*/
template<class Socket, class TeardownHandler>
void
async_teardown(Socket& socket, TeardownHandler&& handler) = delete;

//------------------------------------------------------------------------------

/** Tear down a `boost::asio::ip::tcp::socket`.

    This tears down a connection. The implementation will call
    the overload of this function based on the `Stream` parameter
    used to consruct the socket. When `Stream` is a user defined
    type, and not a `boost::asio::ip::tcp::socket` or any
    `boost::asio::ssl::stream`, callers are responsible for
    providing a suitable overload of this function.

    @param socket The socket to tear down.

    @param ec Set to the error if any occurred.
*/
void
teardown(
    boost::asio::ip::tcp::socket& socket,
        error_code& ec);

/** Start tearing down a `boost::asio::ip::tcp::socket`.

    This begins tearing down a connection asynchronously.
    The implementation will call the overload of this function
    based on the `Stream` parameter used to consruct the socket.
    When `Stream` is a user defined type, and not a
    `boost::asio::ip::tcp::socket` or any `boost::asio::ssl::stream`,
    callers are responsible for providing a suitable overload
    of this function.

    @param socket The socket to tear down.

    @param handler The handler to be called when the request completes.
    Copies will be made of the handler as required. The equivalent
    function signature of the handler must be:
    @code void handler(
        error_code const& error // result of operation
    ); @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using boost::asio::io_service::post().

*/
template<class TeardownHandler>
void
async_teardown(
    boost::asio::ip::tcp::socket& socket,
        TeardownHandler&& handler);

} // websocket

//------------------------------------------------------------------------------

namespace websocket_helpers {

// Calls to teardown and async_teardown must be made from
// a namespace that does not contain any overloads of these
// functions. The websocket_helpers namespace is defined here
// for that purpose.

template<class Socket>
inline
void
call_teardown(Socket& socket, error_code& ec)
{
    using websocket::teardown;
    teardown(socket, ec);
}

template<class Socket, class TeardownHandler>
inline
void
call_async_teardown(Socket& socket, TeardownHandler&& handler)
{
    using websocket::async_teardown;
    async_teardown(socket,
        std::forward<TeardownHandler>(handler));
}

} // websocket_helpers

} // beast

#include <beast/websocket/impl/teardown.ipp>

#endif
