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

#include <beast/http/raw_parser.h>

#include <beast/http/impl/joyent_parser.h>

#include <utility>

namespace beast {
namespace http {

raw_parser::raw_parser (callback& cb)
    : m_cb (cb)
{
    static_assert (sizeof(joyent::http_parser) == sizeof(state_t),
        "state_t size must match http_parser size");

    static_assert (sizeof(joyent::http_parser_settings) == sizeof(hooks_t),
        "hooks_t size must match http_parser_settings size");

    auto s (reinterpret_cast <joyent::http_parser*> (&m_state));
    s->data = this;

    auto h (reinterpret_cast <joyent::http_parser_settings*> (&m_hooks));
    h->on_message_begin = &raw_parser::on_message_start;
    h->on_url = &raw_parser::on_url;
    h->on_status = &raw_parser::on_status;
    h->on_header_field = &raw_parser::on_header_field;
    h->on_header_value = &raw_parser::on_header_value;
    h->on_headers_complete = &raw_parser::on_headers_done;
    h->on_body = &raw_parser::on_body;
    h->on_message_complete = &raw_parser::on_message_complete;
}

raw_parser::~raw_parser()
{
}

void
raw_parser::reset (message_type type)
{
    auto s (reinterpret_cast <joyent::http_parser*> (&m_state));
    http_parser_init (s, (type == request)
        ? joyent::HTTP_REQUEST : joyent::HTTP_RESPONSE);
}

auto
raw_parser::process_data (void const* buf, std::size_t bytes) ->
    std::pair <error_code, std::size_t>
{
    auto s (reinterpret_cast <joyent::http_parser*> (&m_state));
    auto h (reinterpret_cast <joyent::http_parser_settings const*> (&m_hooks));
    std::size_t const bytes_used (joyent::http_parser_execute (s, h,
        static_cast <const char*> (buf), bytes));
    return std::make_pair (m_ec, bytes_used);;
}

auto
raw_parser::process_eof () ->
    error_code
{
    auto s (reinterpret_cast <joyent::http_parser*> (&m_state));
    auto h (reinterpret_cast <joyent::http_parser_settings const*> (&m_hooks));
    joyent::http_parser_execute (s, h, nullptr, 0);
    return m_ec;
}

//------------------------------------------------------------------------------

int
raw_parser::do_message_start ()
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&m_state));
    if (p->type == joyent::HTTP_REQUEST)
        m_ec = m_cb.get().on_request ();
    else if (p->type == joyent::HTTP_RESPONSE)
        m_ec = m_cb.get().on_response ();
    return m_ec ? 1 : 0;
}

int
raw_parser::do_url (char const* in, std::size_t bytes)
{
    m_ec = m_cb.get().on_url (in, bytes);
    return m_ec ? 1 : 0;
}

int
raw_parser::do_status (char const* in, std::size_t bytes)
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&m_state));
    m_ec = m_cb.get().on_status (p->status_code, in, bytes);
    return m_ec ? 1 : 0;
}

int
raw_parser::do_header_field (char const* in, std::size_t bytes)
{
    m_ec = m_cb.get().on_header_field (in, bytes);
    return m_ec ? 1 : 0;
}

int
raw_parser::do_header_value (char const* in, std::size_t bytes)
{
    m_ec = m_cb.get().on_header_value (in, bytes);
    return m_ec ? 1 : 0;
}

int
raw_parser::do_headers_done ()
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&m_state));
    bool const keep_alive (joyent::http_should_keep_alive (p) != 0);
    m_ec = m_cb.get().on_headers_done (keep_alive);
    return m_ec ? 1 : 0;
}

int
raw_parser::do_body (char const* in, std::size_t bytes)
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&m_state));
    bool const is_final (
        joyent::http_body_is_final (p) != 0);
    m_ec = m_cb.get().on_body (is_final, in, bytes);
    return m_ec ? 1 : 0;
}

int
raw_parser::do_message_complete ()
{
    auto const p (reinterpret_cast <joyent::http_parser const*> (&m_state));
    bool const keep_alive (joyent::http_should_keep_alive (p) != 0);
    m_ec = m_cb.get().on_message_complete (keep_alive);
    return m_ec ? 1 : 0;
}

//------------------------------------------------------------------------------

int
raw_parser::on_message_start (joyent::http_parser* p)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_message_start();
}

int
raw_parser::on_url (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_url (in, bytes);
}

int
raw_parser::on_status (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_status (in, bytes);
}

int
raw_parser::on_header_field (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_header_field (in, bytes);
}

int
raw_parser::on_header_value (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_header_value (in, bytes);
}

int
raw_parser::on_headers_done (joyent::http_parser* p)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_headers_done();
}

int
raw_parser::on_body (joyent::http_parser* p,
    char const* in, std::size_t bytes)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_body (
            in, bytes);
}

int
raw_parser::on_message_complete (joyent::http_parser* p)
{
    return reinterpret_cast <raw_parser*> (
        p->data)->do_message_complete();
}

//------------------------------------------------------------------------------

auto
raw_parser::callback::on_request () ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_response () ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_url(
    void const*, std::size_t) ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_status (int,
    void const*, std::size_t) ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_header_field (
    void const*, std::size_t) ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_header_value (
    void const*, std::size_t) ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_headers_done (
    bool) ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_body (
    bool, void const*, std::size_t) ->
    error_code
{
    return error_code();
}

auto
raw_parser::callback::on_message_complete (
    bool) ->
    error_code
{
    return error_code();
}

}
}
