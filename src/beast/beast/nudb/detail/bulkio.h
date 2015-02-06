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

#ifndef BEAST_NUDB_DETAIL_BULKIO_H_INCLUDED
#define BEAST_NUDB_DETAIL_BULKIO_H_INCLUDED

#include <beast/nudb/detail/buffer.h>
#include <beast/nudb/detail/stream.h>
#include <algorithm>
#include <cstddef>

namespace beast {
namespace nudb {
namespace detail {

// Scans a file in sequential large reads
template <class File>
class bulk_reader
{
private:
    File& f_;
    buffer buf_;
    std::size_t last_;      // size of file
    std::size_t offset_;    // current position
    std::size_t avail_;     // bytes left to read in buf
    std::size_t used_;      // bytes consumed in buf

public:
    bulk_reader (File& f, std::size_t offset,
        std::size_t last, std::size_t buffer_size);

    std::size_t
    offset() const
    {
        return offset_ - avail_;
    }

    bool
    eof() const
    {
        return offset() >= last_;
    }

    istream
    prepare (std::size_t needed);
};

template <class File>
bulk_reader<File>::bulk_reader (File& f, std::size_t offset,
    std::size_t last, std::size_t buffer_size)
    : f_ (f)
    , last_ (last)
    , offset_ (offset)
    , avail_ (0)
    , used_ (0)
{
    buf_.reserve (buffer_size);
}


template <class File>
istream
bulk_reader<File>::prepare (std::size_t needed)
{
    if (needed > avail_)
    {
        if (offset_ + needed - avail_ > last_)
            throw file_short_read_error();
        if (needed > buf_.size())
        {
            buffer buf;
            buf.reserve (needed);
            std::memcpy (buf.get(),
                buf_.get() + used_, avail_);
            buf_ = std::move(buf);
        }
        else
        {
            std::memmove (buf_.get(),
                buf_.get() + used_, avail_);
        }

        auto const n = std::min(
            buf_.size() - avail_, last_ - offset_);
        f_.read(offset_, buf_.get() + avail_, n);
        offset_ += n;
        avail_ += n;
        used_ = 0;
    }
    istream is(buf_.get() + used_, needed);
    used_ += needed;
    avail_ -= needed;
    return is;
}

//------------------------------------------------------------------------------

// Buffers file writes
// Caller must call flush manually at the end
template <class File>
class bulk_writer
{
private:
    File& f_;
    buffer buf_;
    std::size_t offset_;    // current position
    std::size_t used_;      // bytes written to buf

public:
    bulk_writer (File& f, std::size_t offset,
        std::size_t buffer_size);

    ostream
    prepare (std::size_t needed);

    // Returns the number of bytes buffered
    std::size_t
    size()
    {
        return used_;
    }

    // Return current offset in file. This
    // is advanced with each call to prepare.
    std::size_t
    offset() const
    {
        return offset_ + used_;
    }

    // flush cannot be called from the destructor
    // since it can throw, so callers must do it manually.
    void
    flush();
};

template <class File>
bulk_writer<File>::bulk_writer (File& f,
        std::size_t offset, std::size_t buffer_size)
    : f_ (f)
    , offset_ (offset)
    , used_ (0)

{
    buf_.reserve (buffer_size);
}

template <class File>
ostream
bulk_writer<File>::prepare (std::size_t needed)
{
    if (used_ + needed > buf_.size())
        flush();
    if (needed > buf_.size())
        buf_.reserve (needed);
    ostream os (buf_.get() + used_, needed);
    used_ += needed;
    return os;
}

template <class File>
void
bulk_writer<File>::flush()
{
    if (used_)
    {
        auto const offset = offset_;
        auto const used = used_;
        offset_ += used_;
        used_ = 0;
        f_.write (offset, buf_.get(), used);
    }
}

} // detail
} // nudb
} // beast

#endif
