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

#include <beast/http/basic_parser.h>
#include <beast/http/impl/joyent_parser.h>
#include <beast/http/rfc2616.h>
#include <beast/utility/noexcept.h>
#include <boost/system/error_code.hpp>

namespace beast {
namespace http {

boost::system::error_category const&
message_category() noexcept
{
    class message_category_t : public boost::system::error_category
    {
    public:
        const char*
        name() const noexcept override
        {
            return "http::message";
        }

        std::string
        message (int ev) const override
        {
            return joyent::http_errno_description (
                static_cast<joyent::http_errno>(ev));
        }

        boost::system::error_condition
        default_error_condition (int ev) const noexcept override
        {
            return boost::system::error_condition (ev, *this);
        }

        bool
        equivalent (int ev, boost::system::error_condition const& condition
            ) const noexcept override
        {
            return condition.value() == ev &&
                &condition.category() == this;
        }

        bool
        equivalent (boost::system::error_code const& error,
            int ev) const noexcept override
        {
            return error.value() == ev &&
                &error.category() == this;
        }
    };

    static message_category_t cat;
    return cat;
}

//------------------------------------------------------------------------------

basic_parser::basic_parser (bool request) noexcept
{
    static_assert (sizeof(joyent::http_parser) == sizeof(state_t),
        "state_t size must match http_parser size");

    static_assert (sizeof(joyent::http_parser_settings) == sizeof(hooks_t),
        "hooks_t size must match http_parser_settings size");

    auto s (reinterpret_cast <joyent::http_parser*> (&state_));
    s->data = this;

    auto h (reinterpret_cast <joyent::http_parser_settings*> (&hooks_));
    h->on_message_begin     = &basic_parser::cb_message_start;
    h->on_url               = &basic_parser::cb_url;
    h->on_status            = &basic_parser::cb_status;
    h->on_header_field      = &basic_parser::cb_header_field;
    h->on_header_value      = &basic_parser::cb_header_value;
    h->on_headers_complete  = &basic_parser::cb_headers_complete;
    h->on_body              = &basic_parser::cb_body;
    h->on_message_complete  = &basic_parser::cb_message_complete;
    
    joyent::http_parser_init (s, request
        ? joyent::http_parser_type::HTTP_REQUEST
        : joyent::http_parser_type::HTTP_RESPONSE);
}

basic_parser&
basic_parser::operator= (basic_parser&& other)
{
    *reinterpret_cast<joyent::http_parser*>(&state_) =
        *reinterpret_cast<joyent::http_parser*>(&other.state_);
    reinterpret_cast<joyent::http_parser*>(&state_)->data = this;
    complete_ = other.complete_;
    url_ = std::move (other.url_);
    status_ = std::move (other.status_);
    field_ = std::move (other.field_);
    value_ = std::move (other.value_);
    return *this;
}

auto
basic_parser::write (void const* data, std::size_t bytes) ->
    std::pair <error_code, std::size_t>
{
    std::pair <error_code, std::size_t> result ({}, 0);
    auto s (reinterpret_cast <joyent::http_parser*> (&state_));
    auto h (reinterpret_cast <joyent::http_parser_settings const*> (&hooks_));
    result.second = joyent::http_parser_execute (s, h,
        static_cast <const char*> (data), bytes);
    result.first = error_code{static_cast<int>(s->http_errno),
        message_category()};
    return result;
}

auto
basic_parser::write_eof() ->
    error_code
{
    auto s (reinterpret_cast <joyent::http_parser*> (&state_));
    auto h (reinterpret_cast <joyent::http_parser_settings const*> (&hooks_));
    joyent::http_parser_execute (s, h, nullptr, 0);
    return error_code{static_cast<int>(s->http_errno),
        message_category()};
}

//------------------------------------------------------------------------------

void
basic_parser::check_header()
{
    if (! value_.empty())
    {
        rfc2616::trim_right_in_place (value_);
        on_field (field_, value_);
        field_.clear();
        value_.clear();
    }
}

int
basic_parser::do_message_start ()
{
    complete_ = false;
    url_.clear();
    status_.clear();
    field_.clear();
    value_.clear();
    on_start();
    return 0;
}

int
basic_parser::do_url (char const* in, std::size_t bytes)
{
    url_.append (static_cast <char const*> (in), bytes);
    return 0;
}

int
basic_parser::do_status (char const* in, std::size_t bytes)
{
    status_.append (static_cast <char const*> (in), bytes);
    return 0;
}

int
basic_parser::do_header_field (char const* in, std::size_t bytes)
{
    check_header();
    field_.append (static_cast <char const*> (in), bytes);
    return 0;
}

int
basic_parser::do_header_value (char const* in, std::size_t bytes)
{
    value_.append (static_cast <char const*> (in), bytes);
    return 0;
}

/*  Called when all the headers are complete but before
    the content body, if present.
    Returning 1 from here tells the joyent parser
    that the message has no body (e.g. a HEAD request).
*/
int
basic_parser::do_headers_complete()
{
    check_header();
    auto const p (reinterpret_cast <joyent::http_parser const*> (&state_));
    bool const keep_alive (joyent::http_should_keep_alive (p) != 0);
    if (p->type == joyent::http_parser_type::HTTP_REQUEST)
        return on_request (joyent::convert_http_method (
            joyent::http_method(p->method)), url_,
                p->http_major, p->http_minor, keep_alive, p->upgrade) ? 0 : 1;
    return on_response (p->status_code, status_,
        p->http_major, p->http_minor, keep_alive, p->upgrade) ? 0 : 1;
}

/*  Called repeatedly for the content body. The passed buffer
    has already had the transfer-encoding removed.
*/
int
basic_parser::do_body (char const* in, std::size_t bytes)
{
    on_body (in, bytes);
    return 0;
}

/* Called when the both the headers and content body (if any) are complete. */
int
basic_parser::do_message_complete ()
{
    complete_ = true;
    on_complete();
    return 0;
}

//------------------------------------------------------------------------------

int
basic_parser::cb_message_start (joyent::http_parser* p)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_message_start();
}

int
basic_parser::cb_url (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_url (in, bytes);
}

int
basic_parser::cb_status (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_status (in, bytes);
}

int
basic_parser::cb_header_field (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_header_field (in, bytes);
}

int
basic_parser::cb_header_value (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_header_value (in, bytes);
}

int
basic_parser::cb_headers_complete (joyent::http_parser* p)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_headers_complete();
}

int
basic_parser::cb_body (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_body (
            in, bytes);
}

int
basic_parser::cb_message_complete (joyent::http_parser* p)
{
    return reinterpret_cast <basic_parser*> (
        p->data)->do_message_complete();
}

} // http
} // beast
