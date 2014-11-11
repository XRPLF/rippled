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

#include <beast/http/basic_parser.h>
#include <beast/http/method.h>
#include <beast/http/headers.h>
#include <beast/utility/ci_char_traits.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
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
    message (message const&) = delete;
    message& operator= (message const&) = delete;

    template <class = void>
    message();

#if defined(_MSC_VER) && _MSC_VER <= 1800
    message (message&& other);
    message& operator= (message&& other);

#else
    message (message&& other) = default;
    message& operator= (message&& other) = default;

#endif

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

#if defined(_MSC_VER) && _MSC_VER <= 1800
inline
message::message (message&& other)
    : request_ (other.request_)
    , method_ (std::move(other.method_))
    , url_ (std::move(other.url_))
    , status_ (other.status_)
    , reason_ (std::move(other.reason_))
    , version_ (other.version_)
    , keep_alive_ (other.keep_alive_)
    , upgrade_ (other.upgrade_)
    , headers (std::move(other.headers))
{
}

inline
message&
message::operator= (message&& other)
{
    request_ = other.request_;
    method_ = std::move(other.method_);
    url_ = std::move(other.url_);
    status_ = other.status_;
    reason_ = std::move(other.reason_);
    version_ = other.version_;
    keep_alive_ = other.keep_alive_;
    upgrade_ = other.upgrade_;
    headers = std::move(other.headers);
    return *this;    
}
#endif

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
