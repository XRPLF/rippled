//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_READ_HPP
#define BEAST_HTTP_READ_HPP

#include <beast/http/error.hpp>
#include <beast/http/parser_v1.hpp>
#include <beast/async_completion.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

namespace beast {
namespace http {

/** Read a HTTP/1 message from a stream.

    This function is used to synchronously read a message from
    the stream. The call blocks until one of the following conditions
    is true:

    @li A complete message is read in.

    @li An error occurs on the stream.

    This function is implemented in terms of one or more calls to the
    stream's `read_some` function.

    @param stream The stream to which the data is to be written.
    The type must support the @b `SyncReadStream` concept.

    @param streambuf An object meeting the @b `Streambuf` type requirements
    used to hold unread bytes. The implementation may read past the end of
    the message. The extra bytes are stored here, to be presented in a
    subsequent call to @ref read.

    @param msg An object used to store the message. Any
    contents will be overwritten.

    @throws boost::system::system_error Thrown on failure.
*/
template<class SyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    message_v1<isRequest, Body, Headers>& msg)
{
    error_code ec;
    read(stream, streambuf, msg, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

/** Read a HTTP/1 message from a stream.

    This function is used to synchronously read a message from
    the stream. The call blocks until one of the following conditions
    is true:

    @li A complete message is read in.

    @li An error occurs on the stream.

    This function is implemented in terms of one or more calls to the
    stream's `read_some` function.

    @param stream The stream to which the data is to be written.
    The type must support the @b `SyncReadStream` concept.

    @param streambuf An object meeting the @b `Streambuf` type requirements
    used to hold unread bytes. The implementation may read past the end of
    the message. The extra bytes are stored here, to be presented in a
    subsequent call to @ref read.

    @param msg An object used to store the message. Any contents
    will be overwritten.

    @param ec Set to the error, if any occurred.
*/
template<class SyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    message_v1<isRequest, Body, Headers>& msg,
        error_code& ec);

/** Start an asynchronous operation to read a HTTP/1 message from a stream.

    This function is used to asynchronously read a message from the
    stream. The function call always returns immediately. The asynchronous
    operation will continue until one of the following conditions is true:

    @li A complete message is read in.

    @li An error occurs on the stream.

    This operation is implemented in terms of one or more calls to the
    next layer's `async_read_some` function, and is known as a
    <em>composed operation</em>. The program must ensure that the stream
    performs no other operations until this operation completes.

    @param stream The stream to read the message from.
    The type must support the @b `AsyncReadStream` concept.

    @param streambuf A Streambuf used to hold unread bytes. The
    implementation may read past the end of the message. The extra
    bytes are stored here, to be presented in a subsequent call to
    @ref async_read.

    @param msg An object used to store the message. Any contents
    will be overwritten.

    @param handler The handler to be called when the request completes.
    Copies will be made of the handler as required. The equivalent
    function signature of the handler must be:
    @code void handler(
        error_code const& error // result of operation
    ); @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using `boost::asio::io_service::post`.
*/
template<class AsyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers,
        class ReadHandler>
#if GENERATING_DOCS
void_or_deduced
#else
typename async_completion<
    ReadHandler, void(error_code)>::result_type
#endif
async_read(AsyncReadStream& stream, Streambuf& streambuf,
    message_v1<isRequest, Body, Headers>& msg,
        ReadHandler&& handler);

} // http
} // beast

#include <beast/http/impl/read.ipp>

#endif
