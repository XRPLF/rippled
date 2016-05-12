//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_CONSUMING_BUFFERS_IPP
#define BEAST_IMPL_CONSUMING_BUFFERS_IPP

#include <beast/core/buffer_concepts.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

namespace beast {

template<class BufferSequence, class ValueType>
class consuming_buffers<BufferSequence, ValueType>::const_iterator
{
    friend class consuming_buffers<BufferSequence, ValueType>;

    using iter_type =
        typename BufferSequence::const_iterator;

    iter_type it_;
    consuming_buffers const* b_ = nullptr;

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
        return it_ == b_->begin_ ?
            *it_ + b_->skip_ : *it_;
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
            iter_type it)
        : it_(it)
        , b_(&b)
    {
    }
};

template<class BufferSequence, class ValueType>
consuming_buffers<BufferSequence, ValueType>::
consuming_buffers(consuming_buffers&& other)
    : consuming_buffers(std::move(other),
        std::distance<iter_type>(
            other.bs_.begin(), other.begin_))
{
}

template<class BufferSequence, class ValueType>
consuming_buffers<BufferSequence, ValueType>::
consuming_buffers(consuming_buffers const& other)
    : consuming_buffers(other,
        std::distance<iter_type>(
            other.bs_.begin(), other.begin_))
{
}

template<class BufferSequence, class ValueType>
auto
consuming_buffers<BufferSequence, ValueType>::
operator=(consuming_buffers&& other) ->
    consuming_buffers&
{
    auto const nbegin = std::distance<iter_type>(
        other.bs_.begin(), other.begin_);
    bs_ = std::move(other.bs_);
    begin_ = std::next(bs_.begin(), nbegin);
    skip_ = other.skip_;
    return *this;
}

template<class BufferSequence, class ValueType>
auto
consuming_buffers<BufferSequence, ValueType>::
operator=(consuming_buffers const& other) ->
    consuming_buffers&
{
    auto const nbegin = std::distance<iter_type>(
        other.bs_.begin(), other.begin_);
    bs_ = other.bs_;
    begin_ = std::next(bs_.begin(), nbegin);
    skip_ = other.skip_;
    return *this;
}

template<class BufferSequence, class ValueType>
consuming_buffers<BufferSequence, ValueType>::
consuming_buffers(BufferSequence const& bs)
    : bs_(bs)
    , begin_(bs_.begin())
{
    static_assert(is_BufferSequence<BufferSequence, ValueType>::value,
        "BufferSequence requirements not met");
}

template<class BufferSequence, class ValueType>
auto
consuming_buffers<BufferSequence, ValueType>::begin() const ->
    const_iterator
{
    return const_iterator{*this, begin_};
}

template<class BufferSequence, class ValueType>
auto
consuming_buffers<BufferSequence, ValueType>::end() const ->
    const_iterator
{
    return const_iterator{*this, bs_.end()};
}

template<class BufferSequence, class ValueType>
void
consuming_buffers<BufferSequence, ValueType>::consume(std::size_t n)
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

template<class BufferSequence>
consuming_buffers<BufferSequence, typename BufferSequence::value_type>
consumed_buffers(BufferSequence const& bs, std::size_t n)
{
    consuming_buffers<BufferSequence> cb(bs);
    cb.consume(n);
    return cb;
}

} // beast

#endif
