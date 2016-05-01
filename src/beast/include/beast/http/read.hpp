//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_READ_HPP
#define BEAST_HTTP_READ_HPP

#include <beast/async_completion.hpp>
#include <beast/http/error.hpp>
#include <beast/http/parser.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

namespace beast {
namespace http {

/** Parse a HTTP/1 message from a stream.

    This function reads from a stream and passes data to the
    specified parser. The call will block until one of the
    following conditions are met:

    @li The parser indicates it has received a complete message.

    @li The stream returns an end of file.

    @li An error is encountered by the stream or the parser.

    This operation is implemented in terms of one or more calls
    to the stream's `read_some` function. The implementation may
    read additional octets that lie past the end of the message
    being parsed. This additional data is stored in the streambuf,
    which may be used in subsequent calls.

    @param stream The stream to parse the message from.

    @param streambuf A Streambuf holding additional bytes
    read by the implementation from the stream. This is both
    an input and an output parameter; on entry, any data in the
    stream buffer's input sequence will be given to the parser
    first.

    @param parser An object meeting the requirements of Parser
    which will receive the data.

    @throws boost::system::system_error on failure.
*/
template<class SyncReadStream, class Streambuf, class Parser>
void
parse_v1(SyncReadStream& stream,
    Streambuf& streambuf, Parser& parser)
{
    error_code ec;
    parse_v1(stream, streambuf, parser, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

/** Parse a HTTP/1 message from a stream.

    This function reads from a stream and passes data to the
    specified parser. The call will block until one of the
    following conditions are met:

    @li The parser indicates it has received a complete message.

    @li The stream returns an end of file.

    @li An error is encountered by the stream or the parser.

    This operation is implemented in terms of one or more calls
    to the stream's `read_some` function. The implementation may
    read additional octets that lie past the end of the message
    being parsed. This additional data is stored in the streambuf,
    which may be used in subsequent calls.

    @param stream The stream to parse the message from.

    @param streambuf A Streambuf holding additional bytes
    read by the implementation from the stream. This is both
    an input and an output parameter; on entry, any data in the
    stream buffer's input sequence will be given to the parser
    first.

    @param parser An object meeting the requirements of Parser
    which will receive the data.

    @param ec Set to the error, if any occurred.
*/
template<class SyncReadStream, class Streambuf, class Parser>
void
parse_v1(SyncReadStream& stream, Streambuf& streambuf,
    Parser& parser, error_code& ec);

/** Read a message in HTTP/1 format from a stream.

    This function reads from a stream and parses data in the
    HTTP/1 wire format to produce a message. The call will block
    until one of the following conditions are met:

    @li The parser indicates it has received a complete message.

    @li The stream returns an end of file.

    @li An error is encountered by the stream or the parser.

    This operation is implemented in terms of one or more calls
    to the stream's `read_some` function. The implementation may
    read additional octets that lie past the end of the message
    being read. This additional data is stored in the streambuf,
    which may be used in subsequent calls.

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

/** Read a message in HTTP/1 format from a stream.

    This function reads from a stream and parses data in the
    HTTP/1 wire format to produce a message. The call will block
    until one of the following conditions are met:

    @li The parser indicates it has received a complete message.

    @li The stream returns an end of file.

    @li An error is encountered by the stream or the parser.

    This operation is implemented in terms of one or more calls
    to the stream's `read_some` function. The implementation may
    read additional octets that lie past the end of the message
    being read. This additional data is stored in the streambuf,
    which may be used in subsequent calls.

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
    message<isRequest, Body, Headers>& msg,
        ReadHandler&& handler);

} // http
} // beast

#include <beast/http/impl/read.ipp>

#endif
