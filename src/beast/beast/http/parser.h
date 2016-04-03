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

#include <beast/http/message.h>
#include <beast/http/body.h>
#include <functional>
#include <string>
#include <utility>

namespace beast {
namespace http {

/** Parser for HTTP messages.
    The result is stored in a message object.
*/
class parser
    : public beast::http::basic_parser<parser>
{
    friend class basic_parser<parser>;

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

private:
    void
    on_start()
    {
    }

    bool
    on_request(method_t method, std::string const& url,
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
        m_.headers.append(field, value);
    }

    void
    on_body(void const* data, std::size_t bytes)
    {
        write_body_(data, bytes);
    }

    void
    on_complete()
    {
    }
};

} // http
} // beast

#endif
