//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_SSL_IPP_INCLUDED
#define BEAST_WEBSOCKET_IMPL_SSL_IPP_INCLUDED

#include <utility>

namespace beast {
namespace websocket {

/*

See
http://stackoverflow.com/questions/32046034/what-is-the-proper-way-to-securely-disconnect-an-asio-ssl-socket/32054476#32054476

Behavior of ssl::stream regarding close_

    If the remote host calls async_shutdown then the
    local host's async_read will complete with eof.

    If both hosts call async_shutdown then the calls
    to async_shutdown will complete with eof.

*/

template<class AsyncStream>
void
teardown(teardown_tag,
    boost::asio::ssl::stream<AsyncStream>& stream,
        error_code& ec)
{
    stream.shutdown(ec);
}

template<class AsyncStream, class TeardownHandler>
void
async_teardown(teardown_tag,
    boost::asio::ssl::stream<AsyncStream>& stream,
        TeardownHandler&& handler)
{
    stream.async_shutdown(std::forward<TeardownHandler>(handler));
}

} // websocket
} // beast

#endif
