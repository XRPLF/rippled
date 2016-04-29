//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_PREPARE_BUFFERS_IPP
#define BEAST_IMPL_PREPARE_BUFFERS_IPP

#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace beast {

template<class BufferSequence>
void
prepared_buffers<BufferSequence>::
setup(std::size_t n)
{
    for(end_ = bs_.begin(); end_ != bs_.end(); ++end_)
    {
        auto const len =
            boost::asio::buffer_size(*end_);
        if(n <= len)
        {
            size_ = n;
            back_ = end_++;
            return;
        }
        n -= len;
    }
    size_ = 0;
    back_ = end_;
}

template<class BufferSequence>
class prepared_buffers<BufferSequence>::const_iterator
{
    friend class prepared_buffers<BufferSequence>;

    using iter_type =
        typename BufferSequence::const_iterator;

    prepared_buffers const* b_ = nullptr;
    typename BufferSequence::const_iterator it_;

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
        if(it_ == b_->back_)
            return prepare_buffer(b_->size_, *it_);
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
    const_iterator(prepared_buffers const& b,
            bool at_end)
        : b_(&b)
        , it_(at_end ? b.end_ : b.bs_.begin())
    {
    }
};

template<class BufferSequence>
prepared_buffers<BufferSequence>::
prepared_buffers(prepared_buffers&& other)
    : prepared_buffers(std::move(other),
        std::distance<iter_type>(other.bs_.begin(), other.back_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class BufferSequence>
prepared_buffers<BufferSequence>::
prepared_buffers(prepared_buffers const& other)
    : prepared_buffers(other,
        std::distance<iter_type>(other.bs_.begin(), other.back_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class BufferSequence>
auto
prepared_buffers<BufferSequence>::
operator=(prepared_buffers&& other) ->
    prepared_buffers&
{
    auto const nback = std::distance<iter_type>(
        other.bs_.begin(), other.back_);
    auto const nend = std::distance<iter_type>(
        other.bs_.begin(), other.end_);
    bs_ = std::move(other.bs_);
    back_ = std::next(bs_.begin(), nback);
    end_ = std::next(bs_.begin(), nend);
    size_ = other.size_;
    return *this;
}

template<class BufferSequence>
auto
prepared_buffers<BufferSequence>::
operator=(prepared_buffers const& other) ->
    prepared_buffers&
{
    auto const nback = std::distance<iter_type>(
        other.bs_.begin(), other.back_);
    auto const nend = std::distance<iter_type>(
        other.bs_.begin(), other.end_);
    bs_ = other.bs_;
    back_ = std::next(bs_.begin(), nback);
    end_ = std::next(bs_.begin(), nend);
    size_ = other.size_;
    return *this;
}

template<class BufferSequence>
prepared_buffers<BufferSequence>::
prepared_buffers(std::size_t n, BufferSequence const& bs)
    : bs_(bs)
{
    setup(n);
}

template<class BufferSequence>
auto
prepared_buffers<BufferSequence>::begin() const ->
    const_iterator
{
    return const_iterator{*this, false};
}

template<class BufferSequence>
auto
prepared_buffers<BufferSequence>::end() const ->
    const_iterator
{
    return const_iterator{*this, true};
}

template<class BufferSequence>
inline
prepared_buffers<BufferSequence>
prepare_buffers(std::size_t n, BufferSequence const& buffers)
{
    return prepared_buffers<BufferSequence>(n, buffers);
}

} // beast

#endif
