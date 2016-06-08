//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_ZEROCOPYSTREAM_H_INCLUDED
#define RIPPLE_OVERLAY_ZEROCOPYSTREAM_H_INCLUDED

#include <google/protobuf/io/zero_copy_stream.h>
#include <boost/asio/buffer.hpp>
#include <cstdint>

namespace ripple {

/** Implements ZeroCopyInputStream around a buffer sequence.
    @tparam Buffers A type meeting the requirements of ConstBufferSequence.
    @see https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.io.zero_copy_stream
*/
template <class Buffers>
class ZeroCopyInputStream
    : public ::google::protobuf::io::ZeroCopyInputStream
{
private:
    using iterator = typename Buffers::const_iterator;
    using const_buffer = boost::asio::const_buffer;

    google::protobuf::int64 count_ = 0;
    iterator last_;
    iterator first_;    // Where pos_ comes from
    const_buffer pos_;  // What Next() will return

public:
    explicit
    ZeroCopyInputStream (Buffers const& buffers);

    bool
    Next (const void** data, int* size) override;

    void
    BackUp (int count) override;

    bool
    Skip (int count) override;

    google::protobuf::int64
    ByteCount() const override
    {
        return count_;
    }
};

//------------------------------------------------------------------------------

template <class Buffers>
ZeroCopyInputStream<Buffers>::ZeroCopyInputStream (
        Buffers const& buffers)
    : last_ (buffers.end())
    , first_ (buffers.begin())
    , pos_ ((first_ != last_) ?
        *first_ : const_buffer(nullptr, 0))
{
}

template <class Buffers>
bool
ZeroCopyInputStream<Buffers>::Next (
    const void** data, int* size)
{
    *data = boost::asio::buffer_cast<void const*>(pos_);
    *size = boost::asio::buffer_size(pos_);
    if (first_ == last_)
        return false;
    count_ += *size;
    pos_ = (++first_ != last_) ? *first_ :
        const_buffer(nullptr, 0);
    return true;
}

template <class Buffers>
void
ZeroCopyInputStream<Buffers>::BackUp (int count)
{
    --first_;
    pos_ = *first_ +
        (boost::asio::buffer_size(*first_) - count);
    count_ -= count;
}

template <class Buffers>
bool
ZeroCopyInputStream<Buffers>::Skip (int count)
{
    if (first_ == last_)
        return false;
    while (count > 0)
    {
        auto const size =
            boost::asio::buffer_size(pos_);
        if (count < size)
        {
            pos_ = pos_ + count;
            count_ += count;
            return true;
        }
        count_ += size;
        if (++first_ == last_)
            return false;
        count -= size;
        pos_ = *first_;
    }
    return true;
}

//------------------------------------------------------------------------------

/** Implements ZeroCopyOutputStream around a Streambuf.
    Streambuf matches the public interface defined by boost::asio::streambuf.
    @tparam Streambuf A type meeting the requirements of Streambuf.
*/
template <class Streambuf>
class ZeroCopyOutputStream
    : public ::google::protobuf::io::ZeroCopyOutputStream
{
private:
    using buffers_type = typename Streambuf::mutable_buffers_type;
    using iterator = typename buffers_type::const_iterator;
    using mutable_buffer = boost::asio::mutable_buffer;

    Streambuf& streambuf_;
    std::size_t blockSize_;
    google::protobuf::int64 count_ = 0;
    std::size_t commit_ = 0;
    buffers_type buffers_;
    iterator pos_;

public:
    explicit
    ZeroCopyOutputStream (Streambuf& streambuf,
        std::size_t blockSize);

    ~ZeroCopyOutputStream();

    bool
    Next (void** data, int* size) override;

    void
    BackUp (int count) override;

    google::protobuf::int64
    ByteCount() const override
    {
        return count_;
    }
};

//------------------------------------------------------------------------------

template <class Streambuf>
ZeroCopyOutputStream<Streambuf>::ZeroCopyOutputStream(
        Streambuf& streambuf, std::size_t blockSize)
    : streambuf_ (streambuf)
    , blockSize_ (blockSize)
    , buffers_ (streambuf_.prepare(blockSize_))
    , pos_ (buffers_.begin())
{
}

template <class Streambuf>
ZeroCopyOutputStream<Streambuf>::~ZeroCopyOutputStream()
{
    if (commit_ != 0)
        streambuf_.commit(commit_);
}

template <class Streambuf>
bool
ZeroCopyOutputStream<Streambuf>::Next(
    void** data, int* size)
{
    if (commit_ != 0)
    {
        streambuf_.commit(commit_);
        count_ += commit_;
    }

    if (pos_ == buffers_.end())
    {
        buffers_ = streambuf_.prepare (blockSize_);
        pos_ = buffers_.begin();
    }

    *data = boost::asio::buffer_cast<void*>(*pos_);
    *size = boost::asio::buffer_size(*pos_);
    commit_ = *size;
    ++pos_;
    return true;
}

template <class Streambuf>
void
ZeroCopyOutputStream<Streambuf>::BackUp (int count)
{
    assert(count <= commit_);
    auto const n = commit_ - count;
    streambuf_.commit(n);
    count_ += n;
    commit_ = 0;
}

} // ripple

#endif
