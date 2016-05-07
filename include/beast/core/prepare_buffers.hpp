//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_PREPARE_BUFFERS_HPP
#define BEAST_PREPARE_BUFFERS_HPP

#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace beast {

/** Get a trimmed const buffer.

    The new buffer starts at the beginning of the passed
    buffer. Ownership of the underlying memory is not
    transferred.
*/
inline
boost::asio::const_buffer
prepare_buffer(std::size_t n,
    boost::asio::const_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void const*>(buffer),
        std::min(n, buffer_size(buffer)) };
}

/** Get a trimmed mutable buffer.

    The new buffer starts at the beginning of the passed
    buffer. Ownership of the underlying memory is not
    transferred.
*/
inline
boost::asio::mutable_buffer
prepare_buffer(std::size_t n,
    boost::asio::mutable_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void*>(buffer),
        std::min(n, buffer_size(buffer)) };
}

/** Wrapper to produce a trimmed buffer sequence.

    This wraps a buffer sequence to efficiently present a shorter
    subset of the original list of buffers starting with the first
    byte of the original sequence.

    @tparam BufferSequence The buffer sequence to wrap.
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

public:
    /// The type for each element in the list of buffers.
    using value_type =
        typename std::iterator_traits<iter_type>::value_type;

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

    /** Construct a wrapped buffer sequence.

        @param n The maximum number of bytes in the wrapped sequence.
        If this is larger than the size of buffers, the wrapped
        sequence will represent the entire input sequence.

        @param buffers The buffer sequence to wrap. A copy of the sequence
        will be made, but ownership of the underlying memory is not transferred.
    */
    prepared_buffers(std::size_t n, BufferSequence const& buffers);

    /// Get a bidirectional iterator to the first element.
    const_iterator
    begin() const;

    /// Get a bidirectional iterator for one past the last element.
    const_iterator
    end() const;

private:
    void
    setup(std::size_t n);
};

//------------------------------------------------------------------------------

/** Return a trimmed, wrapped buffer sequence.

    This function returns a new buffer sequence which wraps the provided
    buffer sequence and efficiently presents a shorter subset of the
    original list of buffers starting with the first byte of the original
    sequence.

    @param n The maximum number of bytes in the wrapped sequence. If this
    is larger than the size of buffers, the wrapped sequence will represent
    the entire input sequence.

    @param buffers The buffer sequence to wrap. A copy of the sequence
    will be made, but ownership of the underlying memory is not transferred.
*/
template<class BufferSequence>
prepared_buffers<BufferSequence>
prepare_buffers(std::size_t n, BufferSequence const& buffers);

} // beast

#include <beast/core/impl/prepare_buffers.ipp>

#endif
