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

    detail::frame_header rd_fh_;        // current frame header
    detail::prepared_key_type rd_key_;  // prepared masking key
    detail::utf8_checker rd_utf8_check_;// for current text msg
    std::size_t rd_need_ = 0;           // bytes left in msg frame payload
    opcode::value rd_opcode_;           // opcode of current msg
    bool rd_cont_ = false;              // expecting a continuation frame
    bool rd_close_ = false;             // got close frame
    op* rd_block_ = nullptr;            // op currently reading

    std::size_t wr_frag_ = 0;           // size of auto-fragments
    bool wr_cont_ = false;              // write continuation is legal
    bool wr_close_ = false;             // sent close frame
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
    prepare_fh(close::value& code);

    template<class Streambuf>
    void
    write_close(Streambuf& sb,
        close_reason const& rc);

    template<class Streambuf>
    void
    write_ping(Streambuf& sb, opcode::value op,
        ping_payload_type const& data);
};

} // detail
} // wsproto
} // beast

#endif
