//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_MESSAGE_HPP
#define BEAST_HTTP_MESSAGE_HPP

#include <beast/http/basic_headers.hpp>
#include <memory>
#include <string>

namespace beast {
namespace http {

namespace detail {

struct request_fields
{
    std::string method;
    std::string url;
};

struct response_fields
{
    int status;
    std::string reason;
};

} // detail

/** A HTTP message.

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
struct message
    : std::conditional<isRequest,
        detail::request_fields, detail::response_fields>::type
{
    /** The type controlling the body traits.

        The body member will be of type `body_type::value_type`.
    */
    using body_type = Body;

    /// The type representing the headers.
    using headers_type = Headers;

    /// Indicates if the message is a request.
    using is_request =
        std::integral_constant<bool, isRequest>;

    /// The container holding the headers.
    headers_type headers;

    /// A container representing the body.
    typename Body::value_type body;
};

#if ! GENERATING_DOCS

/// A typical HTTP request
template<class Body,
    class Headers = basic_headers<std::allocator<char>>>
using request = message<true, Body, Headers>;

/// A typical HTTP response
template<class Body,
    class Headers = basic_headers<std::allocator<char>>>
using response = message<false, Body, Headers>;

#endif

} // http
} // beast

#endif
