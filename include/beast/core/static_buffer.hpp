//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STATIC_BUFFER_HPP
#define BEAST_STATIC_BUFFER_HPP

#include <beast/config.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace beast {

/** A @b DynamicBuffer with a fixed size internal buffer.

    Ownership of the underlying storage belongs to the derived class.

    @note Variables are usually declared using the template class
    @ref static_buffer_n; however, to reduce the number of instantiations
    of template functions receiving static stream buffer arguments in a
    deduced context, the signature of the receiving function should use
    @ref static_buffer.

    When used with @ref static_buffer_n this implements a dynamic
    buffer using no memory allocations.

    @see @ref static_buffer_n
*/
class static_buffer
{
    char* begin_;
    char* in_;
    char* out_;
    char* last_;
    char* end_;

    static_buffer(static_buffer const& other) = delete;
    static_buffer& operator=(static_buffer const&) = delete;

public:
    /// The type used to represent the input sequence as a list of buffers.
    using const_buffers_type = boost::asio::const_buffers_1;

    /// The type used to represent the output sequence as a list of buffers.
    using mutable_buffers_type = boost::asio::mutable_buffers_1;

    /** Constructor.

        This creates a dynamic buffer using the provided storage area.

        @param p A pointer to valid storage of at least `n` bytes.

        @param n The number of valid bytes pointed to by `p`.
    */
    static_buffer(void* p, std::size_t n)
    {
        reset_impl(p, n);
    }

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
        return dist(begin_, end_);
    }

    /// Return the maximum sum of input and output sizes that can be held without an allocation.
    std::size_t
    capacity() const
    {
        return max_size();
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
        consume_impl(n);
    }

protected:
    /** Default constructor.

        The buffer will be in an undefined state. It is necessary
        for the derived class to call @ref reset in order to
        initialize the object.
    */
    static_buffer();

    /** Reset the pointed-to buffer.

        This function resets the internal state to the buffer provided.
        All input and output sequences are invalidated. This function
        allows the derived class to construct its members before
        initializing the static buffer.

        @param p A pointer to valid storage of at least `n` bytes.

        @param n The number of valid bytes pointed to by `p`.
    */
    void
    reset(void* p, std::size_t n);

private:
    static
    inline
    std::size_t
    dist(char const* first, char const* last)
    {
        return static_cast<std::size_t>(last - first);
    }

    template<class = void>
    void
    reset_impl(void* p, std::size_t n);

    template<class = void>
    mutable_buffers_type
    prepare_impl(std::size_t n);

    template<class = void>
    void
    consume_impl(std::size_t n);
};

//------------------------------------------------------------------------------

/** A @b DynamicBuffer with a fixed size internal buffer.

    This implements a dynamic buffer using no memory allocations.

    @tparam N The number of bytes in the internal buffer.

    @note To reduce the number of template instantiations when passing
    objects of this type in a deduced context, the signature of the
    receiving function should use @ref static_buffer instead.

    @see @ref static_buffer
*/
template<std::size_t N>
class static_buffer_n : public static_buffer
{
    char buf_[N];

public:
    /// Copy constructor
    static_buffer_n(static_buffer_n const&);

    /// Copy assignment
    static_buffer_n& operator=(static_buffer_n const&);

    /// Construct a static buffer.
    static_buffer_n()
        : static_buffer(buf_, N)
    {
    }

    /// Returns the @ref static_buffer portion of this object
    static_buffer&
    base()
    {
        return *this;
    }

    /// Returns the @ref static_buffer portion of this object
    static_buffer const&
    base() const
    {
        return *this;
    }
};

} // beast

#include <beast/core/impl/static_buffer.ipp>

#endif
