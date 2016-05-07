//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_PARSER_V1_HPP
#define BEAST_HTTP_PARSER_V1_HPP

#include <beast/http/basic_parser_v1.hpp>
#include <beast/http/message_v1.hpp>
#include <beast/core/error.hpp>
#include <boost/optional.hpp>
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
*/
template<bool isRequest, class Body, class Headers>
class parser_v1
    : public basic_parser_v1<isRequest,
        parser_v1<isRequest, Body, Headers>>
    , private std::conditional<isRequest,
        detail::parser_request, detail::parser_response>::type
{
    using message_type =
        message_v1<isRequest, Body, Headers>;

    std::string field_;
    std::string value_;
    message_type m_;
    typename message_type::body_type::reader r_;

public:
    parser_v1(parser_v1&&) = default;

    parser_v1()
        : r_(m_)
    {
    }

    message_type
    release()
    {
        return std::move(m_);
    }

private:
    friend class basic_parser_v1<isRequest, parser_v1>;

    void flush()
    {
        if(! value_.empty())
        {
            rfc2616::trim_right_in_place(value_);
            // VFALCO could std::move
            m_.headers.insert(field_, value_);
            field_.clear();
            value_.clear();
        }
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

    int on_headers(error_code&)
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
