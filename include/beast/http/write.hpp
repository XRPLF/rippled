//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_WRITE_HPP
#define BEAST_HTTP_WRITE_HPP

#include <beast/http/error.hpp>
#include <beast/http/message.hpp>
#include <beast/async_completion.hpp>
#include <beast/type_check.hpp>
#include <boost/system/error_code.hpp>
#include <ostream>
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
        class WriteHandler>
#if GENERATING_DOCS
void_or_deduced
#else
typename async_completion<
    WriteHandler, void(error_code)>::result_type
#endif
async_write(AsyncWriteStream& stream,
    message<isRequest, Body, Headers> const& msg,
        WriteHandler&& handler);

/** Serialize a message to an ostream.

    The function converts the message to its HTTP/1.* serialized
    representation and stores the result in the output stream.

    @param os The ostream to write to.

    @param msg The message to write.
*/
template<bool isRequest, class Body, class Headers>
std::ostream&
operator<<(std::ostream& os,
    message<isRequest, Body, Headers> const& msg);

} // http
} // beast

#include <beast/http/impl/write.ipp>

#endif
