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

#ifndef BEAST_HTTP_READ_H_INCLUDED
#define BEAST_HTTP_READ_H_INCLUDED

#include <beast/http/error.h>
#include <beast/http/parser.h>
#include <beast/http/type_check.h>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

namespace beast {
namespace http {

/** Read a HTTP message from a stream.

    @param stream The stream to read the message from.

    @param streambuf A Streambuf used to hold unread bytes. The
    implementation may read past the end of the message. The extra
    bytes are stored here, to be presented in a subsequent call to
    read.

    @param msg An object used to store the read message. Any
    contents will be overwritten.

    @throws boost::system::system_error on failure.
*/
template<class SyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    message<isRequest, Body, Headers>& msg)
{
    error_code ec;
    read(stream, streambuf, msg, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

/** Read a HTTP message from a stream.

    @param stream The stream to read the message from.

    @param streambuf A Streambuf used to hold unread bytes. The
    implementation may read past the end of the message. The extra
    bytes are stored here, to be presented in a subsequent call to
    read.

    @param msg An object used to store the read message. Any
    contents will be overwritten.

    @param ec Set to the error, if any occurred.
*/
template<class SyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    message<isRequest, Body, Headers>& msg,
        error_code& ec);

/** Start reading a HTTP message from a stream asynchronously.

    @param stream The stream to read the message from.

    @param streambuf A Streambuf used to hold unread bytes. The
    implementation may read past the end of the message. The extra
    bytes are stored here, to be presented in a subsequent call to
    async_read.

    @param msg An object used to store the read message. Any
    contents will be overwritten.

    @param token The handler to be called when the request completes.
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
template<class AsyncReadStream, class Streambuf,
    bool isRequest, class Body, class Headers,
        class CompletionToken>
#if GENERATING_DOCS
void_or_deduced
#else
auto
#endif
async_read(AsyncReadStream& stream, Streambuf& streambuf,
    message<isRequest, Body, Headers>& msg,
        CompletionToken&& token);

} // http
} // beast

#include <beast/http/impl/read.ipp>

#endif
