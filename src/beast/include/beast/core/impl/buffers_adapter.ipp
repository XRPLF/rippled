//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_BUFFERS_ADAPTER_IPP
#define BEAST_IMPL_BUFFERS_ADAPTER_IPP

#include <beast/core/detail/type_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace beast {

template<class MutableBufferSequence>
class buffers_adapter<MutableBufferSequence>::
    const_buffers_type
{
    buffers_adapter const* ba_;

public:
    using value_type = boost::asio::const_buffer;

    class const_iterator;

    const_buffers_type() = delete;
    const_buffers_type(
        const_buffers_type const&) = default;
    const_buffers_type& operator=(
        const_buffers_type const&) = default;

    const_iterator
    begin() const;

    const_iterator
    end() const;

private:
    friend class buffers_adapter;

    const_buffers_type(buffers_adapter const& ba)
        : ba_(&ba)
    {
    }
};

template<class MutableBufferSequence>
class buffers_adapter<MutableBufferSequence>::
    const_buffers_type::const_iterator
{
    iter_type it_;
    buffers_adapter const* ba_ = nullptr;

public:
    using value_type = boost::asio::const_buffer;
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
        return ba_ == other.ba_ &&
            it_ == other.it_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        return value_type{buffer_cast<void const*>(*it_),
            (ba_->out_ == ba_->bs_.end() ||
                it_ != ba_->out_) ? buffer_size(*it_) : ba_->out_pos_} +
                    (it_ == ba_->begin_ ? ba_->in_pos_ : 0);
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
    friend class const_buffers_type;

    const_iterator(buffers_adapter const& ba,
            iter_type iter)
        : it_(iter)
        , ba_(&ba)
    {
    }
};

template<class MutableBufferSequence>
inline
auto
buffers_adapter<MutableBufferSequence>::const_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->begin_};
}

template<class MutableBufferSequence>
inline
auto
buffers_adapter<MutableBufferSequence>::const_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->out_ ==
        ba_->end_ ? ba_->end_ : std::next(ba_->out_)};
}

//------------------------------------------------------------------------------

template<class MutableBufferSequence>
class buffers_adapter<MutableBufferSequence>::
mutable_buffers_type
{
    buffers_adapter const* ba_;

public:
    using value_type = boost::asio::mutable_buffer;

    class const_iterator;

    mutable_buffers_type() = delete;
    mutable_buffers_type(
        mutable_buffers_type const&) = default;
    mutable_buffers_type& operator=(
        mutable_buffers_type const&) = default;

    const_iterator
    begin() const;

    const_iterator
    end() const;

private:
    friend class buffers_adapter;

    mutable_buffers_type(
            buffers_adapter const& ba)
        : ba_(&ba)
    {
    }
};

template<class MutableBufferSequence>
class buffers_adapter<MutableBufferSequence>::
mutable_buffers_type::const_iterator
{
    iter_type it_;
    buffers_adapter const* ba_ = nullptr;

public:
    using value_type = boost::asio::mutable_buffer;
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
        return ba_ == other.ba_ &&
            it_ == other.it_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        return value_type{buffer_cast<void*>(*it_),
            it_ == std::prev(ba_->end_) ?
                ba_->out_end_ : buffer_size(*it_)} +
                    (it_ == ba_->out_ ? ba_->out_pos_ : 0);
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
    friend class mutable_buffers_type;

    const_iterator(buffers_adapter const& ba,
            iter_type iter)
        : it_(iter)
        , ba_(&ba)
    {
    }
};

template<class MutableBufferSequence>
inline
auto
buffers_adapter<MutableBufferSequence>::mutable_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->out_};
}

template<class MutableBufferSequence>
inline
auto
buffers_adapter<MutableBufferSequence>::mutable_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->end_};
}

//------------------------------------------------------------------------------

template<class MutableBufferSequence>
buffers_adapter<MutableBufferSequence>::buffers_adapter(
        buffers_adapter&& other)
    : buffers_adapter(std::move(other),
        std::distance<iter_type>(other.bs_.begin(), other.begin_),
        std::distance<iter_type>(other.bs_.begin(), other.out_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class MutableBufferSequence>
buffers_adapter<MutableBufferSequence>::buffers_adapter(
        buffers_adapter const& other)
    : buffers_adapter(other,
        std::distance<iter_type>(other.bs_.begin(), other.begin_),
        std::distance<iter_type>(other.bs_.begin(), other.out_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class MutableBufferSequence>
auto
buffers_adapter<MutableBufferSequence>::operator=(
    buffers_adapter&& other) -> buffers_adapter&
{
    auto const nbegin = std::distance<iter_type>(
        other.bs_.begin(), other.begin_);
    auto const nout = std::distance<iter_type>(
        other.bs_.begin(), other.out_);
    auto const nend = std::distance<iter_type>(
        other.bs_.begin(), other.end_);
    bs_ = std::move(other.bs_);
    begin_ = std::next(bs_.begin(), nbegin);
    out_ = std::next(bs_.begin(), nout);
    end_ = std::next(bs_.begin(), nend);
    max_size_ = other.max_size_;
    in_pos_ = other.in_pos_;
    in_size_ = other.in_size_;
    out_pos_ = other.out_pos_;
    out_end_ = other.out_end_;
    return *this;
}

template<class MutableBufferSequence>
auto
buffers_adapter<MutableBufferSequence>::operator=(
    buffers_adapter const& other) -> buffers_adapter&
{
    auto const nbegin = std::distance<iter_type>(
        other.bs_.begin(), other.begin_);
    auto const nout = std::distance<iter_type>(
        other.bs_.begin(), other.out_);
    auto const nend = std::distance<iter_type>(
        other.bs_.begin(), other.end_);
    bs_ = other.bs_;
    begin_ = std::next(bs_.begin(), nbegin);
    out_ = std::next(bs_.begin(), nout);
    end_ = std::next(bs_.begin(), nend);
    max_size_ = other.max_size_;
    in_pos_ = other.in_pos_;
    in_size_ = other.in_size_;
    out_pos_ = other.out_pos_;
    out_end_ = other.out_end_;
    return *this;
}

template<class MutableBufferSequence>
buffers_adapter<MutableBufferSequence>::buffers_adapter(
    MutableBufferSequence const& bs)
    : bs_(bs)
    , begin_(bs_.begin())
    , out_(bs_.begin())
    , end_(bs_.begin())
    , max_size_(boost::asio::buffer_size(bs_))
{
}

template<class MutableBufferSequence>
auto
buffers_adapter<MutableBufferSequence>::prepare(std::size_t n) ->
    mutable_buffers_type
{
    using boost::asio::buffer_size;
    end_ = out_;
    if(end_ != bs_.end())
    {
        auto size = buffer_size(*end_) - out_pos_;
        if(n > size)
        {
            n -= size;
            while(++end_ != bs_.end())
            {
                size = buffer_size(*end_);
                if(n < size)
                {
                    out_end_ = n;
                    n = 0;
                    ++end_;
                    break;
                }
                n -= size;
                out_end_ = size;
            }
        }
        else
        {
            ++end_;
            out_end_ = out_pos_ + n;
            n = 0;
        }
    }
    if(n > 0)
        BOOST_THROW_EXCEPTION(std::length_error{
            "buffer overflow"});
    return mutable_buffers_type{*this};
}

template<class MutableBufferSequence>
void
buffers_adapter<MutableBufferSequence>::commit(std::size_t n)
{
    using boost::asio::buffer_size;
    if(out_ == end_)
        return;
    auto const last = std::prev(end_);
    while(out_ != last)
    {
        auto const avail =
            buffer_size(*out_) - out_pos_;
        if(n < avail)
        {
            out_pos_ += n;
            in_size_ += n;
            max_size_ -= n;
            return;
        }
        ++out_;
        n -= avail;
        out_pos_ = 0;
        in_size_ += avail;
        max_size_ -= avail;
    }

    n = (std::min)(n, out_end_ - out_pos_);
    out_pos_ += n;
    in_size_ += n;
    max_size_ -= n;
    if(out_pos_ == buffer_size(*out_))
    {
        ++out_;
        out_pos_ = 0;
        out_end_ = 0;
    }
}

template<class MutableBufferSequence>
inline
auto
buffers_adapter<MutableBufferSequence>::data() const ->
    const_buffers_type
{
    return const_buffers_type{*this};
}

template<class MutableBufferSequence>
void
buffers_adapter<MutableBufferSequence>::consume(std::size_t n)
{
    using boost::asio::buffer_size;
    while(begin_ != out_)
    {
        auto const avail =
            buffer_size(*begin_) - in_pos_;
        if(n < avail)
        {
            in_size_ -= n;
            in_pos_ += n;
            return;
        }
        n -= avail;
        in_size_ -= avail;
        in_pos_ = 0;
        ++begin_;
    }
    auto const avail = out_pos_ - in_pos_;
    if(n < avail)
    {
        in_size_ -= n;
        in_pos_ += n;
    }
    else
    {
        in_size_ -= avail;
        in_pos_ = out_pos_;
    }
}

} // beast

#endif
