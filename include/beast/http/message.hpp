//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_MESSAGE_HPP
#define BEAST_HTTP_MESSAGE_HPP

#include <beast/http/basic_headers.hpp>
#include <beast/type_check.hpp>
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
    /** The trait type characterizing the body.

        The body member will be of type body_type::value_type.
    */
    using body_type = Body;
    using headers_type = Headers;

    using is_request =
        std::integral_constant<bool, isRequest>;

    int version; // 10 or 11
    headers_type headers;
    typename Body::value_type body;

    message();
    message(message&&) = default;
    message(message const&) = default;
    message& operator=(message&&) = default;
    message& operator=(message const&) = default;

    /** Construct a HTTP request.
    */
    explicit
    message(request_params params);

    /** Construct a HTTP response.
    */
    explicit
    message(response_params params);

    /// Serialize the request or response line to a Streambuf.
    template<class Streambuf>
    void
    write_firstline(Streambuf& streambuf) const
    {
        write_firstline(streambuf,
            std::integral_constant<bool, isRequest>{});
    }

    /// Diagnostics only
    template<bool, class, class>
    friend
    std::ostream&
    operator<<(std::ostream& os,
        message const& m);

private:
    template<class Streambuf>
    void
    write_firstline(Streambuf& streambuf,
        std::true_type) const;

    template<class Streambuf>
    void
    write_firstline(Streambuf& streambuf,
        std::false_type) const;
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

/// Write a FieldSequence to a Streambuf.
template<class Streambuf, class FieldSequence>
void
write_fields(Streambuf& streambuf, FieldSequence const& fields);

/// Returns `true` if a message indicates a keep alive
template<bool isRequest, class Body, class Headers>
bool
is_keep_alive(message<isRequest, Body, Headers> const& msg);

/// Returns `true` if a message indicates a HTTP Upgrade request or response
template<bool isRequest, class Body, class Headers>
bool
is_upgrade(message<isRequest, Body, Headers> const& msg);

/** Connection prepare options.

    These values are used with prepare().
*/
enum class connection
{
    /// Indicates the message should specify Connection: close semantics
    close,

    /// Indicates the message should specify Connection: keep-alive semantics if possible
    keep_alive,

    /// Indicates the message should specify a Connection: upgrade
    upgrade
};

/** Prepare a message.

    This function will adjust the Content-Length, Transfer-Encoding,
    and Connection headers of the message based on the properties of
    the body and the options passed in.
*/
template<
    bool isRequest, class Body, class Headers,
    class... Options>
void
prepare(message<isRequest, Body, Headers>& msg,
    Options&&... options);

} // http
} // beast

#include <beast/http/impl/message.ipp>

#endif
