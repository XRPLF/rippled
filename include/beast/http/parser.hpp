//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_PARSER_HPP
#define BEAST_HTTP_PARSER_HPP

#include <beast/http/basic_parser.hpp>
#include <beast/http/error.hpp>
#include <beast/http/message.hpp>
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

template<bool isRequest, class Body, class Headers>
class parser
    : public basic_parser<isRequest,
        parser<isRequest, Body, Headers>>
    , private std::conditional<isRequest,
        detail::parser_request, detail::parser_response>::type
{
    using message_type =
        message<isRequest, Body, Headers>;

    std::string field_;
    std::string value_;
    message_type m_;
    typename message_type::body_type::reader r_;

public:
    parser(parser&&) = default;

    parser()
        : r_(m_)
    {
    }

    message_type
    release()
    {
        return std::move(m_);
    }

private:
    friend class basic_parser<isRequest, parser>;

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
        // VFALCO This is terrible for setting method
        auto m =
            [&](char const* s, method_t m)
            {
                if(this->method_ == s)
                {
                    m_.method = m;
                    return true;
                }
                return false;
            };
        do
        {
            if(m("DELETE",     method_t::http_delete))
                break;
            if(m("GET",        method_t::http_get))
                break;
            if(m("HEAD",       method_t::http_head))
                break;
            if(m("POST",       method_t::http_post))
                break;
            if(m("PUT",        method_t::http_put))
                break;
            if(m("CONNECT",    method_t::http_connect))
                break;
            if(m("OPTIONS",    method_t::http_options))
                break;
            if(m("TRACE",      method_t::http_trace))
                break;
            if(m("COPY",       method_t::http_copy))
                break;
            if(m("LOCK",       method_t::http_lock))
                break;
            if(m("MKCOL",      method_t::http_mkcol))
                break;
            if(m("MOVE",       method_t::http_move))
                break;
            if(m("PROPFIND",   method_t::http_propfind))
                break;
            if(m("PROPPATCH",  method_t::http_proppatch))
                break;
            if(m("SEARCH",     method_t::http_search))
                break;
            if(m("UNLOCK",     method_t::http_unlock))
                break;
            if(m("BIND",       method_t::http_bind))
                break;
            if(m("REBID",      method_t::http_rebind))
                break;
            if(m("UNBIND",     method_t::http_unbind))
                break;
            if(m("ACL",        method_t::http_acl))
                break;
            if(m("REPORT",     method_t::http_report))
                break;
            if(m("MKACTIVITY", method_t::http_mkactivity))
                break;
            if(m("CHECKOUT",   method_t::http_checkout))
                break;
            if(m("MERGE",      method_t::http_merge))
                break;
            if(m("MSEARCH",    method_t::http_msearch))
                break;
            if(m("NOTIFY",     method_t::http_notify))
                break;
            if(m("SUBSCRIBE",  method_t::http_subscribe))
                break;
            if(m("UNSUBSCRIBE",method_t::http_unsubscribe))
                break;
            if(m("PATCH",      method_t::http_patch))
                break;
            if(m("PURGE",      method_t::http_purge))
                break;
            if(m("MKCALENDAR", method_t::http_mkcalendar))
                break;
            if(m("LINK",       method_t::http_link))
                break;
            if(m("UNLINK",     method_t::http_unlink))
                break;
        }
        while(false);

        m_.url = std::move(this->uri_);

    }

    void set(std::false_type)
    {
        m_.status = this->status_code();
        m_.reason = this->reason_;
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
