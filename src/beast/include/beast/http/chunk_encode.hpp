//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_CHUNK_ENCODE_HPP
#define BEAST_HTTP_CHUNK_ENCODE_HPP

#include <beast/core/buffer_cat.hpp>
#include <beast/http/detail/chunk_encode.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace beast {
namespace http {

/** Returns a chunk-encoded ConstBufferSequence.

    This returns a buffer sequence representing the
    first chunk of a chunked transfer coded body.

    @param fin `true` if this is the last chunk.

    @param buffers The input buffer sequence.

    @return A chunk-encoded ConstBufferSequence representing the input.

    @see <a href=https://tools.ietf.org/html/rfc7230#section-4.1.3>rfc7230 section 4.1.3</a>
*/
template<class ConstBufferSequence>
#if GENERATING_DOCS
implementation_defined
#else
beast::detail::buffer_cat_helper<
    detail::chunk_encode_delim,
    ConstBufferSequence,
    boost::asio::const_buffers_1>
#endif
chunk_encode(bool fin, ConstBufferSequence const& buffers)
{
    using boost::asio::buffer_size;
    return buffer_cat(
        detail::chunk_encode_delim{buffer_size(buffers)},
        buffers,
        fin ? boost::asio::const_buffers_1{"\r\n0\r\n\r\n", 7}
            : boost::asio::const_buffers_1{"\r\n", 2});
}

/** Returns a chunked encoding final chunk.

    @see <a href=https://tools.ietf.org/html/rfc7230#section-4.1.3>rfc7230 section 4.1.3</a>
*/
inline
#if GENERATING_DOCS
implementation_defined
#else
boost::asio::const_buffers_1
#endif
chunk_encode_final()
{
    return boost::asio::const_buffers_1{"0\r\n\r\n", 5};
}

} // http
} // beast

#endif
