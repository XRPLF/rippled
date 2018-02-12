//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_BASIC_PARSER_IPP
#define BEAST_HTTP_IMPL_BASIC_PARSER_IPP

#include <beast/core/static_string.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/clamp.hpp>
#include <beast/core/detail/config.hpp>
#include <beast/http/error.hpp>
#include <beast/http/rfc7230.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/make_unique.hpp>
#include <algorithm>
#include <utility>

namespace beast {
namespace http {

template<bool isRequest, class Derived>
basic_parser<isRequest, Derived>::
basic_parser()
    : body_limit_(
        default_body_limit(is_request{}))
{
}

template<bool isRequest, class Derived>
template<class OtherDerived>
basic_parser<isRequest, Derived>::
basic_parser(basic_parser<
        isRequest, OtherDerived>&& other)
    : body_limit_(other.body_limit_)
    , len_(other.len_)
    , buf_(std::move(other.buf_))
    , buf_len_(other.buf_len_)
    , skip_(other.skip_)
    , state_(other.state_)
    , f_(other.f_)
{
}

template<bool isRequest, class Derived>
bool
basic_parser<isRequest, Derived>::
is_keep_alive() const
{
    BOOST_ASSERT(is_header_done());
    if(f_ & flagHTTP11)
    {
        if(f_ & flagConnectionClose)
            return false;
    }
    else
    {
        if(! (f_ & flagConnectionKeepAlive))
            return false;
    }
    return (f_ & flagNeedEOF) == 0;
}

template<bool isRequest, class Derived>
boost::optional<std::uint64_t>
basic_parser<isRequest, Derived>::
content_length() const
{
    BOOST_ASSERT(is_header_done());
    if(! (f_ & flagContentLength))
        return boost::none;
    return len_;
}

template<bool isRequest, class Derived>
void
basic_parser<isRequest, Derived>::
skip(bool v)
{
    BOOST_ASSERT(! got_some());
    if(v)
        f_ |= flagSkipBody;
    else
        f_ &= ~flagSkipBody;
}

template<bool isRequest, class Derived>
template<class ConstBufferSequence>
std::size_t
basic_parser<isRequest, Derived>::
put(ConstBufferSequence const& buffers,
    error_code& ec)
{
    static_assert(is_const_buffer_sequence<
        ConstBufferSequence>::value,
            "ConstBufferSequence requirements not met");
    using boost::asio::buffer_cast;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    auto const p = buffers.begin();
    auto const last = buffers.end();
    if(p == last)
    {
        ec.assign(0, ec.category());
        return 0;
    }
    if(std::next(p) == last)
    {
        // single buffer
        auto const b = *p;
        return put(boost::asio::const_buffers_1{
            buffer_cast<char const*>(b),
            buffer_size(b)}, ec);
    }
    auto const size = buffer_size(buffers);
    if(size <= max_stack_buffer)
        return put_from_stack(size, buffers, ec);
    if(size > buf_len_)
    {
        // reallocate
        buf_ = boost::make_unique_noinit<char[]>(size);
        buf_len_ = size;
    }
    // flatten
    buffer_copy(boost::asio::buffer(
        buf_.get(), buf_len_), buffers);
    return put(boost::asio::const_buffers_1{
        buf_.get(), buf_len_}, ec);
}

template<bool isRequest, class Derived>
std::size_t
basic_parser<isRequest, Derived>::
put(boost::asio::const_buffers_1 const& buffer,
    error_code& ec)
{
    BOOST_ASSERT(state_ != state::complete);
    using boost::asio::buffer_size;
    auto p = boost::asio::buffer_cast<
        char const*>(*buffer.begin());
    auto n = buffer_size(*buffer.begin());
    auto const p0 = p;
    auto const p1 = p0 + n;
    ec.assign(0, ec.category());
loop:
    switch(state_)
    {
    case state::nothing_yet:
        if(n == 0)
        {
            ec = error::need_more;
            return 0;
        }
        state_ = state::start_line;
        BEAST_FALLTHROUGH;

    case state::start_line:
    {
        maybe_need_more(p, n, ec);
        if(ec)
            goto done;
        parse_start_line(p, p + std::min<std::size_t>(
            header_limit_, n), ec, is_request{});
        if(ec)
        {
            if(ec == error::need_more)
            {
                if(n >= header_limit_)
                {
                    ec = error::header_limit;
                    goto done;
                }
                if(p + 3 <= p1)
                    skip_ = static_cast<
                        std::size_t>(p1 - p - 3);
            }
            goto done;
        }
        BOOST_ASSERT(! is_done());
        n = static_cast<std::size_t>(p1 - p);
        if(p >= p1)
        {
            ec = error::need_more;
            goto done;
        }
        BEAST_FALLTHROUGH;
    }

    case state::fields:
        maybe_need_more(p, n, ec);
        if(ec)
            goto done;
        parse_fields(p, p + std::min<std::size_t>(
            header_limit_, n), ec);
        if(ec)
        {
            if(ec == error::need_more)
            {
                if(n >= header_limit_)
                {
                    ec = error::header_limit;
                    goto done;
                }
                if(p + 3 <= p1)
                    skip_ = static_cast<
                        std::size_t>(p1 - p - 3);
            }
            goto done;
        }
        finish_header(ec, is_request{});
        break;

    case state::body0:
        BOOST_ASSERT(! skip_);
        impl().on_body(content_length(), ec);
        if(ec)
            goto done;
        state_ = state::body;
        BEAST_FALLTHROUGH;

    case state::body:
        BOOST_ASSERT(! skip_);
        parse_body(p, n, ec);
        if(ec)
            goto done;
        break;

    case state::body_to_eof0:
        BOOST_ASSERT(! skip_);
        impl().on_body(content_length(), ec);
        if(ec)
            goto done;
        state_ = state::body_to_eof;
        BEAST_FALLTHROUGH;

    case state::body_to_eof:
        BOOST_ASSERT(! skip_);
        parse_body_to_eof(p, n, ec);
        if(ec)
            goto done;
        break;

    case state::chunk_header0:
        impl().on_body(content_length(), ec);
        if(ec)
            goto done;
        state_ = state::chunk_header;
        BEAST_FALLTHROUGH;

    case state::chunk_header:
        parse_chunk_header(p, n, ec);
        if(ec)
            goto done;
        break;

    case state::chunk_body:
        parse_chunk_body(p, n, ec);
        if(ec)
            goto done;
        break;

    case state::complete:
        ec.assign(0, ec.category());
        goto done;
    }
    if(p < p1 && ! is_done() && eager())
    {
        n = static_cast<std::size_t>(p1 - p);
        goto loop;
    }
done:
    return static_cast<std::size_t>(p - p0);
}

template<bool isRequest, class Derived>
void
basic_parser<isRequest, Derived>::
put_eof(error_code& ec)
{
    BOOST_ASSERT(got_some());
    if( state_ == state::start_line ||
        state_ == state::fields)
    {
        ec = error::partial_message;
        return;
    }
    if(f_ & (flagContentLength | flagChunked))
    {
        if(state_ != state::complete)
        {
            ec = error::partial_message;
            return;
        }
        ec.assign(0, ec.category());
        return;
    }
    impl().on_complete(ec);
    if(ec)
        return;
    state_ = state::complete;
}

template<bool isRequest, class Derived>
template<class ConstBufferSequence>
std::size_t
basic_parser<isRequest, Derived>::
put_from_stack(std::size_t size,
    ConstBufferSequence const& buffers,
        error_code& ec)
{
    char buf[max_stack_buffer];
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    buffer_copy(buffer(buf, sizeof(buf)), buffers);
    return put(boost::asio::const_buffers_1{
        buf, size}, ec);
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
maybe_need_more(
    char const* p, std::size_t n,
        error_code& ec)
{
    if(skip_ == 0)
        return;
    if( n > header_limit_)
        n = header_limit_;
    if(n < skip_ + 4)
    {
        ec = error::need_more;
        return;
    }
    auto const term =
        find_eom(p + skip_, p + n);
    if(! term)
    {
        skip_ = n - 3;
        if(skip_ + 4 > header_limit_)
        {
            ec = error::header_limit;
            return;
        }
        ec = error::need_more;
        return;
    }
    skip_ = 0;
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
parse_start_line(
    char const*& in, char const* last,
    error_code& ec, std::true_type)
{
/*
    request-line   = method SP request-target SP HTTP-version CRLF
    method         = token
*/
    auto p = in;

    string_view method;
    parse_method(p, last, method, ec);
    if(ec)
        return;

    string_view target;
    parse_target(p, last, target, ec);
    if(ec)
        return;

    int version = 0;
    parse_version(p, last, version, ec);
    if(ec)
        return;
    if(version < 10 || version > 11)
    {
        ec = error::bad_version;
        return;
    }

    if(p + 2 > last)
    {
        ec = error::need_more;
        return;
    }
    if(p[0] != '\r' || p[1] != '\n')
    {
        ec = error::bad_version;
        return;
    }
    p += 2;

    if(version >= 11)
        f_ |= flagHTTP11;

    impl().on_request(string_to_verb(method),
        method, target, version, ec);
    if(ec)
        return;

    in = p;
    state_ = state::fields;
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
parse_start_line(
    char const*& in, char const* last,
    error_code& ec, std::false_type)
{
/*
     status-line    = HTTP-version SP status-code SP reason-phrase CRLF
     status-code    = 3*DIGIT
     reason-phrase  = *( HTAB / SP / VCHAR / obs-text )
*/
    auto p = in;

    int version = 0;
    parse_version(p, last, version, ec);
    if(ec)
        return;
    if(version < 10 || version > 11)
    {
        ec = error::bad_version;
        return;
    }

    // SP
    if(p + 1 > last)
    {
        ec = error::need_more;
        return;
    }
    if(*p++ != ' ')
    {
        ec = error::bad_version;
        return;
    }

    parse_status(p, last, status_, ec);
    if(ec)
        return;

    // parse reason CRLF
    string_view reason;
    parse_reason(p, last, reason, ec);
    if(ec)
        return;

    if(version >= 11)
        f_ |= flagHTTP11;

    impl().on_response(
        status_, reason, version, ec);
    if(ec)
        return;

    in = p;
    state_ = state::fields;
}

template<bool isRequest, class Derived>
void
basic_parser<isRequest, Derived>::
parse_fields(char const*& in,
    char const* last, error_code& ec)
{
    string_view name;
    string_view value;
    // https://stackoverflow.com/questions/686217/maximum-on-http-header-values
    static_string<max_obs_fold> buf;
    auto p = in;
    for(;;)
    {
        if(p + 2 > last)
        {
            ec = error::need_more;
            return;
        }
        if(p[0] == '\r')
        {
            if(p[1] != '\n')
                ec = error::bad_line_ending;
            in = p + 2;
            return;
        }
        parse_field(p, last, name, value, buf, ec);
        if(ec)
            return;
        auto const f = string_to_field(name);
        do_field(f, value, ec);
        if(ec)
            return;
        impl().on_field(f, name, value, ec);
        if(ec)
            return;
        in = p;
    }
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
finish_header(error_code& ec, std::true_type)
{
    // RFC 7230 section 3.3
    // https://tools.ietf.org/html/rfc7230#section-3.3

    if(f_ & flagSkipBody)
    {
        state_ = state::complete;
    }
    else if(f_ & flagContentLength)
    {
        if(len_ > 0)
        {
            f_ |= flagHasBody;
            state_ = state::body0;
        }
        else
        {
            state_ = state::complete;
        }
    }
    else if(f_ & flagChunked)
    {
        f_ |= flagHasBody;
        state_ = state::chunk_header0;
    }
    else
    {
        len_ = 0;
        state_ = state::complete;
    }

    impl().on_header(ec);
    if(ec)
        return;
    if(state_ == state::complete)
    {
        impl().on_complete(ec);
        if(ec)
            return;
    }
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
finish_header(error_code& ec, std::false_type)
{
    // RFC 7230 section 3.3
    // https://tools.ietf.org/html/rfc7230#section-3.3

    if( (f_ & flagSkipBody) ||  // e.g. response to a HEAD request
        status_  / 100 == 1 ||   // 1xx e.g. Continue
        status_ == 204 ||        // No Content
        status_ == 304)          // Not Modified
    {
        state_ = state::complete;
        return;
    }

    if(f_ & flagContentLength)
    {
        if(len_ > 0)
        {
            f_ |= flagHasBody;
            state_ = state::body0;
        }
        else
        {
            state_ = state::complete;
        }
    }
    else if(f_ & flagChunked)
    {
        f_ |= flagHasBody;
        state_ = state::chunk_header0;
    }
    else
    {
        f_ |= flagHasBody;
        f_ |= flagNeedEOF;
        state_ = state::body_to_eof0;
    }

    impl().on_header(ec);
    if(ec)
        return;
    if(state_ == state::complete)
    {
        impl().on_complete(ec);
        if(ec)
            return;
    }
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
parse_body(char const*& p,
    std::size_t n, error_code& ec)
{
    n = impl().on_data(string_view{p,
        beast::detail::clamp(len_, n)}, ec);
    p += n;
    len_ -= n;
    if(ec)
        return;
    if(len_ > 0)
        return;
    impl().on_complete(ec);
    if(ec)
        return;
    state_ = state::complete;
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
parse_body_to_eof(char const*& p,
    std::size_t n, error_code& ec)
{
    if(n > body_limit_)
    {
        ec = error::body_limit;
        return;
    }
    body_limit_ = body_limit_ - n;
    n = impl().on_data(string_view{p, n}, ec);
    p += n;
    if(ec)
        return;
}

template<bool isRequest, class Derived>
void
basic_parser<isRequest, Derived>::
parse_chunk_header(char const*& p0,
    std::size_t n, error_code& ec)
{
/*
    chunked-body   = *chunk last-chunk trailer-part CRLF

    chunk          = chunk-size [ chunk-ext ] CRLF chunk-data CRLF
    last-chunk     = 1*("0") [ chunk-ext ] CRLF
    trailer-part   = *( header-field CRLF )

    chunk-size     = 1*HEXDIG
    chunk-data     = 1*OCTET ; a sequence of chunk-size octets
    chunk-ext      = *( ";" chunk-ext-name [ "=" chunk-ext-val ] )
    chunk-ext-name = token
    chunk-ext-val  = token / quoted-string
*/

    auto p = p0;
    auto const pend = p + n;
    char const* eol;

    if(! (f_ & flagFinalChunk))
    {
        if(n < skip_ + 2)
        {
            ec = error::need_more;
            return;
        }
        if(f_ & flagExpectCRLF)
        {
            // Treat the last CRLF in a chunk as
            // part of the next chunk, so p can
            // be parsed in one call instead of two.
            if(! parse_crlf(p))
            {
                ec = error::bad_chunk;
                return;
            }
        }
        eol = find_eol(p0 + skip_, pend, ec);
        if(ec)
            return;
        if(! eol)
        {
            ec = error::need_more;
            skip_ = n - 1;
            return;
        }
        skip_ = static_cast<
            std::size_t>(eol - 2 - p0);

        std::uint64_t v;
        if(! parse_hex(p, v))
        {
            ec = error::bad_chunk;
            return;
        }
        if(v != 0)
        {
            if(v > body_limit_)
            {
                ec = error::body_limit;
                return;
            }
            body_limit_ -= v;
            if(*p == ';')
            {
                // VFALCO TODO Validate extension
                impl().on_chunk(v, make_string(
                    p, eol - 2), ec);
                if(ec)
                    return;
            }
            else if(p == eol - 2)
            {
                impl().on_chunk(v, {}, ec);
                if(ec)
                    return;
            }
            else
            {
                ec = error::bad_chunk;
                return;
            }
            len_ = v;
            skip_ = 2;
            p0 = eol;
            f_ |= flagExpectCRLF;
            state_ = state::chunk_body;
            return;
        }

        f_ |= flagFinalChunk;
    }
    else
    {
        BOOST_ASSERT(n >= 5);
        if(f_ & flagExpectCRLF)
            BOOST_VERIFY(parse_crlf(p));
        std::uint64_t v;
        BOOST_VERIFY(parse_hex(p, v));
        eol = find_eol(p, pend, ec);
        BOOST_ASSERT(! ec);
    }

    auto eom = find_eom(p0 + skip_, pend);
    if(! eom)
    {
        BOOST_ASSERT(n >= 3);
        skip_ = n - 3;
        ec = error::need_more;
        return;
    }

    if(*p == ';')
    {
        // VFALCO TODO Validate extension
        impl().on_chunk(0, make_string(
            p, eol - 2), ec);
        if(ec)
            return;
    }
    p = eol;
    parse_fields(p, eom, ec);
    if(ec)
        return;
    BOOST_ASSERT(p == eom);
    p0 = eom;

    impl().on_complete(ec);
    if(ec)
        return;
    state_ = state::complete;
}

template<bool isRequest, class Derived>
inline
void
basic_parser<isRequest, Derived>::
parse_chunk_body(char const*& p,
    std::size_t n, error_code& ec)
{
    n = impl().on_data(string_view{p,
        beast::detail::clamp(len_, n)}, ec);
    p += n;
    len_ -= n;
    if(ec)
        return;
    if(len_ > 0)
        return;
    state_ = state::chunk_header;
}

template<bool isRequest, class Derived>
void
basic_parser<isRequest, Derived>::
do_field(field f,
    string_view value, error_code& ec)
{
    // Connection
    if(f == field::connection ||
        f == field::proxy_connection)
    {
        auto const list = opt_token_list{value};
        if(! validate_list(list))
        {
            // VFALCO Should this be a field specific error?
            ec = error::bad_value;
            return;
        }
        for(auto const& s : list)
        {
            if(iequals({"close", 5}, s))
            {
                f_ |= flagConnectionClose;
                continue;
            }

            if(iequals({"keep-alive", 10}, s))
            {
                f_ |= flagConnectionKeepAlive;
                continue;
            }

            if(iequals({"upgrade", 7}, s))
            {
                f_ |= flagConnectionUpgrade;
                continue;
            }
        }
        ec.assign(0, ec.category());
        return;
    }

    // Content-Length
    if(f == field::content_length)
    {
        if(f_ & flagContentLength)
        {
            // duplicate
            ec = error::bad_content_length;
            return;
        }

        if(f_ & flagChunked)
        {
            // conflicting field
            ec = error::bad_content_length;
            return;
        }

        std::uint64_t v;
        if(! parse_dec(
            value.begin(), value.end(), v))
        {
            ec = error::bad_content_length;
            return;
        }

        if(v > body_limit_)
        {
            ec = error::body_limit;
            return;
        }

        ec.assign(0, ec.category());
        len_ = v;
        f_ |= flagContentLength;
        return;
    }

    // Transfer-Encoding
    if(f == field::transfer_encoding)
    {
        if(f_ & flagChunked)
        {
            // duplicate
            ec = error::bad_transfer_encoding;
            return;
        }

        if(f_ & flagContentLength)
        {
            // conflicting field
            ec = error::bad_transfer_encoding;
            return;
        }

        ec.assign(0, ec.category());
        auto const v = token_list{value};
        auto const p = std::find_if(v.begin(), v.end(),
            [&](typename token_list::value_type const& s)
            {
                return iequals({"chunked", 7}, s);
            });
        if(p == v.end())
            return;
        if(std::next(p) != v.end())
            return;
        len_ = 0;
        f_ |= flagChunked;
        return;
    }

    // Upgrade
    if(f == field::upgrade)
    {
        ec.assign(0, ec.category());
        f_ |= flagUpgrade;
        return;
    }

    ec.assign(0, ec.category());
}

} // http
} // beast

#endif
