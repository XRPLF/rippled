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

//------------------------------------------------------------------------------

#include <beast/http/method.h>
#include <beast/http/headers.h>
#include <beast/detail/ci_char_traits.hpp>
#include <beast/http/detail/writes.h>
#include <algorithm>
#include <cctype>
#include <ostream>
#include <string>
#include <sstream>
#include <utility>

namespace beast {
namespace deprecated_http {

inline
std::pair<int, int>
http_1_0()
{
    return std::pair<int, int>(1, 0);
}

inline
std::pair<int, int>
http_1_1()
{
    return std::pair<int, int>(1, 1);
}

class message
{
private:
    bool request_;

    // request
    beast::http::method_t method_;
    std::string url_;

    // response
    int status_;
    std::string reason_;

    // message
    std::pair<int, int> version_;
    bool keep_alive_;
    bool upgrade_;

public:
    ~message() = default;
    message (message const&) = default;
    message (message&& other) = default;
    message& operator= (message const&) = default;
    message& operator= (message&& other) = default;

    template <class = void>
    message();

    // Memberspace
    beast::http::headers<std::allocator<char>> headers;

    bool
    request() const
    {
        return request_;
    }

    void
    request (bool value)
    {
        request_ = value;
    }

    // Request

    void
    method (beast::http::method_t http_method)
    {
        method_ = http_method;
    }

    beast::http::method_t
    method() const
    {
        return method_;
    }

    void
    url (std::string const& s)
    {
        url_ = s;
    }

    std::string const&
    url() const
    {
        return url_;
    }

    /** Returns `false` if this is not the last message.
        When keep_alive returns `false`:
            * Server roles respond with a "Connection: close" header.
            * Client roles close the connection.
    */
    bool
    keep_alive() const
    {
        return keep_alive_;
    }

    /** Set the keep_alive setting. */
    void
    keep_alive (bool value)
    {
        keep_alive_ = value;
    }

    /** Returns `true` if this is an HTTP Upgrade message.
        @note Upgrade messages have no content body.
    */
    bool
    upgrade() const
    {
        return upgrade_;
    }

    /** Set the upgrade setting. */
    void
    upgrade (bool value)
    {
        upgrade_ = value;
    }

    int
    status() const
    {
        return status_;
    }

    void
    status (int code)
    {
        status_ = code;
    }

    std::string const&
    reason() const
    {
        return reason_;
    }

    void
    reason (std::string const& text)
    {
        reason_ = text;
    }

    // Message

    void
    version (int major, int minor)
    {
        version_ = std::make_pair (major, minor);
    }

    void
    version (std::pair<int, int> p)
    {
        version_ = p;
    }

    std::pair<int, int>
    version() const
    {
        return version_;
    }
};

//------------------------------------------------------------------------------

template <class>
message::message()
    : request_ (true)
    , method_ (beast::http::method_t::http_get)
    , url_ ("/")
    , status_ (200)
    , version_ (1, 1)
    , keep_alive_ (false)
    , upgrade_ (false)
{
}

//------------------------------------------------------------------------------

template <class Streambuf>
void
write (Streambuf& stream, message const& m)
{
    if (m.request())
    {
        http::detail::write (stream, to_string(m.method()));
        http::detail::write (stream, " ");
        http::detail::write (stream, m.url());
        http::detail::write (stream, " HTTP/");
        http::detail::write (stream, std::to_string(m.version().first));
        http::detail::write (stream, ".");
        http::detail::write (stream, std::to_string(m.version().second));
    }
    else
    {
        http::detail::write (stream, "HTTP/");
        http::detail::write (stream, std::to_string(m.version().first));
        http::detail::write (stream, ".");
        http::detail::write (stream, std::to_string(m.version().second));
        http::detail::write (stream, " ");
        http::detail::write (stream, std::to_string(m.status()));
        http::detail::write (stream, " ");
        http::detail::write (stream, m.reason());
    }
    http::detail::write (stream, "\r\n");
    write_fields(stream, m.headers);
    http::detail::write (stream, "\r\n");
}

} // deprecated_http
} // beast

#endif
