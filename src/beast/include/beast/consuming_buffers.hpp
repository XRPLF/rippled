//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_CONSUMING_BUFFERS_HPP
#define BEAST_CONSUMING_BUFFERS_HPP

#include <beast/type_check.hpp>
#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

namespace beast {

/** Adapter to trim the front of a `BufferSequence`.

    This adapter wraps a buffer sequence to create a new sequence
    which may be incrementally consumed. Bytes consumed are removed
    from the front of the buffer. The underlying memory is not changed,
    instead the adapter efficiently iterates through a subset of
    the buffers wrapped.

    The wrapped buffer is not modified, a copy is made instead.
    Ownership of the underlying memory is not transferred, the application
    is still responsible for managing its lifetime.

    @tparam Buffers The buffer sequence to wrap.

    @ptaram ValueType The type of buffer of the final buffer sequence. This
    can be different from the buffer type of the wrapped sequence. For
    example, a `MutableBufferSequence` can be transformed into a
    consumable `ConstBufferSequence`. Violations of buffer const safety
    are not permitted, and will result in a compile error.
*/
template<class Buffers,
    class ValueType = typename Buffers::value_type>
class consuming_buffers
{
    using iter_type =
        typename Buffers::const_iterator;

    static_assert(std::is_constructible<ValueType,
        typename std::iterator_traits<iter_type>::value_type>::value,
            "ValueType requirements not met");

    Buffers bs_;
    iter_type begin_;
    std::size_t skip_ = 0;

    template<class Deduced>
    consuming_buffers(Deduced&& other, std::size_t nbegin)
        : bs_(std::forward<Deduced>(other).bs_)
        , begin_(std::next(bs_.begin(), nbegin))
        , skip_(other.skip_)
    {
    }

public:
    /// The type for each element in the list of buffers.
    using value_type = ValueType;

    class const_iterator;

    /// Move constructor.
    consuming_buffers(consuming_buffers&&);

    /// Copy constructor.
    consuming_buffers(consuming_buffers const&);

    /// Move assignment.
    consuming_buffers& operator=(consuming_buffers&&);

    /// Copy assignment.
    consuming_buffers& operator=(consuming_buffers const&);

    /** Construct to represent a buffer sequence.

        A copy of the buffer sequence is made. Ownership of the
        underlying memory is not transferred or copied.
    */
    explicit
    consuming_buffers(Buffers const& buffers);

    /// Get a bidirectional iterator to the first element.
    const_iterator
    begin() const;

    /// Get a bidirectional iterator for one past the last element.
    const_iterator
    end() const;

    /** Remove bytes from the beginning of the sequence.

        @param n The number of bytes to remove. If this is
        larger than the number of bytes remaining, all the
        bytes remaining are removed.
    */
    void
    consume(std::size_t n);
};

/// A bidirectional iterator type that may be used to read elements.
template<class Buffers, class ValueType>
class consuming_buffers<Buffers, ValueType>::const_iterator
{
    friend class consuming_buffers<Buffers, ValueType>;

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
            iter_type it)
        : it_(it)
        , b_(&b)
    {
    }
};

template<class Buffers, class ValueType>
consuming_buffers<Buffers, ValueType>::
consuming_buffers(consuming_buffers&& other)
    : consuming_buffers(std::move(other),
        std::distance<iter_type>(
            other.bs_.begin(), other.begin_))
{
}

template<class Buffers, class ValueType>
consuming_buffers<Buffers, ValueType>::
consuming_buffers(consuming_buffers const& other)
    : consuming_buffers(other,
        std::distance<iter_type>(
            other.bs_.begin(), other.begin_))
{
}

template<class Buffers, class ValueType>
auto
consuming_buffers<Buffers, ValueType>::
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

template<class Buffers, class ValueType>
auto
consuming_buffers<Buffers, ValueType>::
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

template<class Buffers, class ValueType>
consuming_buffers<Buffers, ValueType>::
consuming_buffers(Buffers const& bs)
    : bs_(bs)
    , begin_(bs_.begin())
{
}

template<class Buffers, class ValueType>
auto
consuming_buffers<Buffers, ValueType>::begin() const ->
    const_iterator
{
    return const_iterator{*this, begin_};
}

template<class Buffers, class ValueType>
auto
consuming_buffers<Buffers, ValueType>::end() const ->
    const_iterator
{
    return const_iterator{*this, bs_.end()};
}

template<class Buffers, class ValueType>
void
consuming_buffers<Buffers, ValueType>::consume(std::size_t n)
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
