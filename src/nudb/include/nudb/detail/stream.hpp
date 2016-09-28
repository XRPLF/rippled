//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_STREAM_HPP
#define NUDB_DETAIL_STREAM_HPP

#include <boost/assert.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace nudb {
namespace detail {

// Input stream from bytes
template<class = void>
class istream_t
{
    std::uint8_t const* buf_ = nullptr;
    std::size_t size_ = 0;

public:
    istream_t() = default;
    istream_t(istream_t const&) = default;
    istream_t& operator=(istream_t const&) = default;

    istream_t(void const* data, std::size_t size)
        : buf_(reinterpret_cast<std::uint8_t const*>(data))
        , size_(size)
    {
    }

    template<std::size_t N>
    istream_t(std::array<std::uint8_t, N> const& a)
        : buf_(a.data())
        , size_(a.size())
    {
    }

    std::uint8_t const*
    data(std::size_t bytes);

    std::uint8_t const*
    operator()(std::size_t bytes)
    {
        return data(bytes);
    }
};

// Precondition: bytes <= size_
//
template<class _>
std::uint8_t const*
istream_t<_>::data(std::size_t bytes)
{
    BOOST_ASSERT(bytes <= size_);
    if(size_ < bytes)
        throw std::logic_error("short read from istream");
    auto const data = buf_;
    buf_ = buf_ + bytes;
    size_ -= bytes;
    return data;
}

using istream = istream_t<>;

//------------------------------------------------------------------------------

// Output stream to bytes
// VFALCO Should this assert on overwriting the buffer?
template<class = void>
class ostream_t
{
    std::uint8_t* buf_ = nullptr;
    std::size_t size_ = 0;

public:
    ostream_t() = default;
    ostream_t(ostream_t const&) = default;
    ostream_t& operator=(ostream_t const&) = default;

    ostream_t(void* data, std::size_t)
        : buf_(reinterpret_cast<std::uint8_t*>(data))
    {
    }

    template<std::size_t N>
    ostream_t(std::array<std::uint8_t, N>& a)
        : buf_(a.data())
    {
    }

    // Returns the number of bytes written
    std::size_t
    size() const
    {
        return size_;
    }

    std::uint8_t*
    data(std::size_t bytes);

    std::uint8_t*
    operator()(std::size_t bytes)
    {
        return data(bytes);
    }
};

template<class _>
std::uint8_t*
ostream_t<_>::data(std::size_t bytes)
{
    auto const data = buf_;
    buf_ = buf_ + bytes;
    size_ += bytes;
    return data;
}

using ostream = ostream_t<>;

//------------------------------------------------------------------------------

// read blob
inline
void
read(istream& is, void* buffer, std::size_t bytes)
{
    std::memcpy(buffer, is.data(bytes), bytes);
}

// write blob
inline
void
write(ostream& os, void const* buffer, std::size_t bytes)
{
    std::memcpy(os.data(bytes), buffer, bytes);
}

} // detail
} // nudb

#endif
