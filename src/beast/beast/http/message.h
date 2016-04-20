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

#ifndef BEAST_HTTP_MESSAGE_H_INCLUDED
#define BEAST_HTTP_MESSAGE_H_INCLUDED

#include <beast/http/headers.h>
#include <beast/http/method.h>
#include <beast/asio/buffers_debug.h>
#include <beast/asio/type_check.h>
#include <memory>
#include <ostream>
#include <string>

namespace beast {
namespace http {

namespace detail {

struct request_fields
{
    http::method_t method;
    std::string url;
};

struct response_fields
{
    int status;
    std::string reason;
};

} // detail

struct request_params
{
    http::method_t method;
    std::string url;
    int version;
};

struct response_params
{
    int status;
    std::string reason;
    int version;
};

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
    : std::conditional_t<isRequest,
        detail::request_fields, detail::response_fields>
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

// For diagnostic output only
template<bool isRequest, class Body, class Headers>
std::ostream&
operator<<(std::ostream& os,
    message<isRequest, Body, Headers> const& m);

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

} // http
} // beast

#include <beast/http/impl/message.ipp>

#endif
