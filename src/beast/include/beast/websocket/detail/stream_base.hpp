//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP
#define BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP

#include <beast/websocket/error.hpp>
#include <beast/websocket/rfc6455.hpp>
#include <beast/websocket/detail/decorator.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <beast/websocket/detail/invokable.hpp>
#include <beast/websocket/detail/mask.hpp>
#include <beast/websocket/detail/utf8_checker.hpp>
#include <beast/streambuf.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <boost/asio/error.hpp>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace beast {
namespace websocket {
namespace detail {

template<class String>
inline
void
maybe_throw(error_code const& ec, String const&)
{
    if(ec)
        throw boost::system::system_error{ec};
}

template<class UInt>
static
std::size_t
clamp(UInt x)
{
    if(x >= std::numeric_limits<std::size_t>::max())
        return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(x);
}

template<class UInt>
static
std::size_t
clamp(UInt x, std::size_t limit)
{
    if(x >= limit)
        return limit;
    return static_cast<std::size_t>(x);
}

//------------------------------------------------------------------------------

struct stream_base
{
protected:
    struct op {};

    detail::maskgen maskgen_;           // source of mask keys
    decorator_type d_;                  // adorns http messages
    bool keep_alive_ = false;           // close on failed upgrade
    role_type role_;                    // server or client
    bool error_ = false;                // non-zero ec was delivered

    std::size_t rd_msg_max_ =
        16 * 1024 * 1024;               // max message size
    detail::frame_header rd_fh_;        // current frame header
    detail::prepared_key_type rd_key_;  // prepared masking key
    detail::utf8_checker rd_utf8_check_;// for current text msg
    std::uint64_t rd_size_;             // size of the current message so far
    std::uint64_t rd_need_ = 0;         // bytes left in msg frame payload
    opcode rd_opcode_;                  // opcode of current msg
    bool rd_cont_ = false;              // expecting a continuation frame
    bool rd_close_ = false;             // got close frame
    op* rd_block_ = nullptr;            // op currently reading

    std::size_t
        wr_frag_size_ = 16 * 1024;      // size of auto-fragments
    std::size_t wr_buf_size_ = 4096;    // write buffer size
    opcode wr_opcode_ = opcode::text;   // outgoing message type
    bool wr_close_ = false;             // sent close frame
    bool wr_cont_ = false;              // next write is continuation frame
    op* wr_block_ = nullptr;            // op currenly writing

    invokable rd_op_;                   // invoked after write completes
    invokable wr_op_;                   // invoked after read completes
    close_reason cr_;                   // set from received close frame

    stream_base()
        : d_(new decorator<default_decorator>{})
    {
    }

    stream_base(stream_base&&) = default;
    stream_base(stream_base const&) = delete;
    stream_base& operator=(stream_base&&) = default;
    stream_base& operator=(stream_base const&) = delete;

    template<class = void>
    void
    prepare_fh(close_code::value& code);

    template<class Streambuf>
    void
    write_close(Streambuf& sb,
        close_reason const& rc);

    template<class Streambuf>
    void
    write_ping(Streambuf& sb, opcode op,
        ping_payload_type const& data);
};

} // detail
} // websocket
} // beast

#endif
