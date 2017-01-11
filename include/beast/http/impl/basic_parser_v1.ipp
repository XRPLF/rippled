//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_BASIC_PARSER_V1_IPP
#define BEAST_HTTP_IMPL_BASIC_PARSER_V1_IPP

#include <beast/http/detail/rfc7230.hpp>
#include <beast/core/buffer_concepts.hpp>
#include <boost/assert.hpp>

namespace beast {
namespace http {

/* Based on src/http/ngx_http_parse.c from NGINX copyright Igor Sysoev
 *
 * Additional changes are licensed under the same terms as NGINX and
 * copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
/* This code is a modified version of nodejs/http-parser, copyright above:
   https://github.com/nodejs/http-parser
*/

template<bool isRequest, class Derived>
basic_parser_v1<isRequest, Derived>::
basic_parser_v1()
{
    init();
}

template<bool isRequest, class Derived>
template<class OtherDerived>
basic_parser_v1<isRequest, Derived>::
basic_parser_v1(basic_parser_v1<
        isRequest, OtherDerived> const& other)
    : h_max_(other.h_max_)
    , h_left_(other.h_left_)
    , b_max_(other.b_max_)
    , b_left_(other.b_left_)
    , content_length_(other.content_length_)
    , cb_(nullptr)
    , s_(other.s_)
    , flags_(other.flags_)
    , fs_(other.fs_)
    , pos_(other.pos_)
    , http_major_(other.http_major_)
    , http_minor_(other.http_minor_)
    , status_code_(other.status_code_)
    , upgrade_(other.upgrade_)
{
    BOOST_ASSERT(! other.cb_);
}

template<bool isRequest, class Derived>
template<class OtherDerived>
auto
basic_parser_v1<isRequest, Derived>::
operator=(basic_parser_v1<
    isRequest, OtherDerived> const& other) ->
        basic_parser_v1&
{
    BOOST_ASSERT(! other.cb_);
    h_max_ = other.h_max_;
    h_left_ = other.h_left_;
    b_max_ = other.b_max_;
    b_left_ = other.b_left_;
    content_length_ = other.content_length_;
    cb_ = nullptr;
    s_ = other.s_;
    flags_ = other.flags_;
    fs_ = other.fs_;
    pos_ = other.pos_;
    http_major_ = other.http_major_;
    http_minor_ = other.http_minor_;
    status_code_ = other.status_code_;
    upgrade_ = other.upgrade_;
    return *this;
}

template<bool isRequest, class Derived>
bool
basic_parser_v1<isRequest, Derived>::
keep_alive() const
{
    if(http_major_ >= 1 && http_minor_ >= 1)
    {
        if(flags_ & parse_flag::connection_close)
            return false;
    }
    else
    {
        if(! (flags_ & parse_flag::connection_keep_alive))
            return false;
    }
    return ! needs_eof();
}

template<bool isRequest, class Derived>
template<class ConstBufferSequence>
typename std::enable_if<
    ! std::is_convertible<ConstBufferSequence,
        boost::asio::const_buffer>::value,
            std::size_t>::type
basic_parser_v1<isRequest, Derived>::
write(ConstBufferSequence const& buffers, error_code& ec)
{
    static_assert(is_ConstBufferSequence<ConstBufferSequence>::value,
        "ConstBufferSequence requirements not met");
    std::size_t used = 0;
    for(auto const& buffer : buffers)
    {
        used += write(buffer, ec);
        if(ec)
            break;
    }
    return used;
}

template<bool isRequest, class Derived>
std::size_t
basic_parser_v1<isRequest, Derived>::
write(boost::asio::const_buffer const& buffer, error_code& ec)
{
    using beast::http::detail::is_digit;
    using beast::http::detail::is_tchar;
    using beast::http::detail::is_text;
    using beast::http::detail::to_field_char;
    using beast::http::detail::to_value_char;
    using beast::http::detail::unhex;
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;

    auto const data = buffer_cast<void const*>(buffer);
    auto const size = buffer_size(buffer);

    if(size == 0 && s_ != s_dead)
        return 0;

    auto begin =
        reinterpret_cast<char const*>(data);
    auto const end = begin + size;
    auto p = begin;
    auto used = [&]
    {
        return p - reinterpret_cast<char const*>(data);
    };
    auto err = [&](parse_error ev)
    {
        ec = ev;
        s_ = s_dead;
        return used();
    };
    auto errc = [&]
    {
        s_ = s_dead;
        return used();
    };
    auto piece = [&]
    {
        return boost::string_ref{
            begin, static_cast<std::size_t>(p - begin)};
    };
    auto cb = [&](pmf_t next)
    {
        if(cb_ && p != begin)
        {
            (this->*cb_)(ec, piece());
            if(ec)
                return true; // error
        }
        cb_ = next;
        if(cb_)
            begin = p;
        return false;
    };
    for(;p != end; ++p)
    {
        unsigned char ch = *p;
    redo:
        switch(s_)
        {
        case s_dead:
        case s_closed_complete:
            return err(parse_error::connection_closed);
            break;

        case s_req_start:
            flags_ = 0;
            cb_ = nullptr;
            content_length_ = no_content_length;
            s_ = s_req_method0;
            goto redo;

        case s_req_method0:
            if(! is_tchar(ch))
                return err(parse_error::bad_method);
            call_on_start(ec);
            if(ec)
                return errc();
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_method);
            s_ = s_req_method;
            break;

        case s_req_method:
            if(ch == ' ')
            {
                if(cb(nullptr))
                    return errc();
                s_ = s_req_url0;
                break;
            }
            if(! is_tchar(ch))
                return err(parse_error::bad_method);
            break;

        case s_req_url0:
        {
            if(ch == ' ')
                return err(parse_error::bad_uri);
            // VFALCO TODO Better checking for valid URL characters
            if(! is_text(ch))
                return err(parse_error::bad_uri);
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_uri);
            s_ = s_req_url;
            break;
        }

        case s_req_url:
            if(ch == ' ')
            {
                if(cb(nullptr))
                    return errc();
                s_ = s_req_http;
                break;
            }
            // VFALCO TODO Better checking for valid URL characters
            if(! is_text(ch))
                return err(parse_error::bad_uri);
            break;

        case s_req_http:
            if(ch != 'H')
                return err(parse_error::bad_version);
            s_ = s_req_http_H;
            break;

        case s_req_http_H:
            if(ch != 'T')
                return err(parse_error::bad_version);
            s_ = s_req_http_HT;
            break;

        case s_req_http_HT:
            if(ch != 'T')
                return err(parse_error::bad_version);
            s_ = s_req_http_HTT;
            break;

        case s_req_http_HTT:
            if(ch != 'P')
                return err(parse_error::bad_version);
            s_ = s_req_http_HTTP;
            break;

        case s_req_http_HTTP:
            if(ch != '/')
                return err(parse_error::bad_version);
            s_ = s_req_major;
            break;

        case s_req_major:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_major_ = ch - '0';
            s_ = s_req_dot;
            break;

        case s_req_dot:
            if(ch != '.')
                return err(parse_error::bad_version);
            s_ = s_req_minor;
            break;

        case s_req_minor:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_minor_ = ch - '0';
            s_ = s_req_cr;
            break;

        case s_req_cr:
            if(ch != '\r')
                return err(parse_error::bad_version);
            s_ = s_req_lf;
            break;

        case s_req_lf:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            call_on_request(ec);
            if(ec)
                return errc();
            s_ = s_header_name0;
            break;

        //----------------------------------------------------------------------

        case s_res_start:
            flags_ = 0;
            cb_ = nullptr;
            content_length_ = no_content_length;
            if(ch != 'H')
                return err(parse_error::bad_version);
            call_on_start(ec);
            if(ec)
                return errc();
            s_ = s_res_H;
            break;

        case s_res_H:
            if(ch != 'T')
                return err(parse_error::bad_version);
            s_ = s_res_HT;
            break;

        case s_res_HT:
            if(ch != 'T')
                return err(parse_error::bad_version);
            s_ = s_res_HTT;
            break;

        case s_res_HTT:
            if(ch != 'P')
                return err(parse_error::bad_version);
            s_ = s_res_HTTP;
            break;

        case s_res_HTTP:
            if(ch != '/')
                return err(parse_error::bad_version);
            s_ = s_res_major;
            break;

        case s_res_major:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_major_ = ch - '0';
            s_ = s_res_dot;
            break;

        case s_res_dot:
            if(ch != '.')
                return err(parse_error::bad_version);
            s_ = s_res_minor;
            break;

        case s_res_minor:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_minor_ = ch - '0';
            s_ = s_res_space_1;
            break;

        case s_res_space_1:
            if(ch != ' ')
                return err(parse_error::bad_version);
            s_ = s_res_status0;
            break;

        case s_res_status0:
            if(! is_digit(ch))
                return err(parse_error::bad_status);
            status_code_ = ch - '0';
            s_ = s_res_status1;
            break;

        case s_res_status1:
            if(! is_digit(ch))
                return err(parse_error::bad_status);
            status_code_ = status_code_ * 10 + ch - '0';
            s_ = s_res_status2;
            break;

        case s_res_status2:
            if(! is_digit(ch))
                return err(parse_error::bad_status);
            status_code_ = status_code_ * 10 + ch - '0';
            s_ = s_res_space_2;
            break;

        case s_res_space_2:
            if(ch != ' ')
                return err(parse_error::bad_status);
            s_ = s_res_reason0;
            break;

        case s_res_reason0:
            if(ch == '\r')
            {
                s_ = s_res_line_lf;
                break;
            }
            if(! is_text(ch))
                return err(parse_error::bad_reason);
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_reason);
            s_ = s_res_reason;
            break;

        case s_res_reason:
            if(ch == '\r')
            {
                if(cb(nullptr))
                    return errc();
                s_ = s_res_line_lf;
                break;
            }
            if(! is_text(ch))
                return err(parse_error::bad_reason);
            break;

        case s_res_line_lf:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_res_line_done;
            break;

        case s_res_line_done:
            call_on_response(ec);
            if(ec)
                return errc();
            s_ = s_header_name0;
            goto redo;

        //----------------------------------------------------------------------

        case s_header_name0:
        {
            if(ch == '\r')
            {
                s_ = s_headers_almost_done;
                break;
            }
            auto c = to_field_char(ch);
            if(! c)
                return err(parse_error::bad_field);
            switch(c)
            {
            case 'c': pos_ = 0; fs_ = h_C; break;
            case 'p': pos_ = 0; fs_ = h_matching_proxy_connection; break;
            case 't': pos_ = 0; fs_ = h_matching_transfer_encoding; break;
            case 'u': pos_ = 0; fs_ = h_matching_upgrade; break;
            default:
                fs_ = h_general;
                break;
            }
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_field);
            s_ = s_header_name;
            break;
        }

        case s_header_name:
        {
            for(; p != end; ++p)
            {
                ch = *p;
                auto c = to_field_char(ch);
                if(! c)
                    break;
                switch(fs_)
                {
                default:
                case h_general:
                    break;
                case h_C:  ++pos_; fs_ = c=='o' ? h_CO : h_general; break;
                case h_CO: ++pos_; fs_ = c=='n' ? h_CON : h_general; break;
                case h_CON:
                    ++pos_;
                    switch(c)
                    {
                    case 'n': fs_ = h_matching_connection; break;
                    case 't': fs_ = h_matching_content_length; break;
                    default:
                        fs_ = h_general;
                    }
                    break;

                case h_matching_connection:
                    ++pos_;
                    if(c != detail::parser_str::connection[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::connection)-2)
                        fs_ = h_connection;
                    break;

                case h_matching_proxy_connection:
                    ++pos_;
                    if(c != detail::parser_str::proxy_connection[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::proxy_connection)-2)
                        fs_ = h_connection;
                    break;

                case h_matching_content_length:
                    ++pos_;
                    if(c != detail::parser_str::content_length[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::content_length)-2)
                    {
                        if(flags_ & parse_flag::contentlength)
                            return err(parse_error::bad_content_length);
                        fs_ = h_content_length0;
                    }
                    break;

                case h_matching_transfer_encoding:
                    ++pos_;
                    if(c != detail::parser_str::transfer_encoding[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::transfer_encoding)-2)
                        fs_ = h_transfer_encoding;
                    break;

                case h_matching_upgrade:
                    ++pos_;
                    if(c != detail::parser_str::upgrade[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::upgrade)-2)
                        fs_ = h_upgrade;
                    break;

                case h_connection:
                case h_content_length0:
                case h_transfer_encoding:
                case h_upgrade:
                    fs_ = h_general;
                    break;
                }
            }
            if(p == end)
            {
                --p;
                break;
            }
            if(ch == ':')
            {
                if(cb(nullptr))
                    return errc();
                s_ = s_header_value0;
                break;
            }
            return err(parse_error::bad_field);
        }
    /*
        header-field   = field-name ":" OWS field-value OWS
        field-name     = token
        field-value    = *( field-content / obs-fold )
        field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
        field-vchar    = VCHAR / obs-text
        obs-fold       = CRLF 1*( SP / HTAB ) 
                       ; obsolete line folding
    */
        case s_header_value0:
            if(ch == ' ' || ch == '\t')
                break;
            if(ch == '\r')
            {
                s_ = s_header_value0_lf;
                break;
            }
            if(fs_ == h_content_length0)
            {
                content_length_ = 0;
                flags_ |= parse_flag::contentlength;
            }
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_value);
            s_ = s_header_value;
            // fall through

        case s_header_value:
        {
            for(; p != end; ++p)
            {
                ch = *p;
                if(ch == '\r')
                {
                    if(cb(nullptr))
                        return errc();
                    s_ = s_header_value_lf;
                    break;
                }
                auto const c = to_value_char(ch);
                if(! c)
                    return err(parse_error::bad_value);
                switch(fs_)
                {
                case h_general:
                default:
                    break;

                case h_connection:
                    switch(c)
                    {
                    case 'k':
                        pos_ = 0;
                        fs_ = h_matching_connection_keep_alive;
                        break;
                    case 'c':
                        pos_ = 0;
                        fs_ = h_matching_connection_close;
                        break;
                    case 'u':
                        pos_ = 0;
                        fs_ = h_matching_connection_upgrade;
                        break;
                    default:
                        if(ch == ' ' || ch == '\t' || ch == ',')
                            break;
                        if(! is_tchar(ch))
                            return err(parse_error::bad_value);
                        fs_ = h_connection_token;
                        break;
                    }
                    break;

                case h_matching_connection_keep_alive:
                    ++pos_;
                    if(c != detail::parser_str::keep_alive[pos_])
                        fs_ = h_connection_token;
                    else if(pos_ == sizeof(detail::parser_str::keep_alive)- 2)
                        fs_ = h_connection_keep_alive;
                    break;

                case h_matching_connection_close:
                    ++pos_;
                    if(c != detail::parser_str::close[pos_])
                        fs_ = h_connection_token;
                    else if(pos_ == sizeof(detail::parser_str::close)-2)
                        fs_ = h_connection_close;
                    break;

                case h_matching_connection_upgrade:
                    ++pos_;
                    if(c != detail::parser_str::upgrade[pos_])
                        fs_ = h_connection_token;
                    else if(pos_ == sizeof(detail::parser_str::upgrade)-2)
                        fs_ = h_connection_upgrade;
                    break;

                case h_connection_close:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        flags_ |= parse_flag::connection_close;
                    }
                    else if(ch == ' ' || ch == '\t')
                        fs_ = h_connection_close_ows;
                    else if(is_tchar(ch))
                        fs_ = h_connection_token;
                    else
                        return err(parse_error::bad_value);
                    break;

                case h_connection_close_ows:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        flags_ |= parse_flag::connection_close;
                        break;
                    }
                    if(ch == ' ' || ch == '\t')
                        break;
                    return err(parse_error::bad_value);

                case h_connection_keep_alive:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        flags_ |= parse_flag::connection_keep_alive;
                    }
                    else if(ch == ' ' || ch == '\t')
                        fs_ = h_connection_keep_alive_ows;
                    else if(is_tchar(ch))
                        fs_ = h_connection_token;
                    else
                        return err(parse_error::bad_value);
                    break;

                case h_connection_keep_alive_ows:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        flags_ |= parse_flag::connection_keep_alive;
                        break;
                    }
                    if(ch == ' ' || ch == '\t')
                        break;
                    return err(parse_error::bad_value);

                case h_connection_upgrade:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        flags_ |= parse_flag::connection_upgrade;
                    }
                    else if(ch == ' ' || ch == '\t')
                        fs_ = h_connection_upgrade_ows;
                    else if(is_tchar(ch))
                        fs_ = h_connection_token;
                    else
                        return err(parse_error::bad_value);
                    break;

                case h_connection_upgrade_ows:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        flags_ |= parse_flag::connection_upgrade;
                        break;
                    }
                    if(ch == ' ' || ch == '\t')
                        break;
                    return err(parse_error::bad_value);

                case h_connection_token:
                    if(ch == ',')
                        fs_ = h_connection;
                    else if(ch == ' ' || ch == '\t')
                        fs_ = h_connection_token_ows;
                    else if(! is_tchar(ch))
                        return err(parse_error::bad_value);
                    break;

                case h_connection_token_ows:
                    if(ch == ',')
                    {
                        fs_ = h_connection;
                        break;
                    }
                    if(ch == ' ' || ch == '\t')
                        break;
                    return err(parse_error::bad_value);

                case h_content_length0:
                    if(! is_digit(ch))
                        return err(parse_error::bad_content_length);
                    content_length_ = ch - '0';
                    fs_ = h_content_length;
                    break;

                case h_content_length:
                    if(ch == ' ' || ch == '\t')
                    {
                        fs_ = h_content_length_ows;
                        break;
                    }
                    if(! is_digit(ch))
                        return err(parse_error::bad_content_length);
                    if(content_length_ > (no_content_length - 10) / 10)
                        return err(parse_error::bad_content_length);
                    content_length_ =
                        content_length_ * 10 + ch - '0';
                    break;

                case h_content_length_ows:
                    if(ch != ' ' && ch != '\t')
                        return err(parse_error::bad_content_length);
                    break;

                case h_transfer_encoding:
                    if(c == 'c')
                    {
                        pos_ = 0;
                        fs_ = h_matching_transfer_encoding_chunked;
                    }
                    else if(c != ' ' && c != '\t' && c != ',')
                    {
                        fs_ = h_matching_transfer_encoding_general;
                    }
                    break;

                case h_matching_transfer_encoding_chunked:
                    ++pos_;
                    if(c != detail::parser_str::chunked[pos_])
                        fs_ = h_matching_transfer_encoding_general;
                    else if(pos_ == sizeof(detail::parser_str::chunked)-2)
                        fs_ = h_transfer_encoding_chunked;
                    break;

                case h_matching_transfer_encoding_general:
                    if(c == ',')
                        fs_ = h_transfer_encoding;
                    break;

                case h_transfer_encoding_chunked:
                    if(c != ' ' && c != '\t' && c != ',')
                        fs_ = h_transfer_encoding;
                    break;

                case h_upgrade:
                    flags_ |= parse_flag::upgrade;
                    fs_ = h_general;
                    break;
                }
            }
            if(p == end)
                --p;
            break;
        }

        case s_header_value0_lf:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_header_value0_almost_done;
            break;

        case s_header_value0_almost_done:
            if(ch == ' ' || ch == '\t')
            {
                s_ = s_header_value0;
                break;
            }
            if(fs_ == h_content_length0)
                return err(parse_error::bad_content_length);
            if(fs_ == h_upgrade)
                flags_ |= parse_flag::upgrade;
            BOOST_ASSERT(! cb_);
            call_on_value(ec, boost::string_ref{"", 0});
            if(ec)
                return errc();
            s_ = s_header_name0;
            goto redo;

        case s_header_value_lf:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_header_value_almost_done;
            break;

        case s_header_value_almost_done:
            if(ch == ' ' || ch == '\t')
            {
                switch(fs_)
                {
                case h_matching_connection_keep_alive:
                case h_matching_connection_close:
                case h_matching_connection_upgrade:
                    fs_ = h_connection_token_ows;
                    break;

                case h_connection_close:
                    fs_ = h_connection_close_ows;
                    break;

                case h_connection_keep_alive:
                    fs_ = h_connection_keep_alive_ows;
                    break;
                
                case h_connection_upgrade:
                    fs_ = h_connection_upgrade_ows;
                    break;

                case h_content_length:
                    fs_ = h_content_length_ows;
                    break;

                case h_matching_transfer_encoding_chunked:
                    fs_ = h_matching_transfer_encoding_general;
                    break;

                default:
                    break;
                }
                call_on_value(ec, boost::string_ref(" ", 1));
                s_ = s_header_value_unfold;
                break;
            }
            switch(fs_)
            {
            case h_connection_keep_alive:
            case h_connection_keep_alive_ows:
                flags_ |= parse_flag::connection_keep_alive;
                break;
            case h_connection_close:
            case h_connection_close_ows:
                flags_ |= parse_flag::connection_close;
                break;

            case h_connection_upgrade:
            case h_connection_upgrade_ows:
                flags_ |= parse_flag::connection_upgrade;
                break;

            case h_transfer_encoding_chunked:
            case h_transfer_encoding_chunked_ows:
                flags_ |= parse_flag::chunked;
                break;

            default:
                break;
            }
            s_ = s_header_name0;
            goto redo;

        case s_header_value_unfold:
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_value);
            s_ = s_header_value;
            goto redo;

        case s_headers_almost_done:
        {
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            if(flags_ & parse_flag::trailing)
            {
                //if(cb(&self::call_on_chunk_complete)) return errc();
                s_ = s_complete;
                goto redo;
            }
            if((flags_ & parse_flag::chunked) && (flags_ & parse_flag::contentlength))
                return err(parse_error::illegal_content_length);
            upgrade_ = ((flags_ & (parse_flag::upgrade | parse_flag::connection_upgrade)) ==
                (parse_flag::upgrade | parse_flag::connection_upgrade)) /*|| method == "connect"*/;
            call_on_headers(ec);
            if(ec)
                return errc();
            auto const what = call_on_body_what(ec);
            if(ec)
                return errc();
            switch(what)
            {
            case body_what::normal:
                break;
            case body_what::upgrade:
                upgrade_ = true;
                // fall through
            case body_what::skip:
                flags_ |= parse_flag::skipbody;
                break;
            case body_what::pause:
                ++p;
                s_ = s_body_pause;
                return used();
            }
            s_ = s_headers_done;
            goto redo;
        }

        case s_body_pause:
        {
            auto const what = call_on_body_what(ec);
            if(ec)
                return errc();
            switch(what)
            {
            case body_what::normal:
                break;
            case body_what::upgrade:
                upgrade_ = true;
                // fall through
            case body_what::skip:
                flags_ |= parse_flag::skipbody;
                break;
            case body_what::pause:
                return used();
            }
            --p;
            s_ = s_headers_done;
            // fall through
        }

        case s_headers_done:
        {
            BOOST_ASSERT(! cb_);
            if(ec)
                return errc();
            bool const hasBody =
                (flags_ & parse_flag::chunked) || (content_length_ > 0 &&
                    content_length_ != no_content_length);
            if(upgrade_ && (/*method == "connect" ||*/ (flags_ & parse_flag::skipbody) || ! hasBody))
            {
                s_ = s_complete;
            }
            else if((flags_ & parse_flag::skipbody) || content_length_ == 0)
            {
                s_ = s_complete;
            }
            else if(flags_ & parse_flag::chunked)
            {
                s_ = s_chunk_size0;
                break;
            }
            else if(content_length_ != no_content_length)
            {
                s_ = s_body_identity0;
                break;
            }
            else if(! needs_eof())
            {
                s_ = s_complete;
            }
            else
            {
                s_ = s_body_identity_eof0;
                break;
            }
            goto redo;
        }

        case s_body_identity0:
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_body);
            s_ = s_body_identity;
            // fall through

        case s_body_identity:
        {
            std::size_t n;
            if(static_cast<std::size_t>((end - p)) < content_length_)
                n = end - p;
            else
                n = static_cast<std::size_t>(content_length_);
            BOOST_ASSERT(content_length_ != 0 && content_length_ != no_content_length);
            content_length_ -= n;
            if(content_length_ == 0)
            {
                p += n - 1;
                s_ = s_complete;
                goto redo; // ????
            }
            p += n - 1;
            break;
        }

        case s_body_identity_eof0:
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_body);
            s_ = s_body_identity_eof;
            // fall through

        case s_body_identity_eof:
            p = end - 1;
            break;

        case s_chunk_size0:
        {
            auto v = unhex(ch);
            if(v == -1)
                return err(parse_error::invalid_chunk_size);
            content_length_ = v;
            s_ = s_chunk_size;
            break;
        }

        case s_chunk_size:
        {
            if(ch == '\r')
            {
                s_ = s_chunk_size_lf;
                break;
            }
            if(ch == ';')
            {
                s_ = s_chunk_ext_name0;
                break;
            }
            auto v = unhex(ch);
            if(v == -1)
                return err(parse_error::invalid_chunk_size);
            if(content_length_ > (no_content_length - 16) / 16)
                return err(parse_error::bad_content_length);
            content_length_ =
                content_length_ * 16 + v;
            break;
        }

        case s_chunk_ext_name0:
            if(! is_tchar(ch))
                return err(parse_error::invalid_ext_name);
            s_ = s_chunk_ext_name;
            break;

        case s_chunk_ext_name:
            if(ch == '\r')
            {
                s_ = s_chunk_size_lf;
                break;
            }
            if(ch == '=')
            {
                s_ = s_chunk_ext_val;
                break;
            }
            if(ch == ';')
            {
                s_ = s_chunk_ext_name0;
                break;
            }
            if(! is_tchar(ch))
                return err(parse_error::invalid_ext_name);
            break;

        case s_chunk_ext_val:
            if(ch == '\r')
            {
                s_ = s_chunk_size_lf;
                break;
            }
            break;

        case s_chunk_size_lf:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            if(content_length_ == 0)
            {
                flags_ |= parse_flag::trailing;
                s_ = s_header_name0;
                break;
            }
            //call_chunk_header(ec); if(ec) return errc();
            s_ = s_chunk_data0;
            break;

        case s_chunk_data0:
            BOOST_ASSERT(! cb_);
            cb(&self::call_on_body);
            s_ = s_chunk_data;
            goto redo; // VFALCO fall through?

        case s_chunk_data:
        {
            std::size_t n;
            if(static_cast<std::size_t>((end - p)) < content_length_)
                n = end - p;
            else
                n = static_cast<std::size_t>(content_length_);
            content_length_ -= n;
            p += n - 1;
            if(content_length_ == 0)
                s_ = s_chunk_data_cr;
            break;
        }

        case s_chunk_data_cr:
            if(ch != '\r')
                return err(parse_error::bad_crlf);
            if(cb(nullptr))
                return errc();
            s_ = s_chunk_data_lf;
            break;

        case s_chunk_data_lf:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_chunk_size0;
            break;

        case s_complete:
            ++p;
            if(cb(nullptr))
                return errc();
            call_on_complete(ec);
            if(ec)
                return errc();
            s_ = s_restart;
            return used();

        case s_restart:
            if(keep_alive())
                reset();
            else
                s_ = s_dead;
            goto redo;
        }
    }
    if(cb_)
    {
        (this->*cb_)(ec, piece());
        if(ec)
            return errc();
    }
    return used();
}

template<bool isRequest, class Derived>
void
basic_parser_v1<isRequest, Derived>::
write_eof(error_code& ec)
{
    switch(s_)
    {
    case s_restart:
        s_ = s_closed_complete;
        break;

    case s_dead:
    case s_closed_complete:
        break;

    case s_body_identity_eof0:
    case s_body_identity_eof:
        cb_ = nullptr;
        call_on_complete(ec);
        if(ec)
        {
            s_ = s_dead;
            break;
        }
        s_ = s_closed_complete;
        break;

    default:
        s_ = s_dead;
        ec = parse_error::short_read;
        break;
    }
}

template<bool isRequest, class Derived>
void
basic_parser_v1<isRequest, Derived>::
reset()
{
    cb_ = nullptr;
    h_left_ = h_max_;
    b_left_ = b_max_;
    reset(std::integral_constant<bool, isRequest>{});
}

template<bool isRequest, class Derived>
bool
basic_parser_v1<isRequest, Derived>::
needs_eof(std::true_type) const
{
    return false;
}

template<bool isRequest, class Derived>
bool
basic_parser_v1<isRequest, Derived>::
needs_eof(std::false_type) const
{
    // See RFC 2616 section 4.4
    if( status_code_ / 100 == 1 ||      // 1xx e.g. Continue
        status_code_ == 204 ||          // No Content
        status_code_ == 304 ||          // Not Modified
        flags_ & parse_flag::skipbody)  // response to a HEAD request
        return false;

    if((flags_ & parse_flag::chunked) ||
        content_length_ != no_content_length)
        return false;

    return true;
}

} // http
} // beast

#endif
