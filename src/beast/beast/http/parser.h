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

#ifndef BEAST_HTTP_PARSER_H_INCLUDED
#define BEAST_HTTP_PARSER_H_INCLUDED

#include <beast/http/basic_parser.h>
#include <beast/http/error.h>
#include <beast/http/message.h>
#include <boost/optional.hpp>
#include <functional>
#include <beast/cxx17/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace http {

template<bool isRequest, class Body,
    class Allocator = std::allocator<char>>
class parser
    : public basic_parser<parser<isRequest, Body, Allocator>>
{
public:
    using message_type =
        parsed_message<isRequest, Body, Allocator>;

private:
    std::function<message_type(void)> factory_;
    boost::optional<message_type> m_;
    boost::optional<typename message_type::body_type::reader> r_;

public:
    parser(parser&&) = default;

    template<class... Args,
        class = std::enable_if_t<std::is_constructible<
            message<isRequest, Body, Allocator>, Args...>::value>>
    explicit
    parser(Args&&... args)
        : http::basic_parser<parser>(isRequest)
    {
        setup(std::forward<Args>(args)...);
        reset();
    }

    template<class... Args,
        class = std::enable_if_t<std::is_constructible<
            message<isRequest, Body, Allocator>, Args...>::value>>
    void
    setup(Args&&... args)
    {
        factory_ = {
            // broken in gcc/clang
            // [args_ = std::forward<Args>(args)...]
            [=]
            {
                message<isRequest, Body, Allocator> m(args...);
                return message_type{std::move(m)};
            }};
    }

    message_type const&
    get() const
    {
        return m_;
    }

    message_type
    release()
    {
        return std::move(*m_);
    }

    void
    reset()
    {
        m_.emplace(factory_());
        r_.emplace(*m_);
    }

private:
    friend class http::basic_parser<parser>;

    void
    on_field(std::string const& field, std::string const& value)
    {
        m_->headers.insert(field, value);
    }

    void
    on_headers_complete(error_code& ec)
    {
        // vFALCO TODO Decode the Content-Length and
        // Transfer-Encoding, see if we can reserve the buffer.
        //
        // body.reserve(content_length)
    }

    bool
    on_request(http::method_t method, std::string const& url,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        m_->method = method;
        m_->url = url;
        m_->version = major * 10 + minor;
        m_->keep_alive = keep_alive;
        m_->upgrade = upgrade;
        return true;
    }

    bool
    on_request(http::method_t, std::string const&,
        int, int, bool, bool,
            std::false_type)
    {
        return true;
    }

    bool
    on_request(http::method_t method, std::string const& url,
        int major, int minor, bool keep_alive, bool upgrade)
    {
        return on_request(method, url,
            major, minor, keep_alive, upgrade,
                std::bool_constant<message_type::is_request>{});
    }

    bool
    on_response(int status, std::string const& reason,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        m_->status = status;
        m_->reason = reason;
        m_->version = major * 10 + minor;
        m_->keep_alive = keep_alive;
        m_->upgrade = upgrade;
        // VFALCO TODO return expect_body_
        return true;
    }
    
    bool
    on_response(int, std::string const&, int, int, bool, bool,
        std::false_type)
    {
        return true;
    }

    bool
    on_response(int status, std::string const& reason,
        int major, int minor, bool keep_alive, bool upgrade)
    {
        return on_response(
            status, reason, major, minor, keep_alive, upgrade,
                std::bool_constant<! message_type::is_request>{});
    }

    void
    on_body(void const* data,
        std::size_t size, error_code& ec)
    {
        r_->write(data, size);
    }

    void
    on_complete()
    {
        r_.reset();
    }
};

template<class Body,
    class Allocator = std::allocator<char>>
using request_parser = parser<true, Body, Allocator>;

template<class Body,
    class Allocator = std::allocator<char>>
using response_parser = parser<false, Body, Allocator>;

} // http
} // beast

//------------------------------------------------------------------------------

//
// LEGACY
//

#include <beast/http/basic_parser.h>
#include <beast/http/message.h>
#include <beast/http/body.h>
#include <functional>
#include <string>
#include <utility>

namespace beast {
namespace deprecated_http {

/** Parser for HTTP messages.
    The result is stored in a message object.
*/
class parser
    : public beast::http::basic_parser<parser>
{
//    friend class basic_parser<parser>;

    message& m_;
    std::function<void(void const*, std::size_t)> write_body_;

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
        : basic_parser(request)
        , m_(m)
        , write_body_(std::move(write_body))
    {
        m_.request(request);
    }

    parser(message& m, body& b, bool request)
        : basic_parser(request)
        , m_(m)
    {
        write_body_ = [&b](void const* data, std::size_t size)
            {
                b.write(data, size);
            };
        m_.request(request);
    }

//private:
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
