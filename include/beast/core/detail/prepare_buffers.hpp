//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_PREPARED_BUFFERS_HPP
#define BEAST_DETAIL_PREPARED_BUFFERS_HPP

#include <beast/core/prepare_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace beast {
namespace detail {

/** A buffer sequence adapter that shortens the sequence size.

    The class adapts a buffer sequence to efficiently represent
    a shorter subset of the original list of buffers starting
    with the first byte of the original sequence.

    @tparam BufferSequence The buffer sequence to adapt.
*/
template<class BufferSequence>
class prepared_buffers
{
    using iter_type =
        typename BufferSequence::const_iterator;

    BufferSequence bs_;
    iter_type back_;
    iter_type end_;
    std::size_t size_;

    template<class Deduced>
    prepared_buffers(Deduced&& other,
            std::size_t nback, std::size_t nend)
        : bs_(std::forward<Deduced>(other).bs_)
        , back_(std::next(bs_.begin(), nback))
        , end_(std::next(bs_.begin(), nend))
        , size_(other.size_)
    {
    }

    void
    setup(std::size_t n);

public:
    /// The type for each element in the list of buffers.
    using value_type = typename std::conditional<
        std::is_convertible<typename
            std::iterator_traits<iter_type>::value_type,
                boost::asio::mutable_buffer>::value,
                    boost::asio::mutable_buffer,
                        boost::asio::const_buffer>::type;

#if GENERATING_DOCS
    /// A bidirectional iterator type that may be used to read elements.
    using const_iterator = implementation_defined;

#else
    class const_iterator;

#endif

    /// Move constructor.
    prepared_buffers(prepared_buffers&&);

    /// Copy constructor.
    prepared_buffers(prepared_buffers const&);

    /// Move assignment.
    prepared_buffers& operator=(prepared_buffers&&);

    /// Copy assignment.
    prepared_buffers& operator=(prepared_buffers const&);

    /** Construct a shortened buffer sequence.

        @param n The maximum number of bytes in the wrapped
        sequence. If this is larger than the size of passed,
        buffers, the resulting sequence will represent the
        entire input sequence.

        @param buffers The buffer sequence to adapt. A copy of
        the sequence will be made, but ownership of the underlying
        memory is not transferred.
    */
    prepared_buffers(std::size_t n, BufferSequence const& buffers);

    /// Get a bidirectional iterator to the first element.
    const_iterator
    begin() const;

    /// Get a bidirectional iterator to one past the last element.
    const_iterator
    end() const;
};

template<class BufferSequence>
class prepared_buffers<BufferSequence>::const_iterator
{
    friend class prepared_buffers<BufferSequence>;

    using iter_type =
        typename BufferSequence::const_iterator;

    prepared_buffers const* b_ = nullptr;
    typename BufferSequence::const_iterator it_;

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
prepared_buffers<BufferSequence>::const_iterator::
const_iterator(const_iterator&& other)
    : b_(other.b_)
    , it_(std::move(other.it_))
{
}

template<class BufferSequence>
prepared_buffers<BufferSequence>::const_iterator::
const_iterator(const_iterator const& other)
    : b_(other.b_)
    , it_(other.it_)
{
}

template<class BufferSequence>
auto
prepared_buffers<BufferSequence>::const_iterator::
operator=(const_iterator&& other) ->
    const_iterator&
{
    b_ = other.b_;
    it_ = std::move(other.it_);
    return *this;
}

template<class BufferSequence>
auto
prepared_buffers<BufferSequence>::const_iterator::
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
inline
auto
prepared_buffers<BufferSequence>::begin() const ->
    const_iterator
{
    return const_iterator{*this, false};
}

template<class BufferSequence>
inline
auto
prepared_buffers<BufferSequence>::end() const ->
    const_iterator
{
    return const_iterator{*this, true};
}

} // detail
} // beast

#endif
