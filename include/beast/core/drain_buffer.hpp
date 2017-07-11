//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DRAIN_BUFFER_HPP
#define BEAST_DRAIN_BUFFER_HPP

#include <beast/config.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/throw_exception.hpp>

namespace beast {

/** A @b DynamicBuffer which does not retain its input sequence.

    This object implements a dynamic buffer with a fixed size
    output area, not dynamically allocated, and whose input
    sequence is always length zero. Bytes committed from the
    output area to the input area are always discarded. This
    is useful for calling interfaces that require a dynamic
    buffer for storage, but where the caller does not want
    to retain the data.
*/
class drain_buffer
{
    char buf_[512];
    std::size_t n_ = 0;

public:
    /// The type used to represent the input sequence as a list of buffers.
    using const_buffers_type = boost::asio::null_buffers;

    /// The type used to represent the output sequence as a list of buffers.
    using mutable_buffers_type = boost::asio::mutable_buffers_1;

    /// Constructor
    drain_buffer() = default;

    /// Copy constructor
    drain_buffer(drain_buffer const&)
    {
        // Previously returned ranges are invalidated
    }

    /// Copy assignment
    drain_buffer&
    operator=(drain_buffer const&)
    {
        n_ = 0;
        return *this;
    }

    /// Return the size of the input sequence.
    std::size_t
    size() const
    {
        return 0;
    }

    /// Return the maximum sum of the input and output sequence sizes.
    std::size_t
    max_size() const
    {
        return sizeof(buf_);
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
    data() const
    {
        return {};
    }

    /** Get a list of buffers that represent the output sequence, with the given size.

        @throws std::length_error if the size would exceed the buffer limit
    */
    mutable_buffers_type
    prepare(std::size_t n)
    {
        if(n > sizeof(buf_))
            BOOST_THROW_EXCEPTION(std::length_error{
                "buffer overflow"});
        n_ = n;
        return {buf_, n_};
    }

    /** Move bytes from the output sequence to the input sequence.

        This call always discards the output sequence.
        The size of the input sequence will remain at zero.
    */
    void
    commit(std::size_t)
    {
    }

    /** Remove bytes from the input sequence.

        This call has no effect.
    */
    void
    consume(std::size_t) const
    {
    }
};
} // beast

#endif
