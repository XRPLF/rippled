//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_DETAIL_BUFFER_H_INCLUDED
#define BEAST_NUDB_DETAIL_BUFFER_H_INCLUDED

#include <atomic>
#include <cstdint>
#include <memory>

namespace beast {
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
    buffer (buffer const&) = delete;
    buffer& operator= (buffer const&) = delete;

    explicit
    buffer (std::size_t n)
        : size_ (n)
        , buf_ (new std::uint8_t[n])
    {
    }

    buffer (buffer&& other)
        : size_ (other.size_)
        , buf_ (std::move(other.buf_))
    {
        other.size_ = 0;
    }

    buffer& operator= (buffer&& other)
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
    reserve (std::size_t n)
    {
        if (size_ < n)
            buf_.reset (new std::uint8_t[n]);
        size_ = n;
    }

    // BufferFactory
    void*
    operator() (std::size_t n)
    {
        reserve(n);
        return buf_.get();
    }
};

} // detail
} // nudb
} // beast

#endif
