//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_ASIO_CONSUMING_BUFFERS_H_INLUDED
#define BEAST_ASIO_CONSUMING_BUFFERS_H_INLUDED

#include <beast/asio/type_check.h>
#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

namespace beast {

/// Adapter for excluding bytes at the beginning of a BufferSequence.
/**
    This adapter wraps a BufferSequence so it can be consumed. Bytes
    consumed are removed from the front of the buffer. The underlying
    buffer object is not modified. Instead, the adapter presents a
    new buffer sequence which excludes zero or more bytes from the
    beginning of the underlying buffers.
*/
template<class Buffers,
    class Buffer = typename Buffers::value_type>
class consuming_buffers
{
    using iter_type =
        typename Buffers::const_iterator;

    static_assert(std::is_constructible<Buffer,
        typename std::iterator_traits<iter_type>::value_type>::value,
            "Buffer requirements not met");

    Buffers bs_;
    iter_type begin_;
    std::size_t skip_ = 0;

public:
    using value_type = Buffer;

    class const_iterator;

    consuming_buffers(consuming_buffers&&) = default;
    consuming_buffers(consuming_buffers const&) = default;
    consuming_buffers& operator=(consuming_buffers&&) = default;
    consuming_buffers& operator=(consuming_buffers const&) = default;

    explicit
    consuming_buffers(Buffers const& bs);

    const_iterator
    begin() const;

    const_iterator
    end() const;

    /** Remove bytes from the beginning of the buffers

        @param n The number of bytes to remove. If this is
        larger than the number of bytes remaining, all the
        bytes remaining are removed.
    */
    void
    consume(std::size_t n);
};

template<class Buffers, class Buffer>
class consuming_buffers<Buffers, Buffer>::const_iterator
{
    friend class consuming_buffers<Buffers, Buffer>;

    using iter_type =
        typename Buffers::const_iterator;

    iter_type it_;
    consuming_buffers const* b_;

public:
    using value_type =
        typename std::iterator_traits<iter_type>::value_type;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other) = default;
    const_iterator(const_iterator const& other) = default;
    const_iterator& operator=(const_iterator&& other) = default;
    const_iterator& operator=(const_iterator const& other) = default;

    bool
    operator==(const_iterator const& other) const
    {
        return b_ == other.b_ && it_ == other.it_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        if(it_ == b_->begin_)
            return *it_ + b_->skip_;
        return *it_;
    }

    pointer
    operator->() const = delete;

    const_iterator&
    operator++()
    {
        ++it_;
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--()
    {
        --it_;
        return *this;
    }

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }

private:
    const_iterator(consuming_buffers const& b,
            typename Buffers::const_iterator it)
        : it_(it)
        , b_(&b)
    {
    }
};

template<class Buffers, class Buffer>
consuming_buffers<Buffers, Buffer>::consuming_buffers(
        Buffers const& bs)
    : bs_(bs)
    , begin_(bs_.begin())
{
}

template<class Buffers, class Buffer>
auto
consuming_buffers<Buffers, Buffer>::begin() const ->
    const_iterator
{
    return const_iterator{*this, begin_};
}
    
template<class Buffers, class Buffer>
auto
consuming_buffers<Buffers, Buffer>::end() const ->
    const_iterator
{
    return const_iterator{*this, bs_.end()};
}

template<class Buffers, class Buffer>
void
consuming_buffers<Buffers, Buffer>::consume(std::size_t n)
{
    using boost::asio::buffer_size;
    for(;n > 0 && begin_ != bs_.end(); ++begin_)
    {
        auto const len =
            buffer_size(*begin_) - skip_;
        if(n < len)
        {
            skip_ += n;
            break;
        }
        n -= len;
        skip_ = 0;
    }
}

/// Returns a consumed buffer
template<class Buffers>
consuming_buffers<Buffers, typename Buffers::value_type>
consumed_buffers(Buffers const& bs, std::size_t n)
{
    consuming_buffers<Buffers> cb(bs);
    cb.consume(n);
    return cb;
}

} // beast

#endif
