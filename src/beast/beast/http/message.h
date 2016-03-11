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
namespace http2 {

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

    @tparam isReq `true` if this is a request.

    @tparam Body A type meeting the requirements of Body.

    @tparam Allocator The allocator to use.
*/
template<bool isReq, class Body, class Allocator>
struct message final
    : std::conditional_t<isReq,
        detail::request_fields, detail::response_fields>
{
    static_assert(is_Body<Body>::value,
        "Body requirements not met");

    using body_type = Body;
    using value_type = typename Body::value_type;
    using writer_type = typename Body::writer;
    using allocator_type = Allocator;

    static bool constexpr is_simple = body_type::is_simple;
    static bool constexpr is_request = isReq;

    int version; // 10 or 11
    headers<Allocator> headers;
    value_type body;

    message(message&&) = default;

    /** Construct a HTTP request.
    */
    template<class... Args>
    explicit
    message(request_params params, Args&&... args);
            
    /** Construct a HTTP response.

        @param args... Additional arguments forwarded
        to the body's constructor.
    */
    template<class... Args>
    explicit
    message(response_params params, Args&&... args);

    // Used by the parser, does not prepare
    template<class... Args>
    explicit
    message(Args&&... args);

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

} // http
} // beast

#include <beast/http/impl/message.ipp>

//------------------------------------------------------------------------------

//
// LEGACY
//

#include <beast/http/method.h>
#include <beast/http/headers.h>
#include <algorithm>
#include <cctype>
#include <ostream>
#include <string>
#include <sstream>
#include <utility>

namespace beast {
namespace http {

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
    beast::http::headers headers;

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
        write (stream, to_string(m.method()));
        write (stream, " ");
        write (stream, m.url());
        write (stream, " HTTP/");
        write (stream, std::to_string(m.version().first));
        write (stream, ".");
        write (stream, std::to_string(m.version().second));
    }
    else
    {
        write (stream, "HTTP/");
        write (stream, std::to_string(m.version().first));
        write (stream, ".");
        write (stream, std::to_string(m.version().second));
        write (stream, " ");
        write (stream, std::to_string(m.status()));
        write (stream, " ");
        write (stream, m.reason());
    }
    write (stream, "\r\n");
    write(stream, m.headers);
    write (stream, "\r\n");
}

template <class = void>
std::string
to_string (message const& m)
{
    std::stringstream ss;
    if (m.request())
        ss << to_string(m.method()) << " " << m.url() << " HTTP/" <<
            std::to_string(m.version().first) << "." <<
                std::to_string(m.version().second) << "\r\n";
    else
        ss << "HTTP/" << std::to_string(m.version().first) << "." <<
            std::to_string(m.version().second) << " " <<
                std::to_string(m.status()) << " " << m.reason() << "\r\n";
    ss << to_string(m.headers);
    ss << "\r\n";
    return ss.str();
}

} // http
} // beast

#endif
