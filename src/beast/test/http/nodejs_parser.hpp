//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_NODEJS_PARSER_HPP
#define BEAST_HTTP_NODEJS_PARSER_HPP

#include "nodejs-parser/http_parser.h"

#include <beast/http/method.hpp>
#include <beast/http/basic_parser.hpp>
#include <beast/http/rfc2616.hpp>
#include <beast/type_check.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <string>
#include <type_traits>

namespace beast {
namespace http {

namespace detail {

class nodejs_message_category
    : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "nodejs-http-error";
    }

    std::string
    message(int ev) const override
    {
        return http_errno_description(
            static_cast<http_errno>(ev));
    }

    boost::system::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return boost::system::error_condition{ev, *this};
    }

    bool
    equivalent(int ev,
        boost::system::error_condition const& condition
            ) const noexcept override
    {
        return condition.value() == ev &&
            &condition.category() == this;
    }

    bool
    equivalent(boost::system::error_code const& error,
        int ev) const noexcept override
    {
        return error.value() == ev &&
            &error.category() == this;
    }
};

template<class = void>
boost::system::error_code
make_nodejs_error(int http_errno)
{
    static nodejs_message_category const mc{};
    return boost::system::error_code{http_errno, mc};
}

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
class nodejs_basic_parser
{
    http_parser state_;
    boost::system::error_code* ec_;
    bool complete_ = false;
    std::string url_;
    std::string status_;
    std::string field_;
    std::string value_;

public:
    using error_code = boost::system::error_code;

    nodejs_basic_parser(nodejs_basic_parser&& other);

    nodejs_basic_parser&
    operator=(nodejs_basic_parser&& other);

    nodejs_basic_parser(nodejs_basic_parser const& other);

    nodejs_basic_parser& operator=(nodejs_basic_parser const& other);

    explicit
    nodejs_basic_parser(bool request) noexcept;

    bool
    complete() const noexcept
    {
        return complete_;
    }

    std::size_t
    write(void const* data, std::size_t size)
    {
        error_code ec;
        auto const used = write(data, size, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return used;
    }

    std::size_t
    write(void const* data, std::size_t size,
        error_code& ec);

    template<class ConstBufferSequence>
    std::size_t
    write(ConstBufferSequence const& buffers)
    {
        error_code ec;
        auto const used = write(buffers, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return used;
    }

    template<class ConstBufferSequence>
    std::size_t
    write(ConstBufferSequence const& buffers,
        error_code& ec);

    void
    write_eof()
    {
        error_code ec;
        write_eof(ec);
        if(ec)
            throw boost::system::system_error{ec};
    }

    void
    write_eof(error_code& ec);

private:
    Derived&
    impl()
    {
        return *static_cast<Derived*>(this);
    }

    template<class C>
    class has_on_start_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_start(), std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_start =
        std::integral_constant<bool, has_on_start_t<C>::value>;

    void
    call_on_start(std::true_type)
    {
        impl().on_start();
    }

    void
    call_on_start(std::false_type)
    {
    }

    template<class C>
    class has_on_field_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_field(
                std::declval<std::string const&>(),
                    std::declval<std::string const&>()),
                        std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_field =
        std::integral_constant<bool, has_on_field_t<C>::value>;

    void
    call_on_field(std::string const& field,
        std::string const& value, std::true_type)
    {
        impl().on_field(field, value);
    }

    void
    call_on_field(std::string const&, std::string const&,
        std::false_type)
    {
    }

    template<class C>
    class has_on_headers_complete_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_headers_complete(
                std::declval<error_code&>()), std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_headers_complete =
        std::integral_constant<bool, has_on_headers_complete_t<C>::value>;

    void
    call_on_headers_complete(error_code& ec, std::true_type)
    {
        impl().on_headers_complete(ec);
    }

    void
    call_on_headers_complete(error_code&, std::false_type)
    {
    }

    template<class C>
    class has_on_request_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_request(
                std::declval<method_t>(), std::declval<std::string>(),
                    std::declval<int>(), std::declval<int>(),
                        std::declval<bool>(), std::declval<bool>()),
                            std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_request =
        std::integral_constant<bool, has_on_request_t<C>::value>;

    void
    call_on_request(method_t method, std::string url,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        impl().on_request(
            method, url, major, minor, keep_alive, upgrade);
    }

    void
    call_on_request(method_t, std::string, int, int, bool, bool,
        std::false_type)
    {
    }

    template<class C>
    class has_on_response_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_response(
                std::declval<int>(), std::declval<std::string>,
                    std::declval<int>(), std::declval<int>(),
                        std::declval<bool>(), std::declval<bool>()),
                            std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
#if 0
        using type = decltype(check<C>(0));
#else
        // VFALCO Trait seems broken for http::parser
        using type = std::true_type;
#endif
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_response =
        std::integral_constant<bool, has_on_response_t<C>::value>;

    bool
    call_on_response(int status, std::string text,
        int major, int minor, bool keep_alive, bool upgrade,
            std::true_type)
    {
        return impl().on_response(
            status, text, major, minor, keep_alive, upgrade);
    }

    bool
    call_on_response(int, std::string, int, int, bool, bool,
        std::false_type)
    {
        // VFALCO Certainly incorrect
        return true;
    }

    template<class C>
    class has_on_body_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_body(
                std::declval<void const*>(), std::declval<std::size_t>(),
                    std::declval<error_code&>()), std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_body =
        std::integral_constant<bool, has_on_body_t<C>::value>;

    void
    call_on_body(void const* data, std::size_t bytes,
        error_code& ec, std::true_type)
    {
        impl().on_body(data, bytes, ec);
    }

    void
    call_on_body(void const*, std::size_t,
        error_code&, std::false_type)
    {
    }

    template<class C>
    class has_on_complete_t
    {
        template<class T, class R =
            decltype(std::declval<T>().on_complete(), std::true_type{})>
        static R check(int);
        template <class>
        static std::false_type check(...);
        using type = decltype(check<C>(0));
    public:
        static bool const value = type::value;
    };
    template<class C>
    using has_on_complete =
        std::integral_constant<bool, has_on_complete_t<C>::value>;

    void
    call_on_complete(std::true_type)
    {
        impl().on_complete();
    }

    void
    call_on_complete(std::false_type)
    {
    }

    void
    check_header();

    static int cb_message_start(http_parser*);
    static int cb_url(http_parser*, char const*, std::size_t);
    static int cb_status(http_parser*, char const*, std::size_t);
    static int cb_header_field(http_parser*, char const*, std::size_t);
    static int cb_header_value(http_parser*, char const*, std::size_t);
    static int cb_headers_complete(http_parser*);
    static int cb_body(http_parser*, char const*, std::size_t);
    static int cb_message_complete(http_parser*);
    static int cb_chunk_header(http_parser*);
    static int cb_chunk_complete(http_parser*);

    struct hooks_t : http_parser_settings
    {
        hooks_t()
        {
            http_parser_settings_init(this);
            on_message_begin    = &nodejs_basic_parser::cb_message_start;
            on_url              = &nodejs_basic_parser::cb_url;
            on_status           = &nodejs_basic_parser::cb_status;
            on_header_field     = &nodejs_basic_parser::cb_header_field;
            on_header_value     = &nodejs_basic_parser::cb_header_value;
            on_headers_complete = &nodejs_basic_parser::cb_headers_complete;
            on_body             = &nodejs_basic_parser::cb_body;
            on_message_complete = &nodejs_basic_parser::cb_message_complete;
            on_chunk_header     = &nodejs_basic_parser::cb_chunk_header;
            on_chunk_complete   = &nodejs_basic_parser::cb_chunk_complete;
        }
    };

    static
    http_parser_settings const*
    hooks();
};

template<class Derived>
template<class ConstBufferSequence>
std::size_t
nodejs_basic_parser<Derived>::write(
    ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(beast::is_ConstBufferSequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    std::size_t bytes_used = 0;
    for (auto const& buffer : buffers)
    {
        auto const n = write(
            buffer_cast<void const*>(buffer),
                buffer_size(buffer), ec);
        if(ec)
            return 0;
        bytes_used += n;
        if(complete())
            break;
    }
    return bytes_used;
}

template<class Derived>
http_parser_settings const*
nodejs_basic_parser<Derived>::hooks()
{
    static hooks_t const h;
    return &h;
}

template<class Derived>
nodejs_basic_parser<Derived>::
nodejs_basic_parser(nodejs_basic_parser&& other)
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
nodejs_basic_parser<Derived>::operator=(nodejs_basic_parser&& other) ->
    nodejs_basic_parser&
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
nodejs_basic_parser<Derived>::
nodejs_basic_parser(nodejs_basic_parser const& other)
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
nodejs_basic_parser<Derived>::
operator=(nodejs_basic_parser const& other) ->
    nodejs_basic_parser&
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
nodejs_basic_parser<Derived>::nodejs_basic_parser(bool request) noexcept
{
    state_.data = this;
    http_parser_init(&state_, request
        ? http_parser_type::HTTP_REQUEST
        : http_parser_type::HTTP_RESPONSE);
}

template<class Derived>
std::size_t
nodejs_basic_parser<Derived>::write(void const* data,
    std::size_t size, error_code& ec)
{
    ec_ = &ec;
    auto const n = http_parser_execute(
        &state_, hooks(),
            static_cast<const char*>(data), size);
    if(! ec)
        ec = detail::make_nodejs_error(
            static_cast<int>(state_.http_errno));
    if(ec)
        return 0;
    return n;
}

template<class Derived>
void
nodejs_basic_parser<Derived>::write_eof(error_code& ec)
{
    ec_ = &ec;
    http_parser_execute(
        &state_, hooks(), nullptr, 0);
    if(! ec)
        ec = detail::make_nodejs_error(
            static_cast<int>(state_.http_errno));
}

template<class Derived>
void
nodejs_basic_parser<Derived>::check_header()
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
nodejs_basic_parser<Derived>::cb_message_start(http_parser* p)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
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
nodejs_basic_parser<Derived>::cb_url(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
    t.url_.append(in, bytes);
    return 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_status(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
    t.status_.append(in, bytes);
    return 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_header_field(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
    t.check_header();
    t.field_.append(in, bytes);
    return 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_header_value(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
    t.value_.append(in, bytes);
    return 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_headers_complete(http_parser* p)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
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

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_body(http_parser* p,
    char const* in, std::size_t bytes)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
    t.call_on_body(in, bytes, *t.ec_, has_on_body<Derived>{});
    return *t.ec_ ? 1 : 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_message_complete(http_parser* p)
{
    auto& t = *reinterpret_cast<nodejs_basic_parser*>(p->data);
    t.complete_ = true;
    t.call_on_complete(has_on_complete<Derived>{});
    return 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_chunk_header(http_parser*)
{
    return 0;
}

template<class Derived>
int
nodejs_basic_parser<Derived>::cb_chunk_complete(http_parser*)
{
    return 0;
}

} // http
} // beast

#include <beast/http/error.hpp>
#include <beast/http/message.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

/** A HTTP parser.

    The parser may only be used once.
*/
template<bool isRequest, class Body, class Headers>
class nodejs_parser
    : public nodejs_basic_parser<nodejs_parser<isRequest, Body, Headers>>
{
    using message_type =
        message<isRequest, Body, Headers>;

    message_type m_;
    typename message_type::body_type::reader r_;
    bool started_ = false;

public:
    nodejs_parser(nodejs_parser&&) = default;

    nodejs_parser()
        : http::nodejs_basic_parser<nodejs_parser>(isRequest)
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
    friend class http::nodejs_basic_parser<nodejs_parser>;

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
