//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_BUFFER_HPP
#define NUDB_DETAIL_BUFFER_HPP

#include <atomic>
#include <cstdint>
#include <memory>

namespace nudb {
namespace detail {

// Simple growable memory buffer
class buffer
{
private:
    std::size_t size_ = 0;
    std::unique_ptr<std::uint8_t[]> buf_;

public:
    ~buffer() = default;
    buffer() = default;
    buffer(buffer const&) = delete;
    buffer& operator=(buffer const&) = delete;

    explicit
    buffer(std::size_t n)
        : size_(n)
        , buf_(new std::uint8_t[n])
    {
    }

    buffer(buffer&& other)
        : size_(other.size_)
        , buf_(std::move(other.buf_))
    {
        other.size_ = 0;
    }

    buffer&
    operator=(buffer&& other)
    {
        size_ = other.size_;
        buf_ = std::move(other.buf_);
        other.size_ = 0;
        return *this;
    }

    std::size_t
    size() const
    {
        return size_;
    }

    std::uint8_t*
    get() const
    {
        return buf_.get();
    }

    void
    reserve(std::size_t n)
    {
        if(size_ < n)
            buf_.reset(new std::uint8_t[n]);
        size_ = n;
    }

    // BufferFactory
    void*
    operator()(std::size_t n)
    {
        reserve(n);
        return buf_.get();
    }
};

} // detail
} // nudb

#endif
