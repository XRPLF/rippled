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

#ifndef BEAST_NUDB_DETAIL_STREAM_H_INCLUDED
#define BEAST_NUDB_DETAIL_STREAM_H_INCLUDED

#include <beast/nudb/common.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace beast {
namespace nudb {
namespace detail {

// Input stream from bytes
template <class = void>
class istream_t
{
private:
    std::uint8_t const* buf_;
    std::size_t size_ = 0;

public:
    istream_t (istream_t const&) = default;
    istream_t& operator= (istream_t const&) = default;

    istream_t (void const* data, std::size_t size)
        : buf_(reinterpret_cast<
            std::uint8_t const*>(data))
        , size_(size)
    {
    }

    template <std::size_t N>
    istream_t (std::array<std::uint8_t, N> const& a)
        : buf_ (a.data())
        , size_ (a.size())
    {
    }

    std::uint8_t const*
    data (std::size_t bytes);

    std::uint8_t const*
    operator()(std::size_t bytes)
    {
        return data(bytes);
    }
};

template <class _>
std::uint8_t const*
istream_t<_>::data (std::size_t bytes)
{
    if (size_ < bytes)
        throw short_read_error();
    auto const data = buf_;
    buf_ = buf_ + bytes;
    size_ -= bytes;
    return data;
}

using istream = istream_t<>;

//------------------------------------------------------------------------------

// Output stream to bytes
template <class = void>
class ostream_t
{
private:
    std::uint8_t* buf_;
    std::size_t size_ = 0;

public:
    ostream_t (ostream_t const&) = default;
    ostream_t& operator= (ostream_t const&) = default;

    ostream_t (void* data, std::size_t)
        : buf_ (reinterpret_cast<std::uint8_t*>(data))
    {
    }

    template <std::size_t N>
    ostream_t (std::array<std::uint8_t, N>& a)
        : buf_ (a.data())
    {
    }

    // Returns the number of bytes written
    std::size_t
    size() const
    {
        return size_;
    }

    std::uint8_t*
    data (std::size_t bytes);

    std::uint8_t*
    operator()(std::size_t bytes)
    {
        return data(bytes);
    }
};

template <class _>
std::uint8_t*
ostream_t<_>::data (std::size_t bytes)
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
read (istream& is,
    void* buffer, std::size_t bytes)
{
    std::memcpy (buffer, is.data(bytes), bytes);
}

// write blob
inline
void
write (ostream& os,
    void const* buffer, std::size_t bytes)
{
    std::memcpy (os.data(bytes), buffer, bytes);
}

} // detail
} // nudb
} // beast

#endif
