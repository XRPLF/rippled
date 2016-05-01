//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_MESSAGE_V1_HPP
#define BEAST_HTTP_MESSAGE_V1_HPP

#include <beast/http/message.hpp>
#include <memory>
#include <string>

namespace beast {
namespace http {

#if ! GENERATING_DOCS

struct request_params
{
    std::string method;
    std::string url;
    int version;
};

struct response_params
{
    int status;
    std::string reason;
    int version;
};

#endif

/** A HTTP/1 message.

    A message can be a request or response, depending on the `isRequest`
    template argument value. Requests and responses have different types,
    so functions may be overloaded on them if desired.

    The `Body` template argument type determines the model used
    to read or write the content body of the message.

    @tparam isRequest `true` if this is a request.

    @tparam Body A type meeting the requirements of Body.

    @tparam Headers A type meeting the requirements of Headers.
*/
template<bool isRequest, class Body, class Headers>
struct message_v1 : message<isRequest, Body, Headers>
{
    /// HTTP/1 version (10 or 11)
    int version;

    message_v1() = default;

    /// Construct a HTTP/1 request.
    explicit
    message_v1(request_params params);

    /// Construct a HTTP/1 response.
    explicit
    message_v1(response_params params);
};

#if ! GENERATING_DOCS

/// A typical HTTP/1 request
template<class Body,
    class Headers = basic_headers<std::allocator<char>>>
using request_v1 = message_v1<true, Body, Headers>;

/// A typical HTTP/1 response
template<class Body,
    class Headers = basic_headers<std::allocator<char>>>
using response_v1 = message_v1<false, Body, Headers>;

#endif

/// Returns `true` if a HTTP/1 message indicates a keep alive
template<bool isRequest, class Body, class Headers>
bool
is_keep_alive(message_v1<isRequest, Body, Headers> const& msg);

/// Returns `true` if a HTTP/1 message indicates an Upgrade request or response
template<bool isRequest, class Body, class Headers>
bool
is_upgrade(message_v1<isRequest, Body, Headers> const& msg);

/** HTTP/1 connection prepare options.

    @note These values are used with @ref prepare.
*/
enum class connection
{
    /// Specify Connection: close.
    close,

    /// Specify Connection: keep-alive where possible.
    keep_alive,

    /// Specify Connection: upgrade.
    upgrade
};

/** Prepare a HTTP/1 message.

    This function will adjust the Content-Length, Transfer-Encoding,
    and Connection headers of the message based on the properties of
    the body and the options passed in.

    @param msg The message to prepare. The headers may be modified.

    @param options A list of prepare options.
*/
template<
    bool isRequest, class Body, class Headers,
    class... Options>
void
prepare(message_v1<isRequest, Body, Headers>& msg,
    Options&&... options);

} // http
} // beast

#include <beast/http/impl/message_v1.ipp>

#endif
