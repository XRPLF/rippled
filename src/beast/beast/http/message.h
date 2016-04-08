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
#include <beast/http/type_check.h>
#include <beast/asio/type_check.h>
#include <memory>
#include <string>
#include <beast/cxx17/type_traits.h> // <type_traits>

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

    @tparam isRequest `true` if this is a request.

    @tparam Body A type meeting the requirements of Body.

    @tparam Allocator The allocator to use.
*/
template<bool isRequest, class Body, class Allocator>
struct message
    : std::conditional_t<isRequest,
        detail::request_fields, detail::response_fields>
{
    static_assert(is_Body<Body>::value,
        "Body requirements not met");

    using body_type = Body;
    using value_type = typename Body::value_type;
    using writer_type = typename Body::writer;
    using allocator_type = Allocator;

    static bool constexpr is_simple = body_type::is_simple;
    static bool constexpr is_request = isRequest;

    int version; // 10 or 11
    beast::http::headers<Allocator> headers;
    value_type body;

    message() = default;
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

    /** Serialize the entire message to a Streambuf.
    */
    template<class Streambuf,
        class = std::enable_if_t<is_simple>>
    void
    write(Streambuf& streambuf) const;

    /** Serialize all but the body to a Streambuf.
    */
    template<class Streambuf>
    void
    write_headers(Streambuf& streambuf) const
    {
        static_assert(is_Streambuf<Streambuf>{},
            "Streambuf requirements not met");
        write_headers(streambuf,
              std::bool_constant<is_request>{});
    }

private:
    template<class Streambuf>
    void
    write_headers(Streambuf& streambuf,
        std::true_type) const;

    template<class Streambuf>
    void
    write_headers(Streambuf& streambuf,
            std::false_type) const;
};

template<class Body, class Allocator = std::allocator<char>>
using request = message<true, Body, Allocator>;

template<class Body, class Allocator = std::allocator<char>>
using response = message<false, Body, Allocator>;

//------------------------------------------------------------------------------

/** A parsed HTTP message.

    The parsed information is produced by the HTTP parser.
*/
template<bool isRequest, class Body, class Allocator>
class parsed_message
    : public message<isRequest, Body, Allocator>
{
    using base_type =
        message<isRequest, Body, Allocator>;

public:
    bool keep_alive;
    bool upgrade;

    parsed_message() = default;
    parsed_message(parsed_message&&) = default;
    parsed_message(parsed_message const&) = default;
    parsed_message& operator=(parsed_message&&) = default;
    parsed_message& operator=(parsed_message const&) = default;
};

template<class Body, class Allocator = std::allocator<char>>
using parsed_request = parsed_message<true, Body, Allocator>;

template<class Body, class Allocator = std::allocator<char>>
using parsed_response = parsed_message<false, Body, Allocator>;

template<
    class RespBody, class RespAllocator,
    class ReqBody, class ReqAllocator>
response<RespBody, RespAllocator>&
set_keep_alive(bool keep_alive,
    response<RespBody, RespAllocator>& resp,
        parsed_request<ReqBody, ReqAllocator> const& req)
{
    keep_alive = keep_alive && req.keep_alive;
    if(keep_alive)
    {
        if(req.version < 11)
            resp.headers.replace("Connection", "Keep-Alive");
        else
            resp.headers.erase("Connection");
    }
    else
    {
        if(req.version >= 11)
            resp.headers.replace("Connection", "Close");
        else
            resp.headers.erase("Connection");
    }
    return resp;
}

//------------------------------------------------------------------------------

enum connection_value
{
    close,
    keep_alive,
    upgrade
};

struct connection
{
    connection_value value;

    connection(connection_value value_)
        : value(value_)
    {
    }

    /// Convert bool to keep-alive flag
    connection(bool keep_alive)
        : value(keep_alive ?
            connection_value::keep_alive :
                connection_value::close)
    {
    }
};

/** A prepared HTTP message. */
template<bool isRequest, class Body, class Allocator>
struct prepared_message : public message<isRequest, Body, Allocator>
{
    bool keep_alive;
    bool upgrade;

    prepared_message() = default;
    prepared_message(prepared_message&&) = default;
    prepared_message(prepared_message const&) = default;
    prepared_message& operator=(prepared_message&&) = default;
    prepared_message& operator=(prepared_message const&) = default;

    /** Move-construct a prepared request.

        @param msg The request to prepare.
    */
    template<class... Opts>
    explicit
    prepared_message(
        request<Body, Allocator>&& msg,
            Opts&&... opts);

    /** Copy-construct a prepared request.

        @param msg The request to prepare.
    */
    template<class... Opts>
    explicit
    prepared_message(
        request<Body, Allocator> const& msg,
            Opts&&... opts);

    /** Move-construct a prepared response.

        @param msg The response to prepare.

        @param req The request we are preparing the response for.
    */
    template<class ReqBody, class ReqAllocator, class... Opts>
    prepared_message(response<Body, Allocator>&& msg,
        parsed_request<ReqBody, ReqAllocator> const& req,
            Opts&&... opts);

    /** Copy-construct a prepared response.

        @param msg The response to prepare.

        @param req The request we are preparing the response for.
    */
    template<class ReqBody, class ReqAllocator, class... Opts>
    prepared_message(response<Body, Allocator> const& msg,
        parsed_request<ReqBody, ReqAllocator> const& req,
            Opts&&... opts);

private:
    template<class... Opts>
    void
    construct(Opts&&... opts);

    template<class ReqBody, class ReqAllocator,
        class... Opts>
    void
    construct(parsed_request<ReqBody, ReqAllocator> const& req,
        Opts&&... opts);
};

template<class Body,
    class Allocator = std::allocator<char>>
using prepared_request =
    prepared_message<true, Body, Allocator>;

template<class Body,
    class Allocator = std::allocator<char>>
using prepared_response =
    prepared_message<false, Body, Allocator>;

/** Prepare a HTTP message.

    HTTP messages must be prepared before being sent. This process
    combines information about the corresponding request (for sending
    responses) and information about the body to transform the
    content of the headers. The Body associated with the message
    is given the opportunity to apply further transformations,
    allowing Body-specific customization. For example, a SinglePass
    text_body may set Content-Length to the size, and Content-Type
    to application/text.

    During preparation, metadata about the message is calculated
    and stored in the prepared_message portion of the object.
*/
template<class Body, class Allocator,
    class... Opts>
auto
prepare(request<Body, Allocator>&& msg,
    Opts&&... opts)
{
    return prepared_message<true, Body, Allocator>{
        std::move(msg), std::forward<Opts>(opts)...};
}

template<class Body, class Allocator,
    class... Opts>
auto
prepare(request<Body, Allocator> const& msg,
    Opts&&... opts)
{
    return prepared_message<true, Body, Allocator>{
        msg, std::forward<Opts>(opts)...};
}

template<class Body, class Allocator,
    class ReqBody, class ReqAllocator,
        class... Opts>
auto
prepare(response<Body, Allocator>&& msg,
    parsed_request<ReqBody, ReqAllocator> const& req,
        Opts&&... opts)
{
    return prepared_message<false, Body, Allocator>{
        std::move(msg), req, std::forward<Opts>(opts)...};
}

template<class Body, class Allocator,
    class ReqBody, class ReqAllocator,
        class... Opts>
auto
prepare(response<Body, Allocator> const& msg,
    parsed_request<ReqBody, ReqAllocator> const& req,
        Opts&&... opts)
{
    return prepared_message<false, Body, Allocator>{
        msg, req, std::forward<Opts>(opts)...};
}

} // http
} // beast

#include <beast/http/impl/message.ipp>

//------------------------------------------------------------------------------

#include <beast/http/method.h>
#include <beast/http/headers.h>
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
    m.headers.write(stream);
    http::detail::write (stream, "\r\n");
}

} // deprecated_http
} // beast

#endif
