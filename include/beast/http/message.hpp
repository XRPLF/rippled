//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_MESSAGE_HPP
#define BEAST_HTTP_MESSAGE_HPP

#include <beast/http/basic_headers.hpp>
#include <beast/core/detail/integer_sequence.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

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

    /// Default constructor
    message() = default;

    /** Construct a message.

        @param u An argument forwarded to the body constructor.
    */
    template<class U>
    explicit
    message(U&& u)
        : body(std::forward<U>(u))
    {
    }

    /** Construct a message.

        @param u An argument forwarded to the body constructor.
        @param v An argument forwarded to the headers constructor.
    */
    template<class U, class V>
    message(U&& u, V&& v)
        : headers(std::forward<V>(v))
        , body(std::forward<U>(u))
    {
    }

    /** Construct a message.

        @param un A tuple forwarded as a parameter pack to the body constructor.
    */
    template<class... Un>
    message(std::piecewise_construct_t, std::tuple<Un...> un)
        : message(std::piecewise_construct, un,
            beast::detail::make_index_sequence<sizeof...(Un)>{})
    {

    }

    /** Construct a message.

        @param un A tuple forwarded as a parameter pack to the body constructor.
        @param vn A tuple forwarded as a parameter pack to the headers constructor.
    */
    template<class... Un, class... Vn>
    message(std::piecewise_construct_t,
            std::tuple<Un...>&& un, std::tuple<Vn...>&& vn)
        : message(std::piecewise_construct, un, vn,
            beast::detail::make_index_sequence<sizeof...(Un)>{},
            beast::detail::make_index_sequence<sizeof...(Vn)>{})
    {
    }

private:
    template<class... Un, size_t... IUn>
    message(std::piecewise_construct_t,
            std::tuple<Un...>& tu, beast::detail::index_sequence<IUn...>)
        : body(std::forward<Un>(std::get<IUn>(tu))...)
    {
    }

    template<class... Un, class... Vn,
        std::size_t... IUn, std::size_t... IVn>
    message(std::piecewise_construct_t,
            std::tuple<Un...>& tu, std::tuple<Vn...>& tv,
                beast::detail::index_sequence<IUn...>,
                    beast::detail::index_sequence<IVn...>)
        : headers(std::forward<Vn>(std::get<IVn>(tv))...)
        , body(std::forward<Un>(std::get<IUn>(tu))...)
    {
    }
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
