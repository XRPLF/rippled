//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFERS_ADAPTER_HPP
#define BEAST_BUFFERS_ADAPTER_HPP

#include <boost/asio/buffer.hpp>
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
#if GENERATING_DOCS
    /// The type used to represent the input sequence as a list of buffers.
    using const_buffers_type = implementation_defined;

    /// The type used to represent the output sequence as a list of buffers.
    using mutable_buffers_type = implementation_defined;

#else
    class const_buffers_type;

    class mutable_buffers_type;

#endif

    /// Move constructor.
    buffers_adapter(buffers_adapter&& other);

    /// Copy constructor.
    buffers_adapter(buffers_adapter const& other);

    /// Move assignment.
    buffers_adapter& operator=(buffers_adapter&& other);

    /// Copy assignment.
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

} // beast

#include <beast/impl/buffers_adapter.ipp>

#endif
