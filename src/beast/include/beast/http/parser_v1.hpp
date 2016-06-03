//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_PARSER_V1_HPP
#define BEAST_HTTP_PARSER_V1_HPP

#include <beast/http/basic_parser_v1.hpp>
#include <beast/http/concepts.hpp>
#include <beast/http/message_v1.hpp>
#include <beast/core/error.hpp>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

namespace detail {

struct parser_request
{
    std::string method_;
    std::string uri_;
};

struct parser_response
{
    std::string reason_;
};

} // detail

/** A parser for producing HTTP/1 messages.

    This class uses the basic HTTP/1 wire format parser to convert
    a series of octets into a `message_v1`.

    @note A new instance of the parser is required for each message.
*/
template<bool isRequest, class Body, class Headers>
class parser_v1
    : public basic_parser_v1<isRequest,
        parser_v1<isRequest, Body, Headers>>
    , private std::conditional<isRequest,
        detail::parser_request, detail::parser_response>::type
{
public:
    /// The type of message this parser produces.
    using message_type =
        message_v1<isRequest, Body, Headers>;

private:
    static_assert(is_ReadableBody<Body>::value,
        "ReadableBody requirements not met");

    std::string field_;
    std::string value_;
    message_type m_;
    typename message_type::body_type::reader r_;

public:
    parser_v1(parser_v1&&) = default;
    parser_v1(parser_v1 const&) = delete;
    parser_v1& operator=(parser_v1&&) = delete;
    parser_v1& operator=(parser_v1 const&) = delete;

    /** Construct the parser.

        @param args A list of arguments forwarded to the message constructor.
    */
    template<class... Args>
    explicit
    parser_v1(Args&&... args)
        : m_(std::forward<Args>(args)...)
        , r_(m_)
    {
    }

    /** Returns the parsed message.

        Only valid if `complete()` would return `true`.
    */
    message_type const&
    get() const
    {
        return m_;
    }

    /** Returns the parsed message.

        Only valid if `complete()` would return `true`.
    */
    message_type&
    get()
    {
        return m_;
    }

    /** Returns the parsed message.

        Ownership is transferred to the caller.
        Only valid if `complete()` would return `true`.

        Requires:
            `message<isRequest, Body, Headers>` is MoveConstructible
    */
    message_type
    release()
    {
        static_assert(std::is_move_constructible<decltype(m_)>::value,
            "MoveConstructible requirements not met");
        return std::move(m_);
    }

private:
    friend class basic_parser_v1<isRequest, parser_v1>;

    void flush()
    {
        if(! value_.empty())
        {
            m_.headers.insert(field_, value_);
            field_.clear();
            value_.clear();
        }
    }

    void on_start(error_code&)
    {
    }

    void on_method(boost::string_ref const& s, error_code&)
    {
        this->method_.append(s.data(), s.size());
    }

    void on_uri(boost::string_ref const& s, error_code&)
    {
        this->uri_.append(s.data(), s.size());
    }

    void on_reason(boost::string_ref const& s, error_code&)
    {
        this->reason_.append(s.data(), s.size());
    }

    void on_field(boost::string_ref const& s, error_code&)
    {
        flush();
        field_.append(s.data(), s.size());
    }

    void on_value(boost::string_ref const& s, error_code&)
    {
        value_.append(s.data(), s.size());
    }

    void set(std::true_type)
    {
        m_.method = std::move(this->method_);
        m_.url = std::move(this->uri_);
    }

    void set(std::false_type)
    {
        m_.status = this->status_code();
        m_.reason = std::move(this->reason_);
    }

    int on_headers(std::uint64_t, error_code&)
    {
        flush();
        m_.version = 10 * this->http_major() + this->http_minor();
        return 0;
    }

    void on_request(error_code& ec)
    {
        set(std::integral_constant<
            bool, isRequest>{});
    }

    void on_response(error_code& ec)
    {
        set(std::integral_constant<
            bool, isRequest>{});
    }

    void on_body(boost::string_ref const& s, error_code& ec)
    {
        r_.write(s.data(), s.size(), ec);
    }

    void on_complete(error_code&)
    {
    }
};

} // http
} // beast

#endif
