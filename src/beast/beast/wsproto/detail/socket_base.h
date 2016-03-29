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
#include <beast/wsproto/detail/frame.h>
#include <beast/wsproto/detail/invokable.h>
#include <beast/wsproto/detail/mask.h>
#include <beast/wsproto/detail/utf8_checker.h>
#include <beast/asio/streambuf.h>
#include <beast/http/message.h>
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

using decorator_type =
    std::function<void(beast::http::message&)>;

//------------------------------------------------------------------------------

struct socket_base
{
    detail::maskgen maskgen_;
    decorator_type decorate_;
    bool keep_alive_ = false;
    role_type role_;

    // buffer for reading
    asio::streambuf rd_sb_;

    // current frame header
    detail::frame_header rd_fh_;

    // prepared masking key
    detail::prepared_key_type rd_key_;

    // utf8 check state for current text msg
    detail::utf8_checker rd_utf8_check_;

    // bytes remaining in binary/text frame payload
    std::size_t rd_need_ = 0;

    // opcode of current binary or text message
    opcode::value rd_op_;

    // expecting a continuation frame
    bool rd_cont_ = false;

    bool wr_cont_ = false;

    std::size_t wr_frag_ = 0;

    // true when async write is pending
    bool wr_active_ = false;

    invokable wr_invoke_;

    invokable rd_invoke_;

    bool fail_ = false;

    //-----------------------------------------------------------

    socket_base()
        : decorate_([](auto&){})
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
