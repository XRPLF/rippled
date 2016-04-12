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

#ifndef BEAST_ASIO_PREPARE_BUFFERS_H_INLUDED
#define BEAST_ASIO_PREPARE_BUFFERS_H_INLUDED

#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace beast {

inline
boost::asio::const_buffer
prepare_buffer(
    std::size_t n, boost::asio::const_buffer b)
{
    return { boost::asio::buffer_cast<void const*>(b),
        std::min(n, boost::asio::buffer_size(b)) };
}

inline
boost::asio::mutable_buffer
prepare_buffer(
    std::size_t n, boost::asio::mutable_buffer b)
{
    return { boost::asio::buffer_cast<void*>(b),
        std::min(n, boost::asio::buffer_size(b)) };
}

template<class Buffers>
class prepared_buffers
{
    using iter_type =
        typename Buffers::const_iterator;

    Buffers bs_;
    iter_type back_;
    iter_type end_;
    std::size_t size_;

    template<class Deduced>
    prepared_buffers(Deduced&& other,
            std::size_t nback, std::size_t nend)
        : bs_(std::forward<Deduced>(other.bs_))
        , back_(std::next(bs_.begin(), nback))
        , end_(std::next(bs_.begin(), nend))
        , size_(other.size_)
    {
    }

public:
    using value_type =
        typename std::iterator_traits<iter_type>::value_type;

    class const_iterator;

    prepared_buffers(prepared_buffers&&);
    prepared_buffers(prepared_buffers const&);
    prepared_buffers& operator=(prepared_buffers&&);
    prepared_buffers& operator=(prepared_buffers const&);

    prepared_buffers(std::size_t n, Buffers const& bs);

    const_iterator
    begin() const;

    const_iterator
    end() const;

private:
    void
    setup(std::size_t n)
    {
        end_ = bs_.begin();
        if(n == 0)
            return;
        for(;end_ != bs_.end(); ++end_)
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
        back_ = end_;
    }
};

template<class Buffers>
class prepared_buffers<Buffers>::const_iterator
{
    friend class prepared_buffers<Buffers>;

    using iter_type =
        typename Buffers::const_iterator;

    prepared_buffers const* b_;
    typename Buffers::const_iterator it_;

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

template<class Buffers>
prepared_buffers<Buffers>::
prepared_buffers(prepared_buffers&& other)
    : prepared_buffers(std::move(other),
        std::distance(other.bs_.begin(), other.back_),
        std::distance(other.bs_.begin(), other.end_))
{
}

template<class Buffers>
prepared_buffers<Buffers>::
prepared_buffers(prepared_buffers const& other)
    : prepared_buffers(other,
        std::distance(other.bs_.begin(), other.back_),
        std::distance(other.bs_.begin(), other.end_))
{
}

template<class Buffers>
auto
prepared_buffers<Buffers>::
operator=(prepared_buffers&& other) ->
    prepared_buffers&
{
    auto const nbegin =
        std::distance(other.bs_.begin(), other.back_);
    auto const nback =
        std::distance(other.bs_.begin(), other.end_);
    bs_ = std::move(other.bs_);
    back_ = std::next(bs_.begin(), nback);
    end_ = std::next(bs_.begin(), nend);
    size_ = other.size_;
    return *this;
}

template<class Buffers>
auto
prepared_buffers<Buffers>::
operator=(prepared_buffers const& other) ->
    prepared_buffers&
{
    auto const nbegin =
        std::distance(other.bs_.begin(), other.back_);
    bs_ = other.bs_;
    back_ = std::next(bs_.begin(), nback);
    end_ = std::next(bs_.begin(), nend);
    size_ = other.size_;
    return *this;
}

template<class Buffers>
prepared_buffers<Buffers>::prepared_buffers(
        std::size_t n, Buffers const& bs)
    : bs_(bs)
{
    setup(n);
}

template<class Buffers>
auto
prepared_buffers<Buffers>::begin() const ->
    const_iterator
{
    return const_iterator{*this, false};
}
    
template<class Buffers>
auto
prepared_buffers<Buffers>::end() const ->
    const_iterator
{
    return const_iterator{*this, true};
}

//------------------------------------------------------------------------------

/// Returns a Buffers which is the front subset of the passed buffers
/**
    @param n The maximum number of bytes in the returned buffer. If this is
    larger than buffer_size(buffers), the buffers returned will be the same
    size as the buffers passed.

    @param buffers The buffer sequence to prepare. The buffer sequence may
    be copied or moved as necessary.
*/
template<class Buffers>
inline
auto
prepare_buffers(std::size_t n, Buffers const& buffers)
{
    return prepared_buffers<Buffers>(n, buffers);
}

} // beast

#endif
