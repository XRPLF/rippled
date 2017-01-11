//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_FRAME_HPP
#define BEAST_WEBSOCKET_DETAIL_FRAME_HPP

#include <beast/websocket/rfc6455.hpp>
#include <beast/websocket/detail/endian.hpp>
#include <beast/websocket/detail/utf8_checker.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/static_streambuf.hpp>
#include <beast/core/static_string.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/assert.hpp>
#include <boost/endian/buffers.hpp>
#include <cstdint>

namespace beast {
namespace websocket {
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
inline
bool
is_valid(close_code::value code)
{
    auto const v = code;
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
    if(v <= 999)
        return false;
    return true;
}

//------------------------------------------------------------------------------

// Write frame header to dynamic buffer
//
template<class DynamicBuffer>
void
write(DynamicBuffer& db, frame_header const& fh)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using namespace boost::endian;
    std::size_t n;
    std::uint8_t b[14];
    b[0] = (fh.fin ? 0x80 : 0x00) | static_cast<std::uint8_t>(fh.op);
    if(fh.rsv1)
        b[0] |= 0x40;
    if(fh.rsv2)
        b[0] |= 0x20;
    if(fh.rsv3)
        b[0] |= 0x10;
    b[1] = fh.mask ? 0x80 : 0x00;
    if(fh.len <= 125)
    {
        b[1] |= fh.len;
        n = 2;
    }
    else if(fh.len <= 65535)
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
        native_to_little_uint32(fh.key, &b[n]);
        n += 4;
    }
    db.commit(buffer_copy(
        db.prepare(n), buffer(b)));
}

// Read data from buffers
// This is for ping and pong payloads
//
template<class Buffers>
void
read(ping_data& data, Buffers const& bs)
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    using boost::asio::mutable_buffers_1;
    BOOST_ASSERT(buffer_size(bs) <= data.max_size());
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
    Buffers const& bs, close_code::value& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    using namespace boost::endian;
    auto n = buffer_size(bs);
    BOOST_ASSERT(n <= 125);
    if(n == 0)
    {
        cr = close_reason{};
        code = close_code::none;
        return;
    }
    if(n == 1)
    {
        code = close_code::protocol_error;
        return;
    }
    consuming_buffers<Buffers> cb(bs);
    {
        std::uint8_t b[2];
        buffer_copy(buffer(b), cb);
        cr.code = big_uint16_to_native(&b[0]);
        cb.consume(2);
        n -= 2;
        if(! is_valid(cr.code))
        {
            code = close_code::protocol_error;
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
            code = close_code::protocol_error;
            return;
        }
    }
    else
    {
        cr.reason = "";
    }
    code = close_code::none;
}

} // detail
} // websocket
} // beast

#endif
