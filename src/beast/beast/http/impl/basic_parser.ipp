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

#include <beast/http/src/nodejs_parser.h>
#include <beast/http/rfc2616.h>
#include <beast/http/detail/error.h>

namespace beast {
namespace http {

template<class Derived>
basic_parser<Derived>::basic_parser(bool request) noexcept
{
    state_.data = this;
    http_parser_init(&state_, request
        ? http_parser_type::HTTP_REQUEST
        : http_parser_type::HTTP_RESPONSE);
}

template<class Derived>
auto
basic_parser<Derived>::operator=(basic_parser&& other) ->
    basic_parser&
{
    state_ = other.state_;
    state_.data = this;
    complete_ = other.complete_;
    url_ = std::move(other.url_);
    status_ = std::move(other.status_);
    field_ = std::move(other.field_);
    value_ = std::move(other.value_);
    return *this;
}

template<class Derived>
std::size_t
basic_parser<Derived>::write(void const* data,
    std::size_t size, error_code& ec)
{
    ec_ = &ec;
    auto const n = http_parser_execute(
        &state_, hooks(),
            static_cast<const char*>(data), size);
    if(! ec)
        ec = detail::make_error(
            static_cast<int>(state_.http_errno));
    if(ec)
        return 0;
    return n;
}

template<class Derived>
void
basic_parser<Derived>::write_eof(error_code& ec)
{
    ec_ = &ec;
    http_parser_execute(
        &state_, hooks(), nullptr, 0);
    if(! ec)
        ec = detail::make_error(
            static_cast<int>(state_.http_errno));
}

template<class Derived>
void
basic_parser<Derived>::check_header()
{
    if (! value_.empty())
    {
        rfc2616::trim_right_in_place(value_);
        call_on_field(field_, value_,
            has_on_field<Derived>{});
        field_.clear();
        value_.clear();
    }
}

template<class Derived>
int
basic_parser<Derived>::cb_message_start(http_parser* p)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.complete_ = false;
    t.url_.clear();
    t.status_.clear();
    t.field_.clear();
    t.value_.clear();
    t.call_on_start(has_on_start<Derived>{});
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_url(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.url_.append(in, bytes);
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_status(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.status_.append(in, bytes);
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_header_field(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.check_header();
    t.field_.append(in, bytes);
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_header_value(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.value_.append(in, bytes);
    return 0;
}

/*  Called when all the headers are complete but before
    the content body, if present.
    Returning 1 from here tells the nodejs parser
    that the message has no body (e.g. a HEAD request).
*/
template<class Derived>
int
basic_parser<Derived>::cb_headers_complete(http_parser* p)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.check_header();
    t.call_on_headers_complete(*t.ec_,
        has_on_headers_complete<Derived>{});
    if(*t.ec_)
        return 1;
    bool const keep_alive =
        http_should_keep_alive(p) != 0;
    if(p->type == http_parser_type::HTTP_REQUEST)
    {
        t.call_on_request(convert_http_method(
            http_method(p->method)), t.url_,
                p->http_major, p->http_minor, keep_alive,
                    p->upgrade, has_on_request<Derived>{});
        return 0;
    }
    return t.call_on_response(p->status_code, t.status_,
        p->http_major, p->http_minor, keep_alive,
            p->upgrade, has_on_response<Derived>{}) ? 0 : 1;
}

// Called repeatedly for the content body,
// after any transfer-encoding is applied.
template<class Derived>
int
basic_parser<Derived>::cb_body(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.call_on_body(in, bytes, *t.ec_, has_on_body<Derived>{});
    return *t.ec_ ? 1 : 0;
}

// Called when the both the headers
// and content body (if any) are complete.
template<class Derived>
int
basic_parser<Derived>::cb_message_complete(http_parser* p)
{
    auto& t = *reinterpret_cast<basic_parser*>(p->data);
    t.complete_ = true;
    t.call_on_complete(has_on_complete<Derived>{});
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_chunk_header(http_parser* p)
{
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_chunk_complete(http_parser* p)
{
    return 0;
}

} // http
} // beast
