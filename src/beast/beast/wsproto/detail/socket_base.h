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

#ifndef BEAST_WSPROTO_SOCKET_BASE_H_INCLUDED
#define BEAST_WSPROTO_SOCKET_BASE_H_INCLUDED

#include <beast/wsproto/error.h>
#include <beast/wsproto/rfc6455.h>
#include <beast/wsproto/detail/decorator.h>
#include <beast/wsproto/detail/frame.h>
#include <beast/wsproto/detail/invokable.h>
#include <beast/wsproto/detail/mask.h>
#include <beast/wsproto/detail/utf8_checker.h>
#include <beast/asio/streambuf.h>
#include <beast/http/empty_body.h>
#include <beast/http/message.h>
#include <beast/http/string_body.h>
#include <boost/asio/error.hpp>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace beast {
namespace wsproto {
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

struct socket_base
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

    socket_base()
        : d_(std::make_unique<
            decorator<default_decorator>>())
    {
    }

    socket_base(socket_base&&) = default;
    socket_base(socket_base const&) = delete;
    socket_base& operator=(socket_base&&) = default;
    socket_base& operator=(socket_base const&) = delete;

    template<class = void>
    void
    prepare_fh(close_code& code);

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
} // wsproto
} // beast

#endif
