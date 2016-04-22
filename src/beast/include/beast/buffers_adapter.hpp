//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFERS_ADAPTER_HPP
#define BEAST_BUFFERS_ADAPTER_HPP

#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace beast {

/** Adapts a `MutableBufferSequence` into a `Streambuf`.

    This class wraps a `MutableBufferSequence` to meet the requirements
    of `Streambuf`. Upon construction the input and output sequences are
    empty. A copy of the mutable buffer sequence object is stored; however,
    ownership of the underlying memory is not transferred. The caller is
    responsible for making sure that referenced memory remains valid
    for the duration of any operations.

    The size of the mutable buffer sequence determines the maximum
    number of bytes which may be prepared and committed.

    @tparam Buffers The type of mutable buffer sequence to wrap.
*/
template<class Buffers>
class buffers_adapter
{
private:
    using buffers_type = typename std::decay<Buffers>::type;
    using iter_type = typename buffers_type::const_iterator;

    static auto constexpr is_mutable =
        std::is_constructible<boost::asio::mutable_buffer,
            typename std::iterator_traits<iter_type>::value_type>::value;

    Buffers bs_;
    iter_type begin_;
    iter_type out_;
    iter_type end_;
    std::size_t max_size_;
    std::size_t in_pos_ = 0;    // offset in *begin_
    std::size_t in_size_ = 0;   // size of input sequence
    std::size_t out_pos_ = 0;   // offset in *out_
    std::size_t out_end_ = 0;   // output end offset

    template<class Deduced>
    buffers_adapter(Deduced&& other,
        std::size_t nbegin, std::size_t nout,
            std::size_t nend)
        : bs_(std::forward<Deduced>(other).bs_)
        , begin_(std::next(bs_.begin(), nbegin))
        , out_(std::next(bs_.begin(), nout))
        , end_(std::next(bs_.begin(), nend))
        , max_size_(other.max_size_)
        , in_pos_(other.in_pos_)
        , in_size_(other.in_size_)
        , out_pos_(other.out_pos_)
        , out_end_(other.out_end_)
    {
    }

public:
    class const_buffers_type;
    class mutable_buffers_type;

    // Move constructor.
    buffers_adapter(buffers_adapter&& other);

    // Copy constructor.
    buffers_adapter(buffers_adapter const& other);

    // Move assignment.
    buffers_adapter& operator=(buffers_adapter&& other);

    // Copy assignment.
    buffers_adapter& operator=(buffers_adapter const&);

    /** Construct a buffers adapter.

        @param buffers The mutable buffer sequence to wrap. A copy of
        the object will be made, but ownership of the memory is not
        transferred.
    */
    explicit
    buffers_adapter(Buffers const& buffers);

    /// Returns the largest size output sequence possible.
    std::size_t
    max_size() const
    {
        return max_size_;
    }

    /// Get the size of the input sequence.
    std::size_t
    size() const
    {
        return in_size_;
    }

    /** Get a list of buffers that represents the output sequence, with the given size.

        @throws std::length_error if the size would exceed the limit
        imposed by the underlying mutable buffer sequence.
    */
    mutable_buffers_type
    prepare(std::size_t n);

    /// Move bytes from the output sequence to the input sequence.
    void
    commit(std::size_t n);

    /// Get a list of buffers that represents the input sequence.
    const_buffers_type
    data() const;

    /// Remove bytes from the input sequence.
    void
    consume(std::size_t n);
};

//------------------------------------------------------------------------------

/// The type used to represent the input sequence as a list of buffers.
template<class Buffers>
class buffers_adapter<Buffers>::const_buffers_type
{
    buffers_adapter const* ba_;

public:
    using value_type = boost::asio::const_buffer;

    class const_iterator;

    const_buffers_type() = default;
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

template<class Buffers>
class buffers_adapter<Buffers>::const_buffers_type::const_iterator
{
    iter_type it_;
    buffers_adapter const* ba_;

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

template<class Buffers>
inline
auto
buffers_adapter<Buffers>::const_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->begin_};
}

template<class Buffers>
inline
auto
buffers_adapter<Buffers>::const_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->out_ ==
        ba_->end_ ? ba_->end_ : std::next(ba_->out_)};
}

//------------------------------------------------------------------------------

/// The type used to represent the output sequence as a list of buffers.
template<class Buffers>
class buffers_adapter<Buffers>::mutable_buffers_type
{
    buffers_adapter const* ba_;

public:
    using value_type = boost::asio::mutable_buffer;

    class const_iterator;

    mutable_buffers_type() = default;
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

template<class Buffers>
class buffers_adapter<Buffers>::mutable_buffers_type::const_iterator
{
    iter_type it_;
    buffers_adapter const* ba_;

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

template<class Buffers>
inline
auto
buffers_adapter<Buffers>::mutable_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->out_};
}

template<class Buffers>
inline
auto
buffers_adapter<Buffers>::mutable_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{*ba_, ba_->end_};
}

//------------------------------------------------------------------------------

template<class Buffers>
buffers_adapter<Buffers>::buffers_adapter(
        buffers_adapter&& other)
    : buffers_adapter(std::move(other),
        std::distance<iter_type>(other.bs_.begin(), other.begin_),
        std::distance<iter_type>(other.bs_.begin(), other.out_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class Buffers>
buffers_adapter<Buffers>::buffers_adapter(
        buffers_adapter const& other)
    : buffers_adapter(other,
        std::distance<iter_type>(other.bs_.begin(), other.begin_),
        std::distance<iter_type>(other.bs_.begin(), other.out_),
        std::distance<iter_type>(other.bs_.begin(), other.end_))
{
}

template<class Buffers>
auto
buffers_adapter<Buffers>::operator=(
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

template<class Buffers>
auto
buffers_adapter<Buffers>::operator=(
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

template<class Buffers>
buffers_adapter<Buffers>::buffers_adapter(
    Buffers const& bs)
    : bs_(bs)
    , begin_(bs_.begin())
    , out_(bs_.begin())
    , end_(bs_.begin())
    , max_size_(boost::asio::buffer_size(bs_))
{
}

template<class Buffers>
auto
buffers_adapter<Buffers>::prepare(std::size_t n) ->
    mutable_buffers_type
{
    using boost::asio::buffer_size;
    static_assert(is_mutable,
        "Operation not valid for ConstBufferSequence");
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
        throw std::length_error(
            "no space in buffers_adapter");
    return mutable_buffers_type{*this};
}

template<class Buffers>
void
buffers_adapter<Buffers>::commit(std::size_t n)
{
    using boost::asio::buffer_size;
    static_assert(is_mutable,
        "Operation not valid for ConstBufferSequence");
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

    n = std::min(n, out_end_ - out_pos_);
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

template<class Buffers>
inline
auto
buffers_adapter<Buffers>::data() const ->
    const_buffers_type
{
    return const_buffers_type{*this};
}

template<class Buffers>
void
buffers_adapter<Buffers>::consume(std::size_t n)
{
    for(;;)
    {
        if(begin_ != out_)
        {
            auto const avail =
                buffer_size(*begin_) - in_pos_;
            if(n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
                break;
            }
            n -= avail;
            in_size_ -= avail;
            in_pos_ = 0;
            ++begin_;
        }
        else
        {
            auto const avail = out_pos_ - in_pos_;
            if(n < avail)
            {
                in_size_ -= n;
                in_pos_ += n;
            }
            else
            {
                in_size_ -= avail;
                if(out_pos_ != out_end_||
                    out_ != std::prev(bs_.end()))
                {
                    in_pos_ = out_pos_;
                }
                else
                {
                    // Use the whole buffer now.
                    in_pos_ = 0;
                    out_pos_ = 0;
                    out_end_ = 0;
                }
            }
            break;
        }
    }
}

} // beast

#endif
