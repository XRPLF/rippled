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

#ifndef BEAST_ASIO_STATIC_STREAMBUF_H_INLUDED
#define BEAST_ASIO_STATIC_STREAMBUF_H_INLUDED

#include <boost/asio/buffer.hpp>
#include <boost/utility/base_from_member.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>

namespace beast {
namespace asio {

/// A Streambuf with a fixed size internal buffer
/**
*/
class static_streambuf
{
protected:
    std::uint8_t* in_;
    std::uint8_t* out_;
    std::uint8_t* last_;
    std::uint8_t* end_;

public:
    using size_type = std::size_t;

    class const_buffers_type;
    class mutable_buffers_type;

    static_streambuf(
        static_streambuf const& other) noexcept = delete;

    static_streambuf& operator=
        (static_streambuf const&) noexcept = delete;

    /** Get the maximum size of the basic_streambuf. */
    size_type
    max_size() const
    {
        return end_ - in_;
    }

    /** Get the size of the input sequence. */
    size_type
    size() const
    {
        return out_ - in_;
    }

    /** Get a list of buffers that represents the output sequence, with the given size. */
    mutable_buffers_type
    prepare(size_type n);

    /** Move bytes from the output sequence to the input sequence. */
    void
    commit(size_type n)
    {
        out_ += std::min<std::size_t>(n, last_ - out_);
    }

    /** Get a list of buffers that represents the input sequence. */
    const_buffers_type
    data() const;

    /** Remove bytes from the input sequence. */
    void
    consume(size_type n)
    {
        in_ += std::min<std::size_t>(n, out_ - in_);
    }

protected:
    static_streambuf(std::uint8_t* p, std::size_t n)
    {
        reset(p, n);
    }

    void
    reset(std::uint8_t* p, std::size_t n)
    {
        in_ = p;
        out_ = p;
        last_ = p;
        end_ = p + n;
    }
};

//------------------------------------------------------------------------------

class static_streambuf::const_buffers_type
{
    std::size_t n_;
    std::uint8_t const* p_;

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
    friend class static_streambuf;

    const_buffers_type(
            std::uint8_t const* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

class static_streambuf::const_buffers_type::const_iterator
{
    std::size_t n_;
    std::uint8_t const* p_;

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
        return p_ == other.p_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return value_type{p_, n_};
    }

    // Unsupported since we return by value
    /*
    pointer
    operator->() const
    {
        return &**this;
    }
    */

    const_iterator&
    operator++()
    {
        p_ += n_;
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
        p_ -= n_;
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

    const_iterator(
            std::uint8_t const* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

inline
auto
static_streambuf::const_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{p_, n_};
}

inline
auto
static_streambuf::const_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{p_ + n_, n_};
}

//------------------------------------------------------------------------------

class static_streambuf::mutable_buffers_type
{
    std::size_t n_;
    std::uint8_t* p_;

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
    friend class static_streambuf;

    mutable_buffers_type(
            std::uint8_t* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

class static_streambuf::mutable_buffers_type::const_iterator
{
    std::size_t n_;
    std::uint8_t* p_;

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
        return p_ == other.p_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return value_type{p_, n_};
    }

    // Unsupported since we return by value
    /*
    pointer
    operator->() const
    {
        return &**this;
    }
    */

    const_iterator&
    operator++()
    {
        p_ += n_;
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
        p_ -= n_;
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

    const_iterator(std::uint8_t* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

inline
auto
static_streambuf::mutable_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{p_, n_};
}

inline
auto
static_streambuf::mutable_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{p_ + n_, n_};
}

//------------------------------------------------------------------------------

inline
auto
static_streambuf::prepare(size_type n) ->
    mutable_buffers_type
{
    if(n > end_ - out_)
        throw std::length_error("no space in streambuf");
    last_ = out_ + n;
    return mutable_buffers_type{out_, n};
}

inline
auto
static_streambuf::data() const ->
    const_buffers_type
{
    return const_buffers_type{in_,
        static_cast<std::size_t>(out_ - in_)};
}

//------------------------------------------------------------------------------

/// A Streambuf with a fixed size internal buffer
/**
    To pass one of these as a parameter, use the base static_streambuf
*/
template<std::size_t N>
class static_streambuf_n
    : private boost::base_from_member<
        std::array<std::uint8_t, N>>
    , public static_streambuf
{
public:
    static_streambuf_n(
        static_streambuf_n const&) = delete;
    static_streambuf_n& operator=(
        static_streambuf_n const&) = delete;

    static_streambuf_n()
        : static_streambuf(member.data(), member.size())
    {
    }

    void
    reset()
    {
        static_streambuf::reset(member.data(), member.size());
    }
};

} // asio
} // beast

#endif
