//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_RFC6455_HPP
#define BEAST_WEBSOCKET_RFC6455_HPP

#include <beast/static_string.hpp>
#include <boost/optional.hpp>
#include <array>
#include <cstdint>

namespace beast {
namespace websocket {

/** WebSocket frame header opcodes. */
enum class opcode : std::uint8_t
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

/** Close status codes.

    These codes accompany close frames.

    @see <a href="https://tools.ietf.org/html/rfc6455#section-7.4.1">RFC 6455 7.4.1 Defined Status Codes</a>

*/
#if GENERATING_DOCS
enum close_code
#else
namespace close_code {
using value = std::uint16_t;
enum
#endif
{
    /// used internally to mean "no error"
    none            = 0,

    normal          = 1000,
    going_away      = 1001,
    protocol_error  = 1002,

    unknown_data    = 1003,
    bad_payload     = 1007,
    policy_error    = 1008,
    too_big         = 1009,
    needs_extension = 1010,
    internal_error  = 1011,

    service_restart = 1012,
    try_again_later = 1013,

    reserved1       = 1004,
    no_status       = 1005, // illegal on wire
    abnormal        = 1006, // illegal on wire
    reserved2       = 1015,

    last = 5000 // satisfy warnings
};
#if ! GENERATING_DOCS
} // close_code
#endif

#if ! GENERATING_DOCS
using reason_string_type =
    static_string<123, char>;
using ping_payload_type =
    static_string<125, char>;
#endif

/** Description of the close reason.

    This object stores the close code (if any) and the optional
    utf-8 encoded implementation defined reason string.
*/
struct close_reason
{
    /// The close code.
    close_code::value code = close_code::none;

    /// The optional utf8-encoded reason string.
    reason_string_type reason;

    /** Default constructor.

        The code will be none. Default constructed objects
        will explicitly convert to bool as `false`.
    */
    close_reason() = default;

    /// Construct from a code.
    close_reason(close_code::value code_)
        : code(code_)
    {
    }

    /// Construct from a reason. code is close_code::normal.
    template<class CharT>
    close_reason(CharT const* reason_)
        : code(close_code::normal)
        , reason(reason_)
    {
    }

    /// Construct from a code and reason.
    template<class CharT>
    close_reason(close_code::value code_,
            CharT const* reason_)
        : code(code_)
        , reason(reason_)
    {
    }

    /// Returns `true` if a code was specified
    operator bool() const
    {
        return code != close_code::none;
    }
};

#if ! GENERATING_DOCS

/// Identifies the role of a WebSockets stream.
enum class role_type
{
    /// Stream is operating as a client.
    client,

    /// Stream is operating as a server.
    server
};

#endif

} // websocket
} // beast

#endif
