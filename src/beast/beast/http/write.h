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

#ifndef BEAST_HTTP_WRITE_H_INCLUDED
#define BEAST_HTTP_WRITE_H_INCLUDED

#include <beast/http/error.h>
#include <beast/http/message.h>
#include <boost/system/error_code.hpp>
#include <type_traits>

namespace beast {
namespace http {

/** Write a HTTP message to a stream.

    @param stream The stream to send the message on.

    @param msg The message to send.

    @throws boost::system::error code on failure.
*/
template<class SyncWriteStream,
    bool isRequest, class Body, class Headers>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Headers> const& msg)
{
    error_code ec;
    write(stream, msg, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

/** Write a HTTP message to a stream.

    @param stream The stream to send the message on.

    @param msg The message to send.

    @param ec Set to the error, if any occurred.
*/
template<class SyncWriteStream,
    bool isRequest, class Body, class Headers>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Headers> const& msg,
        error_code& ec);

/** Start writing a HTTP message to a stream asynchronously.

    @param stream The stream to send the message on.

    @param msg The message to send.

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

    @note The message must remain valid at least until the
          completion handler is called, no copies are made.
*/
template<class AsyncWriteStream,
    bool isRequest, class Body, class Headers,
        class CompletionToken>
#if GENERATING_DOCS
void_or_deduced
#else
auto
#endif
async_write(AsyncWriteStream& stream,
    message<isRequest, Body, Headers> const& msg,
        CompletionToken&& token);

} // http
} // beast

#include <beast/http/impl/write.ipp>

#endif
