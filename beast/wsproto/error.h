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

#ifndef BEAST_WSPROTO_ERROR_H_INCLUDED
#define BEAST_WSPROTO_ERROR_H_INCLUDED

#include <boost/system/error_code.hpp>

namespace beast {
namespace wsproto {

using error_code = boost::system::error_code;

/// Error values
enum class error
{
    /// Both sides performed a WebSocket close
    closed = 1,

    /// WebSocket connection failed, protocol violation
    failed,

    /// Upgrade request failed, connection is closed
    handshake_failed,

    /// Upgrade request failed, but connection is still open
    keep_alive,

    /// HTTP response is malformed
    response_malformed,

    /// HTTP response failed the upgrade
    response_failed,

    /// Upgrade request denied for invalid fields.
    response_denied,

    /// Upgrade request is malformed
    request_malformed,

    /// Upgrade request fields incorrect
    request_invalid,

    /// Upgrade request denied
    request_denied
};

error_code
make_error_code(error e);

} // wsproto
} // beast

#include <beast/wsproto/impl/error.ipp>

#endif
