//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_PARSE_HPP
#define BEAST_HTTP_PARSE_HPP

#include <beast/core/error.hpp>
#include <beast/core/async_completion.hpp>

namespace beast {
namespace http {

/** Parse an object from a stream.

    This function synchronously reads from a stream and passes
    data to the specified parser. The call will block until one
    of the following conditions are met:

    @li The parser indicates that parsing is complete.

    @li An error occurs in the stream or parser.

    This function is implemented in terms of one or more calls
    to the stream's `read_some` function. The implementation may
    read additional octets that lie past the end of the object
    being parsed. This additional data is stored in the stream
    buffer, which may be used in subsequent calls.

    @note This algorithm is generic, and not specific to HTTP
    messages. It is up to the parser to determine what predicate
    defines a complete operation.

    @param stream The stream from which the data is to be read.
    The type must support the @b SyncReadStream concept.

    @param dynabuf A @b DynamicBuffer holding additional bytes
    read by the implementation from the stream. This is both
    an input and an output parameter; on entry, any data in the
    stream buffer's input sequence will be given to the parser
    first.

    @param parser An object meeting the requirements of @b Parser
    which will receive the data.

    @throws system_error Thrown on failure.
*/
template<class SyncReadStream, class DynamicBuffer, class Parser>
void
parse(SyncReadStream& stream,
    DynamicBuffer& dynabuf, Parser& parser);

/** Parse an object from a stream.

    This function synchronously reads from a stream and passes
    data to the specified parser. The call will block until one
    of the following conditions are met:

    @li The parser indicates that parsing is complete.

    @li An error occurs in the stream or parser.

    This function is implemented in terms of one or more calls
    to the stream's `read_some` function. The implementation may
    read additional octets that lie past the end of the object
    being parsed. This additional data is stored in the stream
    buffer, which may be used in subsequent calls.

    @note This algorithm is generic, and not specific to HTTP
    messages. It is up to the parser to determine what predicate
    defines a complete operation.

    @param stream The stream from which the data is to be read.
    The type must support the @b SyncReadStream concept.

    @param dynabuf A @b DynamicBuffer holding additional bytes
    read by the implementation from the stream. This is both
    an input and an output parameter; on entry, any data in the
    stream buffer's input sequence will be given to the parser
    first.

    @param parser An object meeting the requirements of @b Parser
    which will receive the data.

    @param ec Set to the error, if any occurred.
*/
template<class SyncReadStream, class DynamicBuffer, class Parser>
void
parse(SyncReadStream& stream,
    DynamicBuffer& dynabuf, Parser& parser, error_code& ec);

/** Start an asynchronous operation to parse an object from a stream.

    This function is used to asynchronously read from a stream and
    pass the data to the specified parser. The function call always
    returns immediately. The asynchronous operation will continue
    until one of the following conditions is true:

    @li The parser indicates that parsing is complete.

    @li An error occurs in the stream or parser.

    This operation is implemented in terms of one or more calls to
    the next layer's `async_read_some` function, and is known as a
    <em>composed operation</em>. The program must ensure that the
    stream performs no other operations until this operation completes.
    The implementation may read additional octets that lie past the
    end of the object being parsed. This additional data is stored
    in the stream buffer, which may be used in subsequent calls.

    @param stream The stream from which the data is to be read.
    The type must support the @b AsyncReadStream concept.

    @param dynabuf A @b DynamicBuffer holding additional bytes
    read by the implementation from the stream. This is both
    an input and an output parameter; on entry, any data in the
    stream buffer's input sequence will be given to the parser
    first.

    @param parser An object meeting the requirements of @b Parser
    which will receive the data. This object must remain valid
    until the completion handler is invoked.

    @param handler The handler to be called when the request
    completes. Copies will be made of the handler as required.
    The equivalent function signature of the handler must be:
    @code void handler(
        error_code const& error // result of operation
    ); @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using `boost::asio::io_service::post`.
*/
template<class AsyncReadStream,
    class DynamicBuffer, class Parser, class ReadHandler>
#if GENERATING_DOCS
void_or_deduced
#else
typename async_completion<
    ReadHandler, void(error_code)>::result_type
#endif
async_parse(AsyncReadStream& stream, DynamicBuffer& dynabuf,
    Parser& parser, ReadHandler&& handler);

} // http
} // beast

#include <beast/http/impl/parse.ipp>

#endif
