//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STATIC_STREAMBUF_HPP
#define BEAST_STATIC_STREAMBUF_HPP

#include <boost/asio/buffer.hpp>
#include <boost/utility/base_from_member.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>

namespace beast {

/** A `Streambuf` with a fixed size internal buffer.

    Ownership of the underlying storage belongs to the derived class.

    @note Variables are usually declared using the template class
    `static_streambuf_n`; however, to reduce the number of instantiations
    of template functions receiving static stream buffer arguments in a
    deduced context, the signature of the receiving function should use
    `static_streambuf`.
*/
class static_streambuf
{
#if GENERATING_DOCS
private:
#else
protected:
#endif
    std::uint8_t* in_;
    std::uint8_t* out_;
    std::uint8_t* last_;
    std::uint8_t* end_;

public:
    class const_buffers_type;
    class mutable_buffers_type;

#if GENERATING_DOCS
private:
#endif
    static_streambuf(
        static_streambuf const& other) noexcept = delete;
    static_streambuf& operator=(
        static_streambuf const&) noexcept = delete;
#if GENERATING_DOCS
public:
#endif

    /// Returns the largest size output sequence possible.
    std::size_t
    max_size() const
    {
        return end_ - in_;
    }

    /// Get the size of the input sequence.
    std::size_t
    size() const
    {
        return out_ - in_;
    }

    /** Get a list of buffers that represents the output sequence, with the given size.

        @throws std::length_error if the size would exceed the limit
        imposed by the underlying mutable buffer sequence.
    */
    mutable_buffers_type
    prepare(std::size_t n);

    /// Move bytes from the output sequence to the input sequence.
    void
    commit(std::size_t n)
    {
        out_ += std::min<std::size_t>(n, last_ - out_);
    }

    /// Get a list of buffers that represents the input sequence.
    const_buffers_type
    data() const;

    /// Remove bytes from the input sequence.
    void
    consume(std::size_t n)
    {
        in_ += std::min<std::size_t>(n, out_ - in_);
    }

#if GENERATING_DOCS
private:
#else
protected:
#endif
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

/// The type used to represent the input sequence as a list of buffers.
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

    pointer
    operator->() const = delete;

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

/// The type used to represent the output sequence as a list of buffers.
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

    pointer
    operator->() const = delete;

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
static_streambuf::prepare(std::size_t n) ->
    mutable_buffers_type
{
    if(n > static_cast<std::size_t>(end_ - out_))
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

/** A `Streambuf` with a fixed size internal buffer.

    @tparam N The number of bytes in the internal buffer.

    @note To reduce the number of template instantiations when passing
    objects of this type in a deduced context, the signature of the
    receiving function should use `static_streambuf` instead.
*/
template<std::size_t N>
class static_streambuf_n
    : private boost::base_from_member<
        std::array<std::uint8_t, N>>
    , public static_streambuf
{
    using member_type = boost::base_from_member<
        std::array<std::uint8_t, N>>;
public:
#if GENERATING_DOCS
private:
#endif
    static_streambuf_n(
        static_streambuf_n const&) = delete;
    static_streambuf_n& operator=(
        static_streambuf_n const&) = delete;
#if GENERATING_DOCS
public:
#endif

    /// Construct a static stream buffer.
    static_streambuf_n()
        : static_streambuf(
            member_type::member.data(),
                member_type::member.size())
    {
    }

    /** Reset the stream buffer.

        Postconditions:
            The input sequence and output sequence are empty,
            `max_size()` returns `N`.
    */
    void
    reset()
    {
        static_streambuf::reset(
            member_type::member.data(),
                member_type::member.size());
    }
};

} // beast

#endif
