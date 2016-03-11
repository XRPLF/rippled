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

#ifndef BEAST_HTTP_HTTP_PARSER_H_INCLUDED
#define BEAST_HTTP_HTTP_PARSER_H_INCLUDED

#include <beast/http/detail/http_parser_base.h>
#include <beast/cxx17/type_traits.h> // <type_traits>

namespace beast {
namespace http2 {

template<class Message>
class parser
    : public http::basic_parser<parser<Message>>
{
    std::function<Message(void)> factory_;
    boost::optional<Message> m_;
    boost::optional<typename Message::body_type::reader> r_;
    bool keep_alive_;
    bool upgrade_;

public:
    using message_type = Message;

    parser(parser&&) = default;

    template<class... Args,
        class = std::enable_if_t<std::is_constructible<
            message_type, Args...>::value>>
    explicit
    parser(Args&&... args)
        : http::basic_parser<parser>(
            Message::is_request)
    {
        setup(std::forward<Args>(args)...);
        reset();
    }

    template<class... Args,
        class = std::enable_if_t<std::is_constructible<
            message_type, Args...>::value>>
    void
    setup(Args&&... args)
    {
        factory_ = {
        #ifndef _MSC_VER
            // Broken in MSVC
            [args_ = std::forward<Args>(args)...]
            {
                return message_type{args_...};
            }};
        #else
            [=]
            {
                return message_type{args...};
            }};
        #endif
    }

    bool
    keep_alive() const
    {
        return keep_alive_;
    }

    bool
    upgrade() const
    {
        return upgrade_;
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
        keep_alive_ = keep_alive;
        upgrade_ = upgrade_;
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
        keep_alive_ = keep_alive;
        upgrade_ = upgrade_;
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

} // http
} // beast

#endif
