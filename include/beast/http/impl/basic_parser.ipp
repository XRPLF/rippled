//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_BASIC_PARSER_IPP
#define BEAST_HTTP_IMPL_BASIC_PARSER_IPP

#include <beast/http/impl/http_parser.h>
#include <beast/http/rfc2616.hpp>
#include <beast/http/detail/error.hpp>

namespace beast {
namespace http {

namespace detail {

inline
beast::http::method_t
convert_http_method(http_method m)
{
    using namespace beast;
    switch (m)
    {
    case HTTP_DELETE:      return http::method_t::http_delete;
    case HTTP_GET:         return http::method_t::http_get;
    case HTTP_HEAD:        return http::method_t::http_head;
    case HTTP_POST:        return http::method_t::http_post;
    case HTTP_PUT:         return http::method_t::http_put;

    // pathological
    case HTTP_CONNECT:     return http::method_t::http_connect;
    case HTTP_OPTIONS:     return http::method_t::http_options;
    case HTTP_TRACE:       return http::method_t::http_trace;

    // webdav
    case HTTP_COPY:        return http::method_t::http_copy;
    case HTTP_LOCK:        return http::method_t::http_lock;
    case HTTP_MKCOL:       return http::method_t::http_mkcol;
    case HTTP_MOVE:        return http::method_t::http_move;
    case HTTP_PROPFIND:    return http::method_t::http_propfind;
    case HTTP_PROPPATCH:   return http::method_t::http_proppatch;
    case HTTP_SEARCH:      return http::method_t::http_search;
    case HTTP_UNLOCK:      return http::method_t::http_unlock;
    case HTTP_BIND:        return http::method_t::http_bind;
    case HTTP_REBIND:      return http::method_t::http_rebind;
    case HTTP_UNBIND:      return http::method_t::http_unbind;
    case HTTP_ACL:         return http::method_t::http_acl;

    // subversion
    case HTTP_REPORT:      return http::method_t::http_report;
    case HTTP_MKACTIVITY:  return http::method_t::http_mkactivity;
    case HTTP_CHECKOUT:    return http::method_t::http_checkout;
    case HTTP_MERGE:       return http::method_t::http_merge;

    // upnp
    case HTTP_MSEARCH:     return http::method_t::http_msearch;
    case HTTP_NOTIFY:      return http::method_t::http_notify;
    case HTTP_SUBSCRIBE:   return http::method_t::http_subscribe;
    case HTTP_UNSUBSCRIBE: return http::method_t::http_unsubscribe;

    // RFC-5789
    case HTTP_PATCH:       return http::method_t::http_patch;
    case HTTP_PURGE:       return http::method_t::http_purge;

    // CalDav
    case HTTP_MKCALENDAR:  return http::method_t::http_mkcalendar;

    // RFC-2068, section 19.6.1.2
    case HTTP_LINK:        return http::method_t::http_link;
    case HTTP_UNLINK:      return http::method_t::http_unlink;
    };

    return http::method_t::http_get;
}

} // detail

template<class Derived>
basic_parser<Derived>::
basic_parser(basic_parser&& other)
{
    state_ = other.state_;
    state_.data = this;
    complete_ = other.complete_;
    url_ = std::move(other.url_);
    status_ = std::move(other.status_);
    field_ = std::move(other.field_);
    value_ = std::move(other.value_);
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
basic_parser<Derived>::
basic_parser(basic_parser const& other)
{
    state_ = other.state_;
    state_.data = this;
    complete_ = other.complete_;
    url_ = other.url_;
    status_ = other.status_;
    field_ = other.field_;
    value_ = other.value_;
}

template<class Derived>
auto
basic_parser<Derived>::
operator=(basic_parser const& other) ->
    basic_parser&
{
    state_ = other.state_;
    state_.data = this;
    complete_ = other.complete_;
    url_ = other.url_;
    status_ = other.status_;
    field_ = other.field_;
    value_ = other.value_;
    return *this;
}

template<class Derived>
basic_parser<Derived>::basic_parser(bool request) noexcept
{
    state_.data = this;
    http_parser_init(&state_, request
        ? http_parser_type::HTTP_REQUEST
        : http_parser_type::HTTP_RESPONSE);
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
        t.call_on_request(detail::convert_http_method(
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
basic_parser<Derived>::cb_chunk_header(http_parser*)
{
    return 0;
}

template<class Derived>
int
basic_parser<Derived>::cb_chunk_complete(http_parser*)
{
    return 0;
}

} // http
} // beast

#endif
