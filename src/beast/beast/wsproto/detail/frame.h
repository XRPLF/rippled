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
#include <beast/wsproto/detail/mask.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/endian/buffers.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace beast {
namespace wsproto {
namespace detail {

struct frame_state
{
    role_type role;
    bool cont = false; // expecting a continuation frame
};

// debug assistant
template<class ConstBufferSequence>
std::string
buffersToString(ConstBufferSequence const& bs)
{
    std::string s;
    using namespace boost::asio;
    s.reserve(buffer_size(bs));
    std::copy(buffers_begin(bs),
        buffers_end(bs), std::back_inserter(s));
    return s;
}

// holds any encoded frame header
// 14 = 2 + max(0,2,8) + 4
//
using fh_buffer = std::array<std::uint8_t, 14>;

// decode first 2 bytes of frame header
// return: number of additional bytes needed
//
template<class = void>
std::size_t
decode_fh1(frame_header& fh, fh_buffer const& b)
{
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
    return need;
}

// decode remainder of frame header
// requires decode_fh1 called first
//
template<class = void>
error_code
decode_fh2(frame_header& fh, fh_buffer const& b)
{
    using namespace boost::endian;
    std::size_t n;
    switch(fh.len)
    {
    case 126:
        fh.len = reinterpret_cast<
            big_uint16_buf_t const*>(&b[2])->value();
        // length not canonical
        if(fh.len < 126)
            return error::frame_header_invalid;
        n = 4;
        break;
    case 127:
        fh.len = reinterpret_cast<
            big_uint64_buf_t const*>(&b[2])->value();
        // length not canonical
        if(fh.len < 65536)
            return error::frame_header_invalid;
        n = 10;
        break;
    default:
        n = 2;
    }
    if(fh.mask)
        fh.key = reinterpret_cast<
            big_uint32_buf_t const*>(&b[n])->value();
    return {};
}

// encode the frame header into a fh_buffer
//
template<class = void>
boost::asio::const_buffers_1
encode_fh(fh_buffer& b, frame_header const& fh)
{
    std::size_t n;
    b[0] = (fh.fin ? 0x80 : 0x00) | fh.op;
    b[1] = fh.mask ? 0x80 : 0x00;
    using namespace boost::endian;
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
        big_uint32_buf_t key(fh.key);
        std::copy(key.data(),
            key.data() + 4, &b[n]);
        n += 4;
    }
    return { b.data(), n };
}

// write a frame header into a streambuf
//
template<class Streambuf>
void
write_fh(Streambuf& sb, frame_header const& fh)
{
    using namespace boost::asio;
    fh_buffer b;
    auto const fb = encode_fh(b, fh);
    sb.commit(buffer_copy(sb.prepare(
        buffer_size(fb)), fb));
}

// write the frame payload into a streambuf
//
template<class Streambuf, class ConstBuffers>
void
write_body(Streambuf& sb,
    frame_header const& fh, ConstBuffers const& cb)
{
    using namespace boost::asio;
    if(! fh.mask)
        sb.commit(buffer_copy(
            sb.prepare(buffer_size(cb)), cb));
    else
        sb.commit(mask_and_copy(
            sb.prepare(buffer_size(cb)),
                cb, fh.key));
}

// validate the contents of the frame
// header, and update the current frame state.
//
template<class = void>
error_code
update_frame_state(
    frame_state& fs, frame_header const& fh)
{
    // invalid length for control message
    if(is_control(fh.op) && fh.len > 125)
        return error::frame_header_invalid;

    // reserved bits not cleared
    if(fh.rsv1 || fh.rsv2 || fh.rsv2)
        return error::frame_header_invalid;

    // reserved opcode
    if(is_reserved(fh.op))
        return error::frame_header_invalid;

    // invalid opcode
    // (only in locally generated headers)
    if(is_invalid(fh.op))
        return error::frame_header_invalid;

    // fragmented control message
    if(is_control(fh.op) && ! fh.fin)
        return error::frame_header_invalid;

    // continuation without an active message
    if(! fs.cont && fh.op == opcode::cont)
        return error::frame_header_invalid;

    // new data frame when continuation expected
    if(fs.cont && ! is_control(fh.op) &&
            fh.op != opcode::cont)
        return error::frame_header_invalid;

    // unmasked frame from client
    if(fs.role == role_type::server && ! fh.mask)
        return error::frame_header_invalid;

    // masked frame from server
    if(fs.role == role_type::client && fh.mask)
        return error::frame_header_invalid;

    if(! is_control(fh.op))
        fs.cont = ! fh.fin;

    return {};
}

} // detail
} // wsproto
} // beast

#endif
