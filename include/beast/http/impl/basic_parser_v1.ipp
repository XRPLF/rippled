//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_BASIC_PARSER_V1_IPP
#define BEAST_HTTP_IMPL_BASIC_PARSER_V1_IPP

namespace beast {
namespace http {

template<bool isRequest, class Derived>
bool
basic_parser_v1<isRequest, Derived>::
keep_alive() const
{
    if(http_major_ > 0 && http_minor_ > 0)
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

// Implementation inspired by nodejs/http-parser

template<bool isRequest, class Derived>
template<class ConstBufferSequence, class>
std::size_t
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
    using beast::http::detail::is_token;
    using beast::http::detail::is_text;
    using beast::http::detail::to_field_char;
    using beast::http::detail::to_value_char;
    using beast::http::detail::unhex;
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
 
    auto const data = buffer_cast<void const*>(buffer);
    auto const size = buffer_size(buffer);

    if(size == 0 && s_ != s_closed)
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
        s_ = s_closed;
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
        case s_closed:
            return err(parse_error::connection_closed);
            break;

        case s_req_start:
            flags_ = 0;
            cb_ = nullptr;
            content_length_ = no_content_length;
            s_ = s_req_method_start;
            goto redo;

        case s_req_method_start:
            if(! is_token(ch))
                return err(parse_error::bad_method);
            cb_ = &self::call_on_method;
            s_ = s_req_method;
            break;

        case s_req_method:
            if(! is_token(ch))
            {
                if(cb(nullptr))
                    return used();
                s_ = s_req_space_before_url;
                goto redo;
            }
            break;

        case s_req_space_before_url:
            if(ch != ' ')
                return err(parse_error::bad_request);
            s_ = s_req_url_start;
            break;

        case s_req_url_start:
            if(ch == ' ')
                return err(parse_error::bad_uri);
            // VFALCO TODO Require valid URL character
            if(cb(&self::call_on_uri))
                return used();
            s_ = s_req_url;
            break;

        case s_req_url:
            if(ch == ' ')
            {
                if(cb(nullptr))
                    return used();
                s_ = s_req_http_start;
                break;
            }
            // VFALCO TODO Require valid URL character
            break;

        case s_req_http_start:
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
            s_ = s_req_major_start;
            break;

        case s_req_major_start:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_major_ = ch - '0';
            s_ = s_req_major;
            break;

        case s_req_major:
            if(ch == '.')
            {
                s_ = s_req_minor_start;
                break;
            }
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_major_ = 10 * http_major_ + ch - '0';
            if(http_major_ > 999)
                return err(parse_error::bad_version);
            break;

        case s_req_minor_start:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_minor_ = ch - '0';
            s_ = s_req_minor;
            break;

        case s_req_minor:
            if(ch == '\r')
            {
                s_ = s_req_line_end;
                break;
            }
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_minor_ = 10 * http_minor_ + ch - '0';
            if(http_minor_ > 999)
                return err(parse_error::bad_version);
            break;

        case s_req_line_end:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            call_on_request(ec);
            if(ec)
                return used();
            s_ = s_header_field_start;
            break;

        //--------------------------------------------

        case s_res_start:
            flags_ = 0;
            cb_ = nullptr;
            content_length_ = no_content_length;
            switch(ch)
            {
            case 'H': s_ = s_res_H; break;
            case '\r':
            case '\n':
                break;
            default:
                return err(parse_error::bad_version);
            }
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
            s_ = s_res_major_start;
            break;

        case s_res_major_start:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_major_ = ch - '0';
            s_ = s_res_major;
            break;

        case s_res_major:
            if(ch == '.')
            {
                s_ = s_res_minor_start;
                break;
            }
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_major_ = 10 * http_major_ + ch - '0';
            if(http_major_ > 999)
                return err(parse_error::bad_version);
            break;

        case s_res_minor_start:
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_minor_ = ch - '0';
            s_ = s_res_minor;
            break;

        case s_res_minor:
            if(ch == ' ')
            {
                s_ = s_res_status_code_start;
                break;
            }
            if(! is_digit(ch))
                return err(parse_error::bad_version);
            http_minor_ = 10 * http_minor_ + ch - '0';
            if(http_minor_ > 999)
                return err(parse_error::bad_version);
            break;

        case s_res_status_code_start:
            if(! is_digit(ch))
            {
                if(ch == ' ')
                    break;
                return err(parse_error::bad_status_code);
            }
            status_code_ = ch - '0';
            s_ = s_res_status_code;
            break;

        case s_res_status_code:
            if(! is_digit(ch))
            {
                switch(ch)
                {
                case ' ': s_ = s_res_status_start; break;
                case '\r': s_ = s_res_line_almost_done; break;
                case '\n': s_ = s_header_field_start; break;
                default:
                    return err(parse_error::bad_status_code);
                }
                break;
            }
            status_code_ = status_code_ * 10 + ch - '0';
            if(status_code_ > 999)
                return err(parse_error::bad_status_code);
            break;

        case s_res_status_start:
            if(ch == '\r')
            {
                s_ = s_res_line_almost_done;
                break;
            }
            if(ch == '\n')
            {
                s_ = s_header_field_start;
                break;
            }
            if(cb(&self::call_on_reason))
                return used();
            pos_ = 0;
            s_ = s_res_status;
            break;

        case s_res_status:
            if(ch == '\r')
            {
                if(cb(nullptr))
                    return used();
                s_ = s_res_line_almost_done;
                break;
            }
            if(ch == '\n')
            {
                if(cb(nullptr))
                    return used();
                s_ = s_header_field_start;
                break;
            }
            break;

        case s_res_line_almost_done:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_res_line_done;
            break;

        case s_res_line_done:
            call_on_response(ec);
            if(ec)
                return used();
            s_ = s_header_field_start;
            goto redo;

        //--------------------------------------------

        // message-header = field-name ":" [ field-value ]
        // field-name     = token

        case s_header_field_start:
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
            if(cb(&self::call_on_field))
                return used();
            s_ = s_header_field;
            break;
        }

        case s_header_field:
        {
            for(; p != end; ch = *++p)
            {
                auto c = to_field_char(ch);
                    if(! c)
                        break;
                switch(fs_)
                {
                case h_general: break;
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
                    if(pos_ >= sizeof(detail::parser_str::connection)-1 ||
                            c != detail::parser_str::connection[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::connection)-2)
                        fs_ = h_connection;
                    break;

                case h_matching_proxy_connection:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::proxy_connection)-1 ||
                            c != detail::parser_str::proxy_connection[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::proxy_connection)-2)
                        fs_ = h_connection;
                    break;

                case h_matching_content_length:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::content_length)-1 ||
                            c != detail::parser_str::content_length[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::content_length)-2)
                    {
                        if(flags_ & parse_flag::contentlength)
                            return err(parse_error::bad_content_length);
                        fs_ = h_content_length;
                        flags_ |= parse_flag::contentlength;
                    }
                    break;

                case h_matching_transfer_encoding:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::transfer_encoding)-1 ||
                            c != detail::parser_str::transfer_encoding[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::transfer_encoding)-2)
                        fs_ = h_transfer_encoding;
                    break;

                case h_matching_upgrade:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::upgrade)-1 ||
                            c != detail::parser_str::upgrade[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::upgrade)-2)
                        fs_ = h_upgrade;
                    break;

                case h_connection:
                case h_content_length:
                case h_transfer_encoding:
                case h_upgrade:
                    // VFALCO Do we allow a space here?
                    fs_ = h_general;
                    break;
                default:
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
                    return used();
                s_ = s_header_value_start;
                break;
            }
            return err(parse_error::bad_field);
        }

        // field-value   = *( field-content | LWS )
        // field-content = *TEXT
        // LWS           = [CRLF] 1*( SP | HT )

        case s_header_value_start:
            if(ch == '\r')
            {
                s_ = s_header_value_discard_lWs0;
                break;
            }
            if(ch == ' ' || ch == '\t')
            {
                s_ = s_header_value_discard_ws0;
                break;
            }
            s_ = s_header_value_text_start;
            goto redo;

        case s_header_value_discard_ws0:
            if(ch == ' ' || ch == '\t')
                break;
            if(ch == '\r')
            {
                s_ = s_header_value_discard_lWs0;
                break;
            }
            s_ = s_header_value_text_start;
            goto redo;

        case s_header_value_discard_lWs0:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_header_value_almost_done0;
            break;

        case s_header_value_almost_done0:
            if(ch == ' ' || ch == '\t')
            {
                s_ = s_header_value_discard_ws0;
                break;
            }
            call_on_value(ec, boost::string_ref{"", 0});
            if(ec)
                return used();
            s_ = s_header_field_start;
            goto redo;

        case s_header_value_text_start:
        {
            auto const c = to_value_char(ch);
            if(! c)
                return err(parse_error::bad_value);
            switch(fs_)
            {
            case h_upgrade:
                flags_ |= parse_flag::upgrade;
                fs_ = h_general;
                break;

            case h_transfer_encoding:
                if(c == 'c')
                    fs_ = h_matching_transfer_encoding_chunked;
                else
                    fs_ = h_general;
                break;

            case h_content_length:
                if(! is_digit(ch))
                    return err(parse_error::bad_content_length);
                content_length_ = ch - '0';
                break;

            case h_connection:
                switch(c)
                {
                case 'k': fs_ = h_matching_connection_keep_alive; break;
                case 'c': fs_ = h_matching_connection_close; break;
                case 'u': fs_ = h_matching_connection_upgrade; break;
                default:
                    fs_ = h_matching_connection_token;
                    break;
                }
                break;

            case h_matching_connection_token_start:
                break;

            default:
                fs_ = h_general;
                break;
            }
            pos_ = 0;
            if(cb(&self::call_on_value))
                return used();
            s_ = s_header_value_text;
            break;
        }

        case s_header_value_text:
        {
            for(; p != end; ch = *++p)
            {
                if(ch == '\r')
                {
                    if(cb(nullptr))
                        return used();
                    s_ = s_header_value_discard_lWs;
                    break;
                }
                auto const c = to_value_char(ch);
                if(! c)
                    return err(parse_error::bad_value);
                switch(fs_)
                {
                case h_general:
                    break;

                case h_connection:
                case h_transfer_encoding:
                    assert(0);
                    break;

                case h_content_length:
                    if(ch == ' ' || ch == '\t')
                        break;
                    if(! is_digit(ch))
                        return err(parse_error::bad_content_length);
                    if(content_length_ > (no_content_length - 10) / 10)
                        return err(parse_error::bad_content_length);
                    content_length_ =
                        content_length_ * 10 + ch - '0';
                    break;

                case h_matching_transfer_encoding_chunked:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::chunked)-1 ||
                            c != detail::parser_str::chunked[pos_])
                        fs_ = h_general;
                    else if(pos_ == sizeof(detail::parser_str::chunked)-2)
                        fs_ = h_transfer_encoding_chunked;
                    break;

                case h_matching_connection_token_start:
                    switch(c)
                    {
                    case 'k': fs_ = h_matching_connection_keep_alive; break;
                    case 'c': fs_ = h_matching_connection_close; break;
                    case 'u': fs_ = h_matching_connection_upgrade; break;
                    default:
                        if(is_token(c))
                            fs_ = h_matching_connection_token;
                        else if(ch == ' ' || ch == '\t')
                            { }
                        else
                            fs_ = h_general;
                        break;
                    }
                    break;

                case h_matching_connection_keep_alive:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::keep_alive)-1 ||
                            c != detail::parser_str::keep_alive[pos_])
                        fs_ = h_matching_connection_token;
                    else if (pos_ == sizeof(detail::parser_str::keep_alive)- 2)
                        fs_ = h_connection_keep_alive;
                    break;

                case h_matching_connection_close:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::close)-1 ||
                            c != detail::parser_str::close[pos_])
                        fs_ = h_matching_connection_token;
                    else if(pos_ == sizeof(detail::parser_str::close)-2)
                        fs_ = h_connection_close;
                    break;

                case h_matching_connection_upgrade:
                    ++pos_;
                    if(pos_ >= sizeof(detail::parser_str::upgrade)-1 ||
                            c != detail::parser_str::upgrade[pos_])
                        fs_ = h_matching_connection_token;
                    else if (pos_ == sizeof(detail::parser_str::upgrade)-2)
                        fs_ = h_connection_upgrade;
                    break;

                case h_matching_connection_token:
                    if(ch == ',')
                    {
                        fs_ = h_matching_connection_token_start;
                        pos_ = 0;
                    }
                    break;

                case h_transfer_encoding_chunked:
                    if(ch != ' ' && ch != '\t')
                        fs_ = h_general;
                    break;

                case h_connection_keep_alive:
                case h_connection_close:
                case h_connection_upgrade:
                    if(ch ==',')
                    {
                        if(fs_ == h_connection_keep_alive)
                            flags_ |= parse_flag::connection_keep_alive;
                        else if(fs_ == h_connection_close)
                            flags_ |= parse_flag::connection_close;
                        else if(fs_ == h_connection_upgrade)
                            flags_ |= parse_flag::connection_upgrade;
                        fs_ = h_matching_connection_token_start;
                        pos_ = 0;
                    }
                    else if(ch != ' ' && ch != '\t')
                    {
                        fs_ = h_matching_connection_token;
                    }
                    break;
                default:
                    break;
                }
            }
            if(p == end)
                --p;
            break;
        }

        case s_header_value_discard_ws:
            if(ch == ' ' || ch == '\t')
                break;
            if(ch == '\r')
            {
                s_ = s_header_value_discard_lWs;
                break;
            }
            if(! is_text(ch))
                return err(parse_error::bad_value);
            call_on_value(ec, boost::string_ref(" ", 1));
            if(ec)
                return used();
            if(cb(&self::call_on_value))
                return used();
            s_ = s_header_value_text;
            break;

        case s_header_value_discard_lWs:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            s_ = s_header_value_almost_done;
            break;

        case s_header_value_almost_done:
            if(ch == ' ' || ch == '\t')
            {
                s_ = s_header_value_discard_ws;
                break;
            }
            switch(fs_)
            {
            case h_connection_keep_alive: flags_ |= parse_flag::connection_keep_alive; break;
            case h_connection_close: flags_ |= parse_flag::connection_close; break;
            case h_transfer_encoding_chunked: flags_ |= parse_flag::chunked; break;
            case h_connection_upgrade: flags_ |= parse_flag::connection_upgrade; break;
            default:
                break;
            }
            s_ = s_header_field_start;
            goto redo;

        case s_headers_almost_done:
        {
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            if(flags_ & parse_flag::trailing)
            {
                //if(cb(&self::call_on_chunk_complete)) return used();
                s_ = s_complete;
                goto redo;
            }
            if((flags_ & parse_flag::chunked) && (flags_ & parse_flag::contentlength))
                return err(parse_error::illegal_content_length);
            upgrade_ = ((flags_ & (parse_flag::upgrade | parse_flag::connection_upgrade)) ==
                (parse_flag::upgrade | parse_flag::connection_upgrade)) /*|| method == "connect"*/;
            auto const maybe_skip = call_on_headers(ec);
            if(ec)
                return used();
            switch(maybe_skip)
            {
            case 0: break;
            case 2: upgrade_ = true; // fall through
            case 1: flags_ |= parse_flag::skipbody; break;
            default:
                return err(parse_error::bad_on_headers_rv);
            }
            s_ = s_headers_done;
            goto redo;
        }

        case s_headers_done:
        {
            assert(! cb_);
            call_on_headers(ec);
            if(ec)
                return used();
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
                s_ = s_chunk_size_start;
                break;
            }
            else if(content_length_ == 0)
            {
                s_ = s_complete;
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
            if(cb(&self::call_on_body))
                return used();
            s_ = s_body_identity;
            goto redo; // VFALCO fall through?

        case s_body_identity:
        {
            std::size_t n;
            if(static_cast<std::size_t>((end - p)) < content_length_)
                n = end - p;
            else
                n = static_cast<std::size_t>(content_length_);
            assert(content_length_ != 0 && content_length_ != no_content_length);
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
            if(cb(&self::call_on_body))
                return used();
            s_ = s_body_identity_eof;
            goto redo; // VFALCO fall through?

        case s_body_identity_eof:
            p = end - 1;
            break;

        case s_chunk_size_start:
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
                s_ = s_chunk_size_almost_done;
                break;
            }
            auto v = unhex(ch);
            if(v == -1)
            {
                if(ch == ';' || ch == ' ')
                {
                    s_ = s_chunk_parameters;
                    break;
                }
                return err(parse_error::invalid_chunk_size);
            }
            if(content_length_ > (no_content_length - 16) / 16)
                return err(parse_error::bad_content_length);
            content_length_ =
                content_length_ * 16 + v;
            break;
        }

        case s_chunk_parameters:
            if(ch == '\r')
            {
                s_ = s_chunk_size_almost_done;
                break;
            }
            break;

        case s_chunk_size_almost_done:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            nread_ = 0;
            if(content_length_ == 0)
            {
                flags_ |= parse_flag::trailing;
                s_ = s_header_field_start;
                break;
            }
            //call_chunk_header(ec); if(ec) return used();
            s_ = s_chunk_data_start;
            break;

        case s_chunk_data_start:
            if(cb(&self::call_on_body))
                return used();
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
                s_ = s_chunk_data_almost_done;
            break;
        }

        case s_chunk_data_almost_done:
            if(ch != '\r')
                return err(parse_error::bad_crlf);
            if(cb(nullptr))
                return used();
            s_ = s_chunk_data_done;
            break;

        case s_chunk_data_done:
            if(ch != '\n')
                return err(parse_error::bad_crlf);
            nread_ = 0;
            s_ = s_chunk_size_start;
            break;

        case s_complete:
            ++p;
            if(cb(nullptr))
                return used();
            call_on_complete(ec);
            if(ec)
                return used();
            s_ = s_restart;
            return used();

        case s_restart:
            if(keep_alive())
                init(std::integral_constant<bool, isRequest>{});
            else
                s_ = s_closed;
            goto redo;
        }
    }
    if(cb_)
    {
        (this->*cb_)(ec, piece());
        if(ec)
            return used();
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
    case s_body_identity_eof:
        cb_ = nullptr;
        call_on_complete(ec);
        if(ec)
            return;
        return;
    default:
        break;
    }
    ec = parse_error::short_read;
    s_ = s_closed;
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
    if (status_code_ / 100 == 1 || // 1xx e.g. Continue
        status_code_ == 204 ||     // No Content
        status_code_ == 304 ||     // Not Modified
        flags_ & parse_flag::skipbody)       // response to a HEAD request
    {
        return false;
    }

    if((flags_ & parse_flag::chunked) || content_length_ != no_content_length)
    {
        return false;
    }

    return true;
}

} // http
} // beast

#endif
