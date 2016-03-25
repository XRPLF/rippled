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

#ifndef BEAST_WSPROTO_DETAIL_FRAME_H_INCLUDED
#define BEAST_WSPROTO_DETAIL_FRAME_H_INCLUDED

#include <beast/wsproto/error.h>
#include <beast/wsproto/frame.h>
#include <beast/wsproto/role.h>
#include <beast/wsproto/detail/utf8_checker.h>
#include <beast/wsproto/detail/mask.h>
#include <beast/asio/consuming_buffers.h>
#include <boost/asio/buffer.hpp>
#include <boost/endian/buffers.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

namespace beast {
namespace wsproto {
namespace detail {

inline
bool constexpr
is_reserved(opcode::value op)
{
    return
        (op >= opcode::rsv3  && op <= opcode::rsv7) ||
        (op >= opcode::crsvb && op <= opcode::crsvf);
}

inline
bool constexpr
is_valid(opcode::value op)
{
    return op >= 0 && op <= opcode::crsvf;
}

inline
bool constexpr
is_control(opcode::value op)
{
    return op >= opcode::close;
}

// Returns `true` if a close code is valid
inline
bool
is_valid(close::value code)
{
    auto const v = static_cast<
        std::uint16_t>(code);
    switch(v)
    {
    case 1000:
    case 1001:
    case 1002:
    case 1003:
    case 1007:
    case 1008:
    case 1009:
    case 1010:
    case 1011:
    case 1012:
    case 1013:
        return true;

    // explicitly reserved
    case 1004:
    case 1005:
    case 1006:
    case 1014:
    case 1015:
        return false;
    }
    // reserved
    if(v >= 1016 && v <= 2999)
        return false;
    // not used
    if(v >= 0 && v <= 999)
        return false;
    return true;
}

// Returns `true` if the close code indicates an unrecoverable error
/*
    If the close code indicates an unrecoverable error, the
    implementation will either not send or not wait for a close
    message.
*/
// VFALCO TODO Use this?
inline
bool constexpr
is_terminal(close::value code)
{
    return
        code == close::protocol_error   ||
        code == close::bad_payload      ||
        code == close::policy_error     ||
        code == close::too_big          ||
        code == close::internal_error
        ;
}

//------------------------------------------------------------------------------

// Write frame header to streambuf
//
template<class Streambuf>
void
write(Streambuf& sb, frame_header const& fh)
{
    using namespace boost::asio;
    using namespace boost::endian;

    std::size_t n;
    std::uint8_t b[14];
    b[0] = (fh.fin ? 0x80 : 0x00) | fh.op;
    b[1] = fh.mask ? 0x80 : 0x00;
    if (fh.len <= 125)
    {
        b[1] |= fh.len;
        n = 2;
    }
    else if (fh.len <= 65535)
    {
        b[1] |= 126;
        ::new(&b[2]) big_uint16_buf_t{
            (std::uint16_t)fh.len};
        n = 4;
    }
    else
    {
        b[1] |= 127;
        ::new(&b[2]) big_uint64_buf_t{fh.len};
        n = 10;
    }
    if(fh.mask)
    {
        little_uint32_buf_t key(fh.key);
        std::copy(key.data(),
            key.data() + 4, &b[n]);
        n += 4;
    }
    sb.commit(buffer_copy(
        sb.prepare(n), buffer(b)));
}

// Read fixed frame header
// Requires at least 2 bytes
//
template<class Streambuf>
std::size_t
read_fh1(frame_header& fh, Streambuf& sb,
    role_type role, close::value& code)
{
    using namespace boost::asio;
    std::uint8_t b[2];
    assert(buffer_size(sb.data()) >= sizeof(b));
    sb.consume(buffer_copy(buffer(b), sb.data()));
    std::size_t need;
    fh.len = b[1] & 0x7f;
    switch(fh.len)
    {
        case 126: need = 2; break;
        case 127: need = 8; break;
        default:
            need = 0;
    }
    if((fh.mask = (b[1] & 0x80)))
        need += 4;
    fh.op   = static_cast<opcode::value>(b[0] & 0x0f);
    fh.fin  = b[0] & 0x80;
    fh.rsv1 = b[0] & 0x40;
    fh.rsv2 = b[0] & 0x20;
    fh.rsv3 = b[0] & 0x10;
    // invalid length for control message
    if(is_control(fh.op) && fh.len > 125)
    {
        code = close::protocol_error;
        return 0;
    }

    // reserved bits not cleared
    if(fh.rsv1 || fh.rsv2 || fh.rsv3)
    {
        code = close::protocol_error;
        return 0;
    }
    // reserved opcode
    if(is_reserved(fh.op))
    {
        code = close::protocol_error;
        return 0;
    }
    // invalid opcode
    // (only in locally generated headers)
    if(! is_valid(fh.op))
    {
        code = close::protocol_error;
        return 0;
    }
    // fragmented control message
    if(is_control(fh.op) && ! fh.fin)
    {
        code = close::protocol_error;
        return 0;
    }
    // unmasked frame from client
    if(role == role_type::server && ! fh.mask)
    {
        code = close::protocol_error;
        return 0;
    }
    // masked frame from server
    if(role == role_type::client && fh.mask)
    {
        code = close::protocol_error;
        return 0;
    }
    code = close::none;
    return need;
}

// Decode variable frame header from stream
//
template<class Streambuf>
void
read_fh2(frame_header& fh, Streambuf& sb,
    role_type role, close::value& code)
{
    using namespace boost::asio;
    using namespace boost::endian;
    switch(fh.len)
    {
    case 126:
    {
        std::uint8_t b[2];
        assert(buffer_size(sb.data()) >= sizeof(b));
        sb.consume(buffer_copy(buffer(b), sb.data()));
        fh.len = reinterpret_cast<
            big_uint16_buf_t const*>(&b[0])->value();
        // length not canonical
        if(fh.len < 126)
        {
            code = close::protocol_error;
            return;
        }
        break;
    }
    case 127:
    {
        std::uint8_t b[8];
        assert(buffer_size(sb.data()) >= sizeof(b));
        sb.consume(buffer_copy(buffer(b), sb.data()));
        fh.len = reinterpret_cast<
            big_uint64_buf_t const*>(&b[0])->value();
        // length not canonical
        if(fh.len < 65536)
        {
            code = close::protocol_error;
            return;
        }
        break;
    }
    }
    if(fh.mask)
    {
        std::uint8_t b[4];
        assert(buffer_size(sb.data()) >= sizeof(b));
        sb.consume(buffer_copy(buffer(b), sb.data()));
        fh.key = reinterpret_cast<
            little_uint32_buf_t const*>(&b[0])->value();
    }
    code= close::none;
}

// Read data from buffers
// This is for ping and pong payloads
//
template<class Buffers>
void
read(std::string& data,
    Buffers const& bs, close::value& code)
{
    using namespace boost::asio;
    for(auto const& b : bs)
        data.append(buffer_cast<char const*>(b),
            buffer_size(b));
    // TODO utf8_check(s) VFALCO Is this needed?
}

// Read reason_code, return true on success
// This is for the close payload
//
template<class Buffers>
void
read(reason_code& rc,
    Buffers const& bs, close::value& code)
{
    using namespace boost::asio;
    using namespace boost::endian;
    auto n = buffer_size(bs);
    assert(n <= 125);
    if(n == 0)
    {
        rc = reason_code{};
        code = close::none;
        return;
    }
    if(n == 1)
    {
        rc = reason_code{}; // VFALCO ?? Why
        code = close::protocol_error;
        return;
    }
    beast::asio::consuming_buffers<
        const_buffer, Buffers> cb(bs);
    {
        std::uint8_t b[2];
        buffer_copy(buffer(b), cb);
        rc.code = static_cast<close::value>(
            reinterpret_cast<
                big_uint16_buf_t const*>(&b[0])->value());
        cb.consume(2);
        n -= 2;
    }
    if(n > 0)
    {
        rc.reason.resize(n);
        buffer_copy(buffer(&rc.reason[0], n), cb);
        if(! detail::check_utf8(rc.reason))
        {
            code = close::protocol_error;
            return;
        }
    }
    else
    {
        rc.reason = "";
    }
    code = close::none;
}

} // detail
} // wsproto
} // beast

#endif
