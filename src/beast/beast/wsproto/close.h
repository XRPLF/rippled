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

#ifndef BEAST_WSPROTO_CLOSE_H_INCLUDED
#define BEAST_WSPROTO_CLOSE_H_INCLUDED

#include <cstdint>

namespace beast {
namespace wsproto {

/// Close status codes.
/** These codes accompany close frames.
    
    @see RFC 6455 7.4.1 Defined Status Codes
    https://tools.ietf.org/html/rfc6455#section-7.4.1
*/
namespace close {
    enum value : std::uint16_t {
        none            = 0,    // indicates no code received

        normal          = 1000,
        going_away      = 1001,
        protocol_error  = 1002,
        unknown_data    = 1003,
        no_status       = 1005, // illegal on wire
        abnormal        = 1006, // illegal on wire
        bad_payload     = 1007,
        policy_error    = 1008,
        too_big         = 1009,
        needs_extension = 1010,
        internal_error  = 1011,
        service_restart = 1012,
        try_again_later = 1013,

        last = 5000 // satisfy warnings
    };
} // close

/// Description of the close reason.
/**
    This object stores the close code (if any) and the optional
    utf-8 encoded implementation defined reason string.
*/
struct reason_code
{
    close::value code = close::none;
    std::string reason;

    /// Default constructor.
    /**
        The code will be none. Default constructed objects
        will explicitly convert to bool as `false`.
    */
    reason_code() = default;

    /// Construct from a code.
    explicit
    reason_code(close::value code_)
        : code(code_)
    {
    }

    /// Construct from a code and reason.
    template<class String>
    reason_code(close::value code_,
            String const& reason_)
        : code(code_)
        , reason(reason_)
    {
    }

    /// Returns `true` if a code was specified
    operator bool() const
    {
        return code != close::none;
    }
};

/// Returns `true` if a close code is reserved.
inline
bool constexpr
is_reserved(close::value code)
{
    return (code >= 1016 && code <= 2999) ||
        code == 1004 || code == 1014;
}

/// Returns `true` if a close code is invalid.
inline
bool constexpr
is_invalid(close::value code)
{
    return (code <= 999 || code >= 5000) ||
        code == close::no_status ||
        code == close::abnormal;
}

/// Returns `true` if the close code indicates an unrecoverable error
/**
    If the close code indicates an unrecoverable error, the
    implementation will either not send or not wait for a close
    message.
*/
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

} // wsproto
} // beast

#endif
