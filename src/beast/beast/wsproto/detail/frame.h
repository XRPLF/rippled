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

#ifndef BEAST_WSPROTO_FRAME_H_INCLUDED
#define BEAST_WSPROTO_FRAME_H_INCLUDED

#include <beast/wsproto/rfc6455.h>
#include <beast/wsproto/static_string.h>
#include <beast/wsproto/detail/utf8_checker.h>
#include <beast/asio/consuming_buffers.h>
#include <beast/asio/static_streambuf.h>
#include <boost/asio/buffer.hpp>
#include <boost/endian/buffers.hpp>
#include <cassert>
#include <cstdint>

namespace beast {
namespace wsproto {
namespace detail {

// Contents of a WebSocket frame header
struct frame_header
{
    opcode op;
    bool fin;
    bool mask;
    bool rsv1;
    bool rsv2;
    bool rsv3;
    std::uint64_t len;
    std::uint32_t key;
};

// holds the largest possible frame header
using fh_streambuf =
    static_streambuf_n<14>;

// holds the largest possible control frame
using frame_streambuf =
    static_streambuf_n< 2 + 8 + 4 + 125 >;

inline
bool constexpr
is_reserved(opcode op)
{
    return
        (op >= opcode::rsv3  && op <= opcode::rsv7) ||
        (op >= opcode::crsvb && op <= opcode::crsvf);
}

inline
bool constexpr
is_valid(opcode op)
{
    return op <= opcode::crsvf;
}

inline
bool constexpr
is_control(opcode op)
{
    return op >= opcode::close;
}

// Returns `true` if a close code is valid
// Note this accepts 0 as a valid close code.
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

//------------------------------------------------------------------------------

// Write frame header to streambuf
//
template<class Streambuf>
void
write(Streambuf& sb, frame_header const& fh)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using namespace boost::endian;
    std::size_t n;
    std::uint8_t b[14];
    b[0] = (fh.fin ? 0x80 : 0x00) | static_cast<std::uint8_t>(fh.op);
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
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
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
    fh.op   = static_cast<opcode>(b[0] & 0x0f);
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
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
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
    code = close::none;
}

// Read data from buffers
// This is for ping and pong payloads
//
template<class Buffers>
void
read(ping_payload_type& data,
    Buffers const& bs, close::value& code)
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    using boost::asio::mutable_buffers_1;
    assert(buffer_size(bs) <= data.max_size());
    data.resize(buffer_size(bs));
    buffer_copy(mutable_buffers_1{
        data.data(), data.size()}, bs);
}

// Read close_reason, return true on success
// This is for the close payload
//
template<class Buffers>
void
read(close_reason& cr,
    Buffers const& bs, close::value& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    using namespace boost::endian;
    auto n = buffer_size(bs);
    assert(n <= 125);
    if(n == 0)
    {
        cr = close_reason{};
        code = close::none;
        return;
    }
    if(n == 1)
    {
        code = close::protocol_error;
        return;
    }
    consuming_buffers<Buffers> cb(bs);
    {
        std::uint8_t b[2];
        buffer_copy(buffer(b), cb);
        cr.code = static_cast<close::value>(
            reinterpret_cast<
                big_uint16_buf_t const*>(&b[0])->value());
        cb.consume(2);
        n -= 2;
        // Check cr.code against 0 because
        // is_valid considers 0 to be valid.
        if(cr.code == 0 || ! is_valid(cr.code))
        {
            code = close::protocol_error;
            return;
        }
    }
    if(n > 0)
    {
        cr.reason.resize(n);
        buffer_copy(buffer(&cr.reason[0], n), cb);
        if(! detail::check_utf8(
            cr.reason.data(), cr.reason.size()))
        {
            code = close::protocol_error;
            return;
        }
    }
    else
    {
        cr.reason = "";
    }
    code = close::none;
}

} // detail
} // wsproto
} // beast

#endif
