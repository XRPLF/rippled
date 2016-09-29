//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_BULKIO_HPP
#define NUDB_DETAIL_BULKIO_HPP

#include <nudb/type_traits.hpp>
#include <nudb/detail/buffer.hpp>
#include <nudb/detail/stream.hpp>
#include <nudb/error.hpp>
#include <algorithm>
#include <cstddef>

namespace nudb {
namespace detail {

// Scans a file in sequential large reads
template<class File>
class bulk_reader
{
    File& f_;
    buffer buf_;
    noff_t last_;       // size of file
    noff_t offset_;     // current position
    std::size_t avail_; // bytes left to read in buf
    std::size_t used_;  // bytes consumed in buf

public:
    bulk_reader(File& f, noff_t offset,
        noff_t last, std::size_t buffer_size);

    noff_t
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
    prepare(std::size_t needed, error_code& ec);
};

template<class File>
bulk_reader<File>::
bulk_reader(File& f, noff_t offset,
        noff_t last, std::size_t buffer_size)
    : f_(f)
    , last_(last)
    , offset_(offset)
    , avail_(0)
    , used_(0)
{
    buf_.reserve(buffer_size);
}

template<class File>
istream
bulk_reader<File>::
prepare(std::size_t needed, error_code& ec)
{
    if(needed > avail_)
    {
        if(offset_ + needed - avail_ > last_)
        {
            ec = error::short_read;
            return {};
        }
        if(needed > buf_.size())
        {
            buffer buf;
            buf.reserve(needed);
            std::memcpy(buf.get(),
                buf_.get() + used_, avail_);
            buf_ = std::move(buf);
        }
        else
        {
            std::memmove(buf_.get(),
                buf_.get() + used_, avail_);
        }

        auto const n = std::min(buf_.size() - avail_,
            static_cast<std::size_t>(last_ - offset_));
        f_.read(offset_, buf_.get() + avail_, n, ec);
        if(ec)
            return {};
        offset_ += n;
        avail_ += n;
        used_ = 0;
    }
    istream is{buf_.get() + used_, needed};
    used_ += needed;
    avail_ -= needed;
    return is;
}

//------------------------------------------------------------------------------

// Buffers file writes
// Caller must call flush manually at the end
template<class File>
class bulk_writer
{
    File& f_;
    buffer buf_;
    noff_t offset_;      // current position
    std::size_t used_;   // bytes written to buf

public:
    bulk_writer(File& f, noff_t offset,
        std::size_t buffer_size);

    ostream
    prepare(std::size_t needed, error_code& ec);

    // Returns the number of bytes buffered
    std::size_t
    size()
    {
        return used_;
    }

    // Return current offset in file. This
    // is advanced with each call to prepare.
    noff_t
    offset() const
    {
        return offset_ + used_;
    }

    // Caller must invoke flush manually in
    // order to handle any error conditions.
    void
    flush(error_code& ec);
};

template<class File>
bulk_writer<File>::
bulk_writer(File& f,
        noff_t offset, std::size_t buffer_size)
    : f_(f)
    , offset_(offset)
    , used_(0)

{
    buf_.reserve(buffer_size);
}

template<class File>
ostream
bulk_writer<File>::
prepare(std::size_t needed, error_code& ec)
{
    if(used_ + needed > buf_.size())
    {
        flush(ec);
        if(ec)
            return{};
    }
    if(needed > buf_.size())
        buf_.reserve(needed);
    ostream os(buf_.get() + used_, needed);
    used_ += needed;
    return os;
}

template<class File>
void
bulk_writer<File>::
flush(error_code& ec)
{
    if(used_)
    {
        auto const offset = offset_;
        auto const used = used_;
        offset_ += used_;
        used_ = 0;
        f_.write(offset, buf_.get(), used, ec);
        if(ec)
            return;
    }
}

} // detail
} // nudb

#endif
