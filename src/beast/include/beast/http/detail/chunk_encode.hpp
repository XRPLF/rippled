//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
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

class chunk_encode_delim
{
    boost::asio::const_buffer cb_;

    // Storage for the longest hex string we might need, plus delimiters.
    std::array<char, 2 * sizeof(std::size_t) + 2> buf_;

    template<class = void>
    void
    copy(chunk_encode_delim const& other);

    template<class = void>
    void
    setup(std::size_t n);

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

    chunk_encode_delim(chunk_encode_delim const& other)
    {
        copy(other);
    }

    explicit
    chunk_encode_delim(std::size_t n)
    {
        setup(n);
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

template<class>
void
chunk_encode_delim::
copy(chunk_encode_delim const& other)
{
    auto const n =
        boost::asio::buffer_size(other.cb_);
    buf_ = other.buf_;
    cb_ = boost::asio::const_buffer(
        &buf_[buf_.size() - n], n);
}

template<class>
void
chunk_encode_delim::
setup(std::size_t n)
{
    buf_[buf_.size() - 2] = '\r';
    buf_[buf_.size() - 1] = '\n';
    auto it = to_hex(buf_.end() - 2, n);
    cb_ = boost::asio::const_buffer{&*it,
        static_cast<std::size_t>(
            std::distance(it, buf_.end()))};
}

} // detail
} // http
} // beast

#endif
