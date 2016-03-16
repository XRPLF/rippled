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

#include <array>
#include <cstdint>

namespace beast {
namespace wsproto {

/** WebSocket frame header opcodes. */
namespace opcode {
    enum value : std::uint8_t
    {
        cont    = 0,
        text    = 1,
        binary  = 2,
        rsv3    = 3,
        rsv4    = 4,
        rsv5    = 5,
        rsv6    = 6,
        rsv7    = 7,
        close   = 8,
        ping    = 9,
        pong    = 10,
        crsvb   = 11,
        crsvc   = 12,
        crsvd   = 13,
        crsve   = 14,
        crsvf   = 15
    };
}

/** Contents of a WebSocket frame header. */
struct frame_header
{
    opcode::value op;
    bool fin;
    bool mask;
    bool rsv1;
    bool rsv2;
    bool rsv3;
    std::uint64_t len;
    std::uint32_t key;
};

inline
bool
is_reserved(opcode::value op)
{
    return
        (op >= opcode::rsv3  && op <= opcode::rsv7) ||
        (op >= opcode::crsvb && op <= opcode::crsvf);
}

inline
bool 
is_invalid(opcode::value op)
{
    return op > 15;
}

inline
bool
is_control(opcode::value op)
{
    return op >= 8;
}

} // wsproto
} // beast

#endif
