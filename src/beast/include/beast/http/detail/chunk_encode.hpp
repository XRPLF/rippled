//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_DETAIL_CHUNK_ENCODE_HPP
#define BEAST_HTTP_DETAIL_CHUNK_ENCODE_HPP

#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <array>
#include <cstddef>

namespace beast {
namespace http {
namespace detail {

/** A buffer sequence containing a chunk-encoding header
*/
class chunk_header
{
    boost::asio::const_buffer cb_;

    // Storage for the longest hex string we might need
    char buf_[2 * sizeof(std::size_t)];

    template<class = void>
    void
    copy(chunk_header const& other);

    template<class = void>
    void
    prepare_impl(std::size_t n);

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

    /** Constructor (default)

        Default-constructed chunk headers are in an
        undefined state.
    */
    chunk_header() = default;

    /// Copy constructor
    chunk_header(chunk_header const& other)
    {
        copy(other);
    }

    /** Construct a chunk header

        @param n The number of octets in this chunk.
    */
    explicit
    chunk_header(std::size_t n)
    {
        prepare_impl(n);
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

    void
    prepare(std::size_t n)
    {
        prepare_impl(n);
    }
};

template<class>
void
chunk_header::
copy(chunk_header const& other)
{
    using boost::asio::buffer_copy;
    auto const n =
        boost::asio::buffer_size(other.cb_);
    auto const mb = boost::asio::mutable_buffers_1(
        &buf_[sizeof(buf_) - n], n);
    cb_ = *mb.begin();
    buffer_copy(mb,
        boost::asio::const_buffers_1(other.cb_));
}

template<class>
void
chunk_header::
prepare_impl(std::size_t n)
{
    auto const end = &buf_[sizeof(buf_)];
    auto it = to_hex(end, n);
    cb_ = boost::asio::const_buffer{&*it,
        static_cast<std::size_t>(
            std::distance(it, end))};
}

/// Returns a buffer sequence holding a CRLF for chunk encoding
inline
boost::asio::const_buffers_1
chunk_crlf()
{
    return {"\r\n", 2};
}

/// Returns a buffer sequence holding a final chunk header
inline
boost::asio::const_buffers_1
chunk_final()
{
    return {"0\r\n", 3};
}

} // detail
} // http
} // beast

#endif
