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

#ifndef BEAST_NUDB_STREAM_H_INCLUDED
#define BEAST_NUDB_STREAM_H_INCLUDED

#include <beast/nudb/error.h>
#include <beast/nudb/detail/config.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
};

//------------------------------------------------------------------------------

// Input stream from bytes
template <class = void>
class istream_t
{
private:
    std::uint8_t const* buf_;
#if ! BEAST_NUDB_NO_DOMAIN_CHECK
    std::size_t bytes_;
#endif

public:
    istream_t (istream_t const&) = default;
    istream_t& operator= (istream_t const&) = default;

    istream_t (void const* data, std::size_t
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        bytes
    #endif
        )
        : buf_(reinterpret_cast<
            std::uint8_t const*>(data))
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        , bytes_(bytes)
    #endif
    {
    }

    template <std::size_t N>
    istream_t (std::array<std::uint8_t, N> const& a)
        : buf_ (a.data())
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        , bytes_ (a.size())
    #endif
    {
    }

    std::uint8_t const*
    data (std::size_t bytes)
    {
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        if (bytes > bytes_)
            throw std::logic_error(
                "nudb: istream");
        bytes_ -= bytes;
    #endif
        auto const data = buf_;
        buf_ = buf_ + bytes;
        return data;
    }
};

using istream = istream_t<>;

//------------------------------------------------------------------------------

// Output stream to bytes
template <class = void>
class ostream_t
{
private:
    std::uint8_t* buf_;
    std::size_t size_ = 0;
#if ! BEAST_NUDB_NO_DOMAIN_CHECK
    std::size_t bytes_;
#endif

public:
    ostream_t (ostream_t const&) = default;
    ostream_t& operator= (ostream_t const&) = default;

    ostream_t (void* data, std::size_t
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        bytes
    #endif
        )
        : buf_ (reinterpret_cast<std::uint8_t*>(data))
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        , bytes_ (bytes)
    #endif
    {
    }

    template <std::size_t N>
    ostream_t (std::array<std::uint8_t, N>& a)
        : buf_ (a.data())
    #if ! BEAST_NUDB_NO_DOMAIN_CHECK
        , bytes_ (a.size())
    #endif
    {
    }

    // Returns the number of bytes written
    std::size_t
    size() const
    {
        return size_;
    }

    std::uint8_t*
    data (std::size_t bytes)
    {
#if ! BEAST_NUDB_NO_DOMAIN_CHECK
        if (bytes > bytes_)
            throw std::logic_error(
                "nudb: ostream");
        bytes_ -= bytes;
#endif
        auto const data = buf_;
        buf_ = buf_ + bytes;
        size_ += bytes;
        return data;
    }
};

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
