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

/** A HTTP parser.

    The parser may only be used once.
*/
template<bool isRequest, class Body, class Headers>
class parser
    : public basic_parser<parser<isRequest, Body, Headers>>
{
    using message_type =
        message<isRequest, Body, Headers>;

    message_type m_;
    typename message_type::body_type::reader r_;
    bool started_ = false;

public:
    parser(parser&&) = default;

    parser()
        : http::basic_parser<parser>(isRequest)
        , r_(m_)
    {
    }

    /// Returns `true` if at least one byte has been processed
    bool
    started()
    {
        return started_;
    }

    message_type
    release()
    {
        return std::move(m_);
    }

private:
    friend class http::basic_parser<parser>;

    void
    on_start()
    {
        started_ = true;
    }

    void
    on_field(std::string const& field, std::string const& value)
    {
        m_.headers.insert(field, value);
    }

    void
    on_headers_complete(error_code&)
    {
        // vFALCO TODO Decode the Content-Length and
        // Transfer-Encoding, see if we can reserve the buffer.
        //
        // r_.reserve(content_length)
    }

    bool
    on_request(http::method_t method, std::string const& url,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        m_.method = method;
        m_.url = url;
        m_.version = major * 10 + minor;
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
                typename message_type::is_request{});
    }

    bool
    on_response(int status, std::string const& reason,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        m_.status = status;
        m_.reason = reason;
        m_.version = major * 10 + minor;
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
                std::integral_constant<bool, ! message_type::is_request::value>{});
    }

    void
    on_body(void const* data,
        std::size_t size, error_code& ec)
    {
        r_.write(data, size, ec);
    }

    void
    on_complete()
    {
    }
};

} // http
} // beast

#endif
