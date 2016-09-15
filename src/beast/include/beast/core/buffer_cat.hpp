//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFER_CAT_HPP
#define BEAST_BUFFER_CAT_HPP

#include <beast/core/detail/buffer_cat.hpp>
#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <iterator>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace beast {

/** Concatenate 2 or more buffer sequences to form a `ConstBufferSequence`.

    This function returns a @b `ConstBufferSequence` that when iterated,
    efficiently concatenates the input buffer sequences. Copies of the
    arguments passed will be made; however, the returned object does
    not take ownership of the underlying memory. The application is still
    responsible for managing the lifetime of the referenced memory.

    @param buffers The list of buffer sequences to concatenate.

    @return A new @b `ConstBufferSequence` that represents the
    concatenation of the input buffer sequences.
*/
#if GENERATING_DOCS
template<class... BufferSequence>
implementation_defined
buffer_cat(BufferSequence const&... buffers)
#else
template<class B1, class B2, class... Bn>
detail::buffer_cat_helper<
    boost::asio::const_buffer, B1, B2, Bn...>
buffer_cat(B1 const& b1, B2 const& b2, Bn const&... bn)
#endif
{
    return detail::buffer_cat_helper<
        boost::asio::const_buffer,
            B1, B2, Bn...>(b1, b2, bn...);
}

} // beast

#endif
