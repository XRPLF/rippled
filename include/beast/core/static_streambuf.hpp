//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STATIC_STREAMBUF_HPP
#define BEAST_STATIC_STREAMBUF_HPP

#include <beast/config.hpp>
#include <boost/utility/base_from_member.hpp>
#include <algorithm>
#include <array>
#include <cstring>

namespace beast {

/** A @b `DynamicBuffer` with a fixed size internal buffer.

    Ownership of the underlying storage belongs to the derived class.

    @note Variables are usually declared using the template class
    @ref static_streambuf_n; however, to reduce the number of instantiations
    of template functions receiving static stream buffer arguments in a
    deduced context, the signature of the receiving function should use
    @ref static_streambuf.
*/
class static_streambuf
{
#if GENERATING_DOCS
private:
#else
protected:
#endif
    std::uint8_t* begin_;
    std::uint8_t* in_;
    std::uint8_t* out_;
    std::uint8_t* last_;
    std::uint8_t* end_;

public:
#if GENERATING_DOCS
    /// The type used to represent the input sequence as a list of buffers.
    using const_buffers_type = implementation_defined;

    /// The type used to represent the output sequence as a list of buffers.
    using mutable_buffers_type = implementation_defined;

#else
    class const_buffers_type;
    class mutable_buffers_type;

    static_streambuf(
        static_streambuf const& other) noexcept = delete;

    static_streambuf& operator=(
        static_streambuf const&) noexcept = delete;

#endif

    /// Return the size of the input sequence.
    std::size_t
    size() const
    {
        return out_ - in_;
    }

    /// Return the maximum sum of the input and output sequence sizes.
    std::size_t
    max_size() const
    {
        return end_ - begin_;
    }

    /// Return the maximum sum of input and output sizes that can be held without an allocation.
    std::size_t
    capacity() const
    {
        return end_ - in_;
    }

    /** Get a list of buffers that represent the input sequence.

        @note These buffers remain valid across subsequent calls to `prepare`.
    */
    const_buffers_type
    data() const;

    /** Get a list of buffers that represent the output sequence, with the given size.

        @throws std::length_error if the size would exceed the limit
        imposed by the underlying mutable buffer sequence.

        @note Buffers representing the input sequence acquired prior to
        this call remain valid.
    */
    mutable_buffers_type
    prepare(std::size_t n);

    /** Move bytes from the output sequence to the input sequence.

        @note Buffers representing the input sequence acquired prior to
        this call remain valid.
    */
    void
    commit(std::size_t n)
    {
        out_ += std::min<std::size_t>(n, last_ - out_);
    }

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
        begin_ = p;
        in_ = p;
        out_ = p;
        last_ = p;
        end_ = p + n;
    }
};

//------------------------------------------------------------------------------

/** A `DynamicBuffer` with a fixed size internal buffer.

    @tparam N The number of bytes in the internal buffer.

    @note To reduce the number of template instantiations when passing
    objects of this type in a deduced context, the signature of the
    receiving function should use `static_streambuf` instead.
*/
template<std::size_t N>
class static_streambuf_n
    : public static_streambuf
#if ! GENERATING_DOCS
    , private boost::base_from_member<
        std::array<std::uint8_t, N>>
#endif
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

#include <beast/core/impl/static_streambuf.ipp>

#endif
