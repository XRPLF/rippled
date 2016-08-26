//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_CHUNK_ENCODE_HPP
#define BEAST_HTTP_DETAIL_CHUNK_ENCODE_HPP

#include <beast/core/buffer_cat.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/logic/tribool.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace beast {
namespace http {
namespace detail {

class chunk_encode_text
{
    boost::asio::const_buffer cb_;

    // Storage for the longest hex string we might need, plus delimiters.
    std::array<char, 2 * sizeof(std::size_t) + 2> buf_;

    template<class OutIter>
    static
    OutIter
    to_hex(OutIter last, std::size_t n)
    {
        if(n == 0)
        {
            *--last = '0';
            return last;
        }
        while(n)
        {
            *--last = "0123456789abcdef"[n&0xf];
            n>>=4;
        }
        return last;
    }

public:
    using value_type = boost::asio::const_buffer;

    using const_iterator = value_type const*;

    chunk_encode_text(chunk_encode_text const& other)
    {
        auto const n =
            boost::asio::buffer_size(other.cb_);
        buf_ = other.buf_;
        cb_ = boost::asio::const_buffer(
            &buf_[buf_.size() - n], n);
    }

    explicit
    chunk_encode_text(std::size_t n)
    {
        buf_[buf_.size() - 2] = '\r';
        buf_[buf_.size() - 1] = '\n';
        auto it = to_hex(buf_.end() - 2, n);
        cb_ = boost::asio::const_buffer{&*it,
            static_cast<std::size_t>(
                std::distance(it, buf_.end()))};
    }

    const_iterator
    begin() const
    {
        return &cb_;
    }

    const_iterator
    end() const
    {
        return begin() + 1;
    }
};

/** Returns a chunk-encoded ConstBufferSequence.

    This returns a buffer sequence representing the
    first chunk of a chunked transfer coded body.

    @param buffers The input buffer sequence.

    @return A chunk-encoded ConstBufferSequence representing the input.

    @see <a href=https://tools.ietf.org/html/rfc7230#section-4.1.3>rfc7230 section 4.1.3</a>
*/
template<class ConstBufferSequence>
#if GENERATING_DOCS
implementation_defined
#else
beast::detail::buffer_cat_helper<boost::asio::const_buffer,
    chunk_encode_text, ConstBufferSequence, boost::asio::const_buffers_1>
#endif
chunk_encode(ConstBufferSequence const& buffers)
{
    using boost::asio::buffer_size;
    return buffer_cat(
        chunk_encode_text{buffer_size(buffers)},
        buffers,
        boost::asio::const_buffers_1{"\r\n", 2});
}

/// Returns a chunked encoding final chunk.
inline
#if GENERATING_DOCS
implementation_defined
#else
boost::asio::const_buffers_1
#endif
chunk_encode_final()
{
    return boost::asio::const_buffers_1(
        "0\r\n\r\n", 5);
}

} // detail
} // http
} // beast

#endif
