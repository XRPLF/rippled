//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_ERROR_HPP
#define BEAST_WEBSOCKET_ERROR_HPP

#include <beast/config.hpp>
#include <beast/core/error.hpp>

namespace beast {
namespace websocket {

/// Error codes returned from @ref beast::websocket::stream operations.
enum class error
{
    /// Both sides performed a WebSocket close
    closed = 1,

    /// WebSocket connection failed, protocol violation
    failed,

    /// Upgrade handshake failed
    handshake_failed
};

} // websocket
} // beast

#include <beast/websocket/impl/error.ipp>

#endif
