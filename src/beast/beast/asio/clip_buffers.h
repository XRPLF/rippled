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

#ifndef BEAST_ASIO_CLIP_BUFFERS_H_INLUDED
#define BEAST_ASIO_CLIP_BUFFERS_H_INLUDED

#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace beast {
namespace asio {

namespace detail {

inline
boost::asio::const_buffer
clip_buffer(std::size_t n, boost::asio::const_buffer b)
{
    using namespace boost::asio;
    return { buffer_cast<void const*>(b),
        std::min(n, buffer_size(b)) };
}

inline
boost::asio::mutable_buffer
clip_buffer(std::size_t n, boost::asio::mutable_buffer b)
{
    using namespace boost::asio;
    return { buffer_cast<void*>(b),
        std::min(n, buffer_size(b)) };
}

template<class BufferSequence>
class clip_buffers_helper
{
    using iter_type =
        typename BufferSequence::const_iterator;

    using value_type =
        typename std::iterator_traits<iter_type>::value_type;

    BufferSequence bs_;
    iter_type back_;
    iter_type end_;
    value_type back_value_;

public:
    class const_iterator;

    clip_buffers_helper(clip_buffers_helper&&) = default;
    clip_buffers_helper(clip_buffers_helper const&) = default;
    clip_buffers_helper& operator=(clip_buffers_helper&&) = default;
    clip_buffers_helper& operator=(clip_buffers_helper const&) = default;

    template<class DeducedBufferSequence>
    clip_buffers_helper(std::size_t n,
            DeducedBufferSequence&& bs)
        : bs_(std::forward<DeducedBufferSequence>(bs))
    {
        setup(n);
    }

    const_iterator
    begin() const;

    const_iterator
    end() const;

private:
    void
    setup(std::size_t n)
    {
        using namespace boost::asio;
        auto it = bs_.begin();
        if(it == bs_.end() || n == 0)
        {
            end_ = it;
            return;
        }
        for(;n > 0 && it != bs_.end(); ++it)
        {
            auto const len = buffer_size(*it);
            if(len >= n)
            {
                back_value_ = clip_buffer(n, *it);
                back_ = it;
                end_ = ++it;
                return;
            }
            n -= len;
        }
        end_ = bs_.end();
        back_ = std::prev(end_);
        back_value_ = *back_;
    }
};

template<class BufferSequence>
class clip_buffers_helper<BufferSequence>::const_iterator
{
    friend class clip_buffers_helper<BufferSequence>;

    using iter_type =
        typename BufferSequence::const_iterator;

    clip_buffers_helper const* b_;
    typename BufferSequence::const_iterator it_;

public:
    using value_type =
        typename std::iterator_traits<iter_type>::value_type;
    using pointer = value_type const*;
    using reference = value_type const&;
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
        if(it_ == b_->back_)
            return b_->back_value_;
        return *it_;
    }

    pointer
    operator->() const
    {
        return &**this;
    }

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
    const_iterator(clip_buffers_helper const& b,
            bool at_end)
        : b_(&b)
        , it_(at_end ? b.end_ : b.bs_.begin())
    {
    }
};
    
template<class BufferSequence>
auto
clip_buffers_helper<BufferSequence>::begin() const ->
    const_iterator
{
    return const_iterator{*this, false};
}
    
template<class BufferSequence>
auto
clip_buffers_helper<BufferSequence>::end() const ->
    const_iterator
{
    return const_iterator{*this, true};
}

} // detail

//------------------------------------------------------------------------------

/// Returns a BufferSequence which is the front subset of the passed buffers
/**
    @param n The maximum number of bytes in the returned buffer. If this is
    larger than buffer_size(buffers), the buffers returned will be the same
    size as the buffers passed.

    @param buffers The buffer sequence to clip. The buffer sequence may
    be copied or moved as necessary.
*/
template<class BufferSequence>
auto
clip_buffers(std::size_t n, BufferSequence&& buffers)
{
    return detail::clip_buffers_helper<
        std::decay_t<BufferSequence>>(n,
            std::forward<BufferSequence>(buffers));
}

} // asio
} // beast

#endif
