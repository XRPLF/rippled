//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFER_CAT_HPP
#define BEAST_BUFFER_CAT_HPP

#include <beast/config.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <tuple>

namespace beast {

/** A buffer sequence representing a concatenation of buffer sequences.

    @see @ref buffer_cat
*/
template<class... Buffers>
class buffer_cat_view
{
#if 0
    static_assert(
        detail::is_all_const_buffer_sequence<Buffers...>::value,
            "BufferSequence requirements not met");
#endif

    std::tuple<Buffers...> bn_;

public:
    /** The type of buffer returned when dereferencing an iterator.

        If every buffer sequence in the view is a @b MutableBufferSequence,
        then `value_type` will be `boost::asio::mutable_buffer`.
        Otherwise, `value_type` will be `boost::asio::const_buffer`.
    */
    using value_type =
    #if BEAST_DOXYGEN
        implementation_defined;
    #else
        typename detail::common_buffers_type<Buffers...>::type;
    #endif

    /// The type of iterator used by the concatenated sequence
    class const_iterator;

    /// Move constructor
    buffer_cat_view(buffer_cat_view&&) = default;

    /// Copy constructor
    buffer_cat_view(buffer_cat_view const&) = default;

    /// Move assignment
    buffer_cat_view& operator=(buffer_cat_view&&) = default;

    // Copy assignment
    buffer_cat_view& operator=(buffer_cat_view const&) = default;

    /** Constructor

        @param buffers The list of buffer sequences to concatenate.
        Copies of the arguments will be made; however, the ownership
        of memory is not transferred.
    */
    explicit
    buffer_cat_view(Buffers const&... buffers);

    /// Return an iterator to the beginning of the concatenated sequence.
    const_iterator
    begin() const;

    /// Return an iterator to the end of the concatenated sequence.
    const_iterator
    end() const;
};

/** Concatenate 2 or more buffer sequences.

    This function returns a constant or mutable buffer sequence which,
    when iterated, efficiently concatenates the input buffer sequences.
    Copies of the arguments passed will be made; however, the returned
    object does not take ownership of the underlying memory. The
    application is still responsible for managing the lifetime of the
    referenced memory.

    @param buffers The list of buffer sequences to concatenate.

    @return A new buffer sequence that represents the concatenation of
    the input buffer sequences. This buffer sequence will be a
    @b MutableBufferSequence if each of the passed buffer sequences is
    also a @b MutableBufferSequence; otherwise the returned buffer
    sequence will be a @b ConstBufferSequence.

    @see @ref buffer_cat_view
*/
#if BEAST_DOXYGEN
template<class... BufferSequence>
buffer_cat_view<BufferSequence...>
buffer_cat(BufferSequence const&... buffers)
#else
template<class B1, class B2, class... Bn>
inline
buffer_cat_view<B1, B2, Bn...>
buffer_cat(B1 const& b1, B2 const& b2, Bn const&... bn)
#endif
{
    static_assert(
        detail::is_all_const_buffer_sequence<B1, B2, Bn...>::value,
            "BufferSequence requirements not met");
    return buffer_cat_view<B1, B2, Bn...>{b1, b2, bn...};
}

} // beast

#include <beast/core/impl/buffer_cat.ipp>

#endif
