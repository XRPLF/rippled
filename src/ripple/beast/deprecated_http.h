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

#ifndef BEAST_DEPRECATED_HTTP_H
#define BEAST_DEPRECATED_HTTP_H

#include <beast/http/method.hpp>
#include <beast/http/headers.hpp>
#include <beast/http/basic_parser.hpp>
#include <beast/http/rfc2616.hpp>
#include <beast/write_streambuf.hpp>
#include <beast/test/http/nodejs_parser.hpp>
#include <beast/detail/ci_char_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace beast {
namespace deprecated_http {

/** Container for the HTTP content-body. */
class body
{
private:
    using buffer_type = boost::asio::streambuf;

    // Hack: use unique_ptr because streambuf cant be moved
    std::unique_ptr <buffer_type> buf_;

public:
    using const_buffers_type = buffer_type::const_buffers_type;

    body();
    body (body&& other);
    body& operator= (body&& other);

    body (body const&) = delete;
    body& operator= (body const&) = delete;

    template <class = void>
    void
    clear();

    void
    write (void const* data, std::size_t bytes);

    template <class ConstBufferSequence>
    void
    write (ConstBufferSequence const& buffers);

    std::size_t
    size() const;

    const_buffers_type
    data() const;
};

template <class = void>
std::string
to_string (body const& b)
{
    std::string s;
    auto const& data (b.data());
    auto const n (boost::asio::buffer_size (data));
    s.resize (n);
    boost::asio::buffer_copy (
        boost::asio::buffer (&s[0], n), data);
    return s;
}

//------------------------------------------------------------------------------

inline
body::body()
    : buf_ (std::make_unique <buffer_type>())
{
}

inline
body::body (body&& other)
    : buf_ (std::move(other.buf_))
{
    other.clear();
}

inline
body&
body::operator= (body&& other)
{
    buf_ = std::move(other.buf_);
    other.clear();
    return *this;
}

template <class>
void
body::clear()
{
    buf_ = std::make_unique <buffer_type>();
}

inline
void
body::write (void const* data, std::size_t bytes)
{
    buf_->commit (boost::asio::buffer_copy (buf_->prepare (bytes),
        boost::asio::const_buffers_1 (data, bytes)));
}

template <class ConstBufferSequence>
void
body::write (ConstBufferSequence const& buffers)
{
    for (auto const& buffer : buffers)
        write (boost::asio::buffer_cast <void const*> (buffer),
            boost::asio::buffer_size (buffer));
}

inline
std::size_t
body::size() const
{
    return buf_->size();
}

inline
auto
body::data() const
    -> const_buffers_type
{
    return buf_->data();
}

//------------------------------------------------------------------------------

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
        beast::write (stream, to_string(m.method()));
        beast::write (stream, " ");
        beast::write (stream, m.url());
        beast::write (stream, " HTTP/");
        beast::write (stream, std::to_string(m.version().first));
        beast::write (stream, ".");
        beast::write (stream, std::to_string(m.version().second));
    }
    else
    {
        beast::write (stream, "HTTP/");
        beast::write (stream, std::to_string(m.version().first));
        beast::write (stream, ".");
        beast::write (stream, std::to_string(m.version().second));
        beast::write (stream, " ");
        beast::write (stream, std::to_string(m.status()));
        beast::write (stream, " ");
        beast::write (stream, m.reason());
    }
    beast::write (stream, "\r\n");
    write_fields(stream, m.headers);
    beast::write (stream, "\r\n");
}

//------------------------------------------------------------------------------

class parser
    : public beast::http::nodejs_basic_parser<parser>
{
    message& m_;
    std::function<void(void const*, std::size_t)> write_body_;
    std::string field_;
    std::string value_;

public:
    parser(parser&&) = default;
    parser(parser const&) = delete;
    parser& operator=(parser&&) = delete;
    parser& operator=(parser const&) = delete;

    /** Construct a parser for HTTP request or response.
        The headers plus request or status line are stored in message.
        The content-body, if any, is passed as a series of calls to
        the write_body function. Transfer encodings are applied before
        any data is passed to the write_body function.
    */
    parser(std::function<void(void const*, std::size_t)> write_body,
            message& m, bool request)
        : nodejs_basic_parser(request)
        , m_(m)
        , write_body_(std::move(write_body))
    {
        m_.request(request);
    }

    parser(message& m, body& b, bool request)
        : nodejs_basic_parser(request)
        , m_(m)
    {
        write_body_ = [&b](void const* data, std::size_t size)
            {
                b.write(data, size);
            };
        m_.request(request);
    }

//private:

    void flush()
    {
        if(! value_.empty())
        {
            rfc2616::trim_right_in_place(value_);
            // VFALCO could std::move
            m_.headers.insert(field_, value_);
            field_.clear();
            value_.clear();
        }
    }

    void
    on_start()
    {
    }

    void
    on_headers_complete(error_code&)
    {
    }

    bool
    on_request(http::method_t method, std::string const& url,
        int major, int minor, bool keep_alive, bool upgrade)
    {
        m_.method(method);
        m_.url(url);
        m_.version(major, minor);
        m_.keep_alive(keep_alive);
        m_.upgrade(upgrade);
        return true;
    }

    bool
    on_response(int status, std::string const& text,
        int major, int minor, bool keep_alive, bool upgrade)
    {
        m_.status(status);
        m_.reason(text);
        m_.version(major, minor);
        m_.keep_alive(keep_alive);
        m_.upgrade(upgrade);
        return true;
    }

    void
    on_field(std::string const& field, std::string const& value)
    {
        m_.headers.insert(field, value);
    }

    void
    on_body(void const* data, std::size_t bytes, error_code&)
    {
        write_body_(data, bytes);
    }

    void
    on_complete()
    {
    }
};

} // deprecated_http
} // beast

#endif
