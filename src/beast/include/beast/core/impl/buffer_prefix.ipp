//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_BUFFER_PREFIX_IPP
#define BEAST_IMPL_BUFFER_PREFIX_IPP

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace beast {

namespace detail {

inline
boost::asio::const_buffer
buffer_prefix(std::size_t n,
    boost::asio::const_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void const*>(buffer),
        (std::min)(n, buffer_size(buffer)) };
}

inline
boost::asio::mutable_buffer
buffer_prefix(std::size_t n,
    boost::asio::mutable_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void*>(buffer),
        (std::min)(n, buffer_size(buffer)) };
}

} // detail

template<class BufferSequence>
class buffer_prefix_view<BufferSequence>::const_iterator
{
    friend class buffer_prefix_view<BufferSequence>;

    buffer_prefix_view const* b_ = nullptr;
    iter_type it_;

public:
    using value_type = typename std::conditional<
        std::is_convertible<typename
            std::iterator_traits<iter_type>::value_type,
                boost::asio::mutable_buffer>::value,
                    boost::asio::mutable_buffer,
                        boost::asio::const_buffer>::type;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other);
    const_iterator(const_iterator const& other);
    const_iterator& operator=(const_iterator&& other);
    const_iterator& operator=(const_iterator const& other);

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
            return detail::buffer_prefix(b_->size_, *it_);
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
    const_iterator(buffer_prefix_view const& b,
            bool at_end)
        : b_(&b)
        , it_(at_end ? b.end_ : b.bs_.begin())
    {
    }
};

template<class BufferSequence>
void
buffer_prefix_view<BufferSequence>::
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
buffer_prefix_view<BufferSequence>::
const_iterator::
const_iterator(const_iterator&& other)
    : b_(other.b_)
    , it_(std::move(other.it_))
{
}

template<class BufferSequence>
buffer_prefix_view<BufferSequence>::
const_iterator::
const_iterator(const_iterator const& other)
    : b_(other.b_)
    , it_(other.it_)
{
}

template<class BufferSequence>
auto
buffer_prefix_view<BufferSequence>::
const_iterator::
operator=(const_iterator&& other) ->
    const_iterator&
{
    b_ = other.b_;
    it_ = std::move(other.it_);
    return *this;
}

template<class BufferSequence>
auto
buffer_prefix_view<BufferSequence>::
const_iterator::
operator=(const_iterator const& other) ->
    const_iterator&
{
    if(&other == this)
        return *this;
    b_ = other.b_;
    it_ = other.it_;
    return *this;
}

template<class BufferSequence>
buffer_prefix_view<BufferSequence>::
buffer_prefix_view(buffer_prefix_view&& other)
    : buffer_prefix_view(std::move(other),
        std::distance<iter_type>(other.bs_.begin(), other.back_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class BufferSequence>
buffer_prefix_view<BufferSequence>::
buffer_prefix_view(buffer_prefix_view const& other)
    : buffer_prefix_view(other,
        std::distance<iter_type>(other.bs_.begin(), other.back_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class BufferSequence>
auto
buffer_prefix_view<BufferSequence>::
operator=(buffer_prefix_view&& other) ->
    buffer_prefix_view&
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
buffer_prefix_view<BufferSequence>::
operator=(buffer_prefix_view const& other) ->
    buffer_prefix_view&
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
buffer_prefix_view<BufferSequence>::
buffer_prefix_view(std::size_t n, BufferSequence const& bs)
    : bs_(bs)
{
    setup(n);
}

template<class BufferSequence>
template<class... Args>
buffer_prefix_view<BufferSequence>::
buffer_prefix_view(std::size_t n,
        boost::in_place_init_t, Args&&... args)
    : bs_(std::forward<Args>(args)...)
{
    setup(n);
}

template<class BufferSequence>
inline
auto
buffer_prefix_view<BufferSequence>::begin() const ->
    const_iterator
{
    return const_iterator{*this, false};
}

template<class BufferSequence>
inline
auto
buffer_prefix_view<BufferSequence>::end() const ->
    const_iterator
{
    return const_iterator{*this, true};
}

} // beast

#endif
