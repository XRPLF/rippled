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

#include <beast/http/message_parser.h>
#include <beast/http/impl/joyent_parser.h>

namespace beast {
namespace http {

message_parser::message_parser (bool request)
    : complete_ (false)
    , checked_url_ (false)
{
    static_assert (sizeof(joyent::http_parser) == sizeof(state_t),
        "state_t size must match http_parser size");

    static_assert (sizeof(joyent::http_parser_settings) == sizeof(hooks_t),
        "hooks_t size must match http_parser_settings size");

    auto s (reinterpret_cast <joyent::http_parser*> (&state_));
    s->data = this;

    auto h (reinterpret_cast <joyent::http_parser_settings*> (&hooks_));
    h->on_message_begin     = &message_parser::cb_message_start;
    h->on_url               = &message_parser::cb_url;
    h->on_status            = &message_parser::cb_status;
    h->on_header_field      = &message_parser::cb_header_field;
    h->on_header_value      = &message_parser::cb_header_value;
    h->on_headers_complete  = &message_parser::cb_headers_done;
    h->on_body              = &message_parser::cb_body;
    h->on_message_complete  = &message_parser::cb_message_complete;

    joyent::http_parser_init (s, request
        ? joyent::http_parser_type::HTTP_REQUEST
        : joyent::http_parser_type::HTTP_RESPONSE);
}

std::pair <message_parser::error_code, std::size_t>
message_parser::write_one (void const* in, std::size_t bytes)
{
    std::pair <error_code, std::size_t> result (error_code(), 0);
    auto s (reinterpret_cast <joyent::http_parser*> (&state_));
    auto h (reinterpret_cast <joyent::http_parser_settings const*> (&hooks_));
    result.second = joyent::http_parser_execute (s, h,
        static_cast <const char*> (in), bytes);
    result.first = ec_;
    return result;
}

//------------------------------------------------------------------------------

int
message_parser::check_url()
{
    if (! checked_url_)
    {
        checked_url_ = true;
        auto const p (reinterpret_cast <joyent::http_parser const*> (&state_));
        ec_ = on_request (joyent::convert_http_method (
            joyent::http_method(p->method)), p->http_major, p->http_minor, url_);
        if (ec_)
            return 1;
    }
    return 0;
}

int
message_parser::do_message_start ()
{
    return ec_ ? 1 : 0;
}

int
message_parser::do_url (char const* in, std::size_t bytes)
{
    url_.append (static_cast <char const*> (in), bytes);
    return 0;
}

int
message_parser::do_status (char const* in, std::size_t bytes)
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&state_));
    return ec_ ? 1 : 0;
}

int
message_parser::do_header_field (char const* in, std::size_t bytes)
{
    if (check_url())
        return 1;
    if (! value_.empty())
    {
        ec_ = on_field (field_, value_);
        if (ec_)
            return 1;
        field_.clear();
        value_.clear();
    }
    field_.append (static_cast <char const*> (in), bytes);
    return 0;
}

int
message_parser::do_header_value (char const* in, std::size_t bytes)
{
    value_.append (static_cast <char const*> (in), bytes);
    return 0;
}

// Returning 1 from here tells the joyent parser
// that the message has no body (e.g. a HEAD request).
//
int
message_parser::do_headers_done ()
{
    if (check_url())
        return 1;
    auto const p (reinterpret_cast <joyent::http_parser const*> (&state_));
    bool const keep_alive (joyent::http_should_keep_alive (p) != 0);
    if (! value_.empty())
    {
        ec_ = on_field (field_, value_);
        if (ec_)
            return 1;
        field_.clear();
        value_.clear();
    }
    return ec_ ? 1 : 0;
}

int
message_parser::do_body (char const* in, std::size_t bytes)
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&state_));
    bool const is_final (
        joyent::http_body_is_final (p) != 0);
    return ec_ ? 1 : 0;
}

int
message_parser::do_message_complete ()
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&state_));
    bool const keep_alive (joyent::http_should_keep_alive (p) != 0);
    complete_ = true;
    return 0;
}

//------------------------------------------------------------------------------

int
message_parser::cb_message_start (joyent::http_parser* p)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_message_start();
}

int
message_parser::cb_url (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_url (in, bytes);
}

int
message_parser::cb_status (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_status (in, bytes);
}

int
message_parser::cb_header_field (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_header_field (in, bytes);
}

int
message_parser::cb_header_value (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_header_value (in, bytes);
}

int
message_parser::cb_headers_done (joyent::http_parser* p)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_headers_done();
}

int
message_parser::cb_body (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_body (
            in, bytes);
}

int
message_parser::cb_message_complete (joyent::http_parser* p)
{
    return reinterpret_cast <message_parser*> (
        p->data)->do_message_complete();
}

} // http
} // beast
