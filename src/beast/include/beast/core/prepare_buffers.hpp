//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_PREPARE_BUFFERS_HPP
#define BEAST_PREPARE_BUFFERS_HPP

#include <beast/config.hpp>
#include <beast/core/detail/prepare_buffers.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace beast {

/** Return a shortened buffer sequence.

    This function returns a new buffer sequence which adapts the
    passed buffer sequence and efficiently presents a shorter subset
    of the original list of buffers starting with the first byte of
    the original sequence.

    @param n The maximum number of bytes in the wrapped
    sequence. If this is larger than the size of passed,
    buffers, the resulting sequence will represent the
    entire input sequence.

    @param buffers The buffer sequence to adapt. A copy of
    the sequence will be made, but ownership of the underlying
    memory is not transferred.
*/
template<class BufferSequence>
#if GENERATING_DOCS
implementation_defined
#else
inline
detail::prepared_buffers<BufferSequence>
#endif
prepare_buffers(std::size_t n, BufferSequence const& buffers)
{
    return detail::prepared_buffers<BufferSequence>(n, buffers);
}

} // beast

#endif
