//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFER_PREFIX_HPP
#define BEAST_BUFFER_PREFIX_HPP

#include <beast/config.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/in_place_init.hpp>
#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <type_traits>

namespace beast {

/** A buffer sequence adapter that shortens the sequence size.

    The class adapts a buffer sequence to efficiently represent
    a shorter subset of the original list of buffers starting
    with the first byte of the original sequence.

    @tparam BufferSequence The buffer sequence to adapt.
*/
template<class BufferSequence>
class buffer_prefix_view
{
    using buffers_type = typename
        std::decay<BufferSequence>::type;

    using iter_type = typename buffers_type::const_iterator;

    BufferSequence bs_;
    iter_type back_;
    iter_type end_;
    std::size_t size_;

    template<class Deduced>
    buffer_prefix_view(Deduced&& other,
            std::size_t nback, std::size_t nend)
        : bs_(std::forward<Deduced>(other).bs_)
        , back_(std::next(bs_.begin(), nback))
        , end_(std::next(bs_.begin(), nend))
        , size_(other.size_)
    {
    }

    void
    setup(std::size_t n);

public:
    /// The type for each element in the list of buffers.
    using value_type = typename std::conditional<
        std::is_convertible<typename
            std::iterator_traits<iter_type>::value_type,
                boost::asio::mutable_buffer>::value,
                    boost::asio::mutable_buffer,
                        boost::asio::const_buffer>::type;

#if BEAST_DOXYGEN
    /// A bidirectional iterator type that may be used to read elements.
    using const_iterator = implementation_defined;

#else
    class const_iterator;

#endif

    /// Move constructor.
    buffer_prefix_view(buffer_prefix_view&&);

    /// Copy constructor.
    buffer_prefix_view(buffer_prefix_view const&);

    /// Move assignment.
    buffer_prefix_view& operator=(buffer_prefix_view&&);

    /// Copy assignment.
    buffer_prefix_view& operator=(buffer_prefix_view const&);

    /** Construct a buffer sequence prefix.

        @param n The maximum number of bytes in the prefix.
        If this is larger than the size of passed, buffers,
        the resulting sequence will represent the entire
        input sequence.

        @param buffers The buffer sequence to adapt. A copy of
        the sequence will be made, but ownership of the underlying
        memory is not transferred.
    */
    buffer_prefix_view(std::size_t n, BufferSequence const& buffers);

    /** Construct a buffer sequence prefix in-place.

        @param n The maximum number of bytes in the prefix.
        If this is larger than the size of passed, buffers,
        the resulting sequence will represent the entire
        input sequence.

        @param args Arguments forwarded to the contained buffers constructor.
    */
    template<class... Args>
    buffer_prefix_view(std::size_t n,
        boost::in_place_init_t, Args&&... args);

    /// Get a bidirectional iterator to the first element.
    const_iterator
    begin() const;

    /// Get a bidirectional iterator to one past the last element.
    const_iterator
    end() const;
};

/** Returns a prefix of a constant buffer.

    The returned buffer points to the same memory as the
    passed buffer, but with a size that is equal to or less
    than the size of the original buffer.

    @param n The size of the returned buffer.

    @param buffer The buffer to shorten. The underlying
    memory is not modified.

    @return A new buffer that points to the first `n` bytes
    of the original buffer.
*/
inline
boost::asio::const_buffer
buffer_prefix(std::size_t n,
    boost::asio::const_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void const*>(buffer),
        (std::min)(n, buffer_size(buffer)) };
}

/** Returns a prefix of a mutable buffer.

    The returned buffer points to the same memory as the
    passed buffer, but with a size that is equal to or less
    than the size of the original buffer.

    @param n The size of the returned buffer.

    @param buffer The buffer to shorten. The underlying
    memory is not modified.

    @return A new buffer that points to the first `n` bytes
    of the original buffer.
*/
inline
boost::asio::mutable_buffer
buffer_prefix(std::size_t n,
    boost::asio::mutable_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void*>(buffer),
        (std::min)(n, buffer_size(buffer)) };
}

/** Returns a prefix of a buffer sequence.

    This function returns a new buffer sequence which when iterated,
    presents a shorter subset of the original list of buffers starting
    with the first byte of the original sequence.

    @param n The maximum number of bytes in the wrapped
    sequence. If this is larger than the size of passed,
    buffers, the resulting sequence will represent the
    entire input sequence.

    @param buffers An instance of @b ConstBufferSequence or
    @b MutableBufferSequence to adapt. A copy of the sequence
    will be made, but ownership of the underlying memory is
    not transferred.
*/
template<class BufferSequence>
#if BEAST_DOXYGEN
buffer_prefix_view<BufferSequence>
#else
inline
typename std::enable_if<
    ! std::is_same<BufferSequence, boost::asio::const_buffer>::value &&
    ! std::is_same<BufferSequence, boost::asio::mutable_buffer>::value,
        buffer_prefix_view<BufferSequence>>::type
#endif
buffer_prefix(std::size_t n, BufferSequence const& buffers)
{
    static_assert(
        is_const_buffer_sequence<BufferSequence>::value ||
        is_mutable_buffer_sequence<BufferSequence>::value,
            "BufferSequence requirements not met");
    return buffer_prefix_view<BufferSequence>(n, buffers);
}

} // beast

#include <beast/core/impl/buffer_prefix.ipp>

#endif
