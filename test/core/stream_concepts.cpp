//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/stream_concepts.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast {

using stream_type = boost::asio::ip::tcp::socket;

static_assert(has_get_io_service<stream_type>::value, "");
static_assert(is_AsyncReadStream<stream_type>::value, "");
static_assert(is_AsyncWriteStream<stream_type>::value, "");
static_assert(is_AsyncStream<stream_type>::value, "");
static_assert(is_SyncReadStream<stream_type>::value, "");
static_assert(is_SyncWriteStream<stream_type>::value, "");
static_assert(is_SyncStream<stream_type>::value, "");

static_assert(! has_get_io_service<int>::value, "");
static_assert(! is_AsyncReadStream<int>::value, "");
static_assert(! is_AsyncWriteStream<int>::value, "");
static_assert(! is_SyncReadStream<int>::value, "");
static_assert(! is_SyncWriteStream<int>::value, "");

} // beast
