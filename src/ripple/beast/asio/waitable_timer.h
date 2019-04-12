//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_BEAST_ASIO_WAITABLETIMER_H_INCLUDED
#define RIPPLE_BEAST_ASIO_WAITABLETIMER_H_INCLUDED

#include <boost/asio/ip/tcp.hpp>

namespace beast {

// Pre boost 1.70, a waitable timer may only be created from an io_context.
// However, post 1.70, the `get_io_service` function is deleted. Post
// boost 1.70, a waitable time may be created from an executor. This functions
// allows a waitable timer to be created from a socket in both pre and post
// boost 1.70 versions
template <class T>
T
create_waitable_timer(boost::asio::ip::tcp::socket& socket)
{
#if BOOST_VERSION >= 107000
    return T{socket.get_executor()};
#else
    return T{socket.get_io_service()};
#endif
}

}  // namespace ripple

#endif
