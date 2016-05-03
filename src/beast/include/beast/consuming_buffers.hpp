//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_CONSUMING_BUFFERS_HPP
#define BEAST_CONSUMING_BUFFERS_HPP

#include <beast/buffer_concepts.hpp>
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

    @tparam BufferSequence The buffer sequence to wrap.

    @tparam ValueType The type of buffer of the final buffer sequence. This
    can be different from the buffer type of the wrapped sequence. For
    example, a `MutableBufferSequence` can be transformed into a
    consumable `ConstBufferSequence`. Violations of buffer const safety
    are not permitted, and will result in a compile error.
*/
template<class BufferSequence,
    class ValueType = typename BufferSequence::value_type>
class consuming_buffers
{
    using iter_type =
        typename BufferSequence::const_iterator;

    static_assert(is_BufferSequence<BufferSequence, ValueType>::value,
        "BufferSequence requirements not met");

    static_assert(std::is_constructible<ValueType,
        typename std::iterator_traits<iter_type>::value_type>::value,
            "ValueType requirements not met");

    BufferSequence bs_;
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

#if GENERATING_DOCS
    /// A bidirectional iterator type that may be used to read elements.
    using const_iterator = implementation_defined;

#else
    class const_iterator;

#endif

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
    consuming_buffers(BufferSequence const& buffers);

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

/** Returns a new, consumned buffer sequence.

    This function returns a new buffer sequence which when iterated,
    efficiently represents the portion of the original buffer sequence
    with `n` bytes removed from the beginning.

    Copies will be made of the buffer sequence passed, but ownership
    of the underlying memory is not transferred.

    @param buffers The buffer sequence to consume.

    @param n The number of bytes to remove from the front. If this is
    larger than the size of the buffer sequence, an empty buffer sequence
    is returned.
*/
template<class BufferSequence>
consuming_buffers<BufferSequence, typename BufferSequence::value_type>
consumed_buffers(BufferSequence const& buffers, std::size_t n);

} // beast

#include <beast/impl/consuming_buffers.ipp>

#endif
