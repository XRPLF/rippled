//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_PREPARE_BUFFER_HPP
#define BEAST_PREPARE_BUFFER_HPP

#include <boost/asio/buffer.hpp>
#include <algorithm>

namespace beast {

/** Return a shortened buffer.

    The returned buffer points to the same memory as the
    passed buffer, but with a size that is equal to or less
    than the size of the original buffer.
    
    @param n The size of the returned buffer.

    @param buffer The buffer to shorten. Ownership of the
    underlying memory is not transferred.

    @return A new buffer that points to the first `n` bytes
    of the original buffer.
*/
inline
boost::asio::const_buffer
prepare_buffer(std::size_t n,
    boost::asio::const_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void const*>(buffer),
        (std::min)(n, buffer_size(buffer)) };
}

/** Return a shortened buffer.

    The returned buffer points to the same memory as the
    passed buffer, but with a size that is equal to or less
    than the size of the original buffer.
    
    @param n The size of the returned buffer.

    @param buffer The buffer to shorten. Ownership of the
    underlying memory is not transferred.

    @return A new buffer that points to the first `n` bytes
    of the original buffer.
*/
inline
boost::asio::mutable_buffer
prepare_buffer(std::size_t n,
    boost::asio::mutable_buffer buffer)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    return { buffer_cast<void*>(buffer),
        (std::min)(n, buffer_size(buffer)) };
}

} // beast

#endif
