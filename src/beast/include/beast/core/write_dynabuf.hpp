//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WRITE_DYNABUF_HPP
#define BEAST_WRITE_DYNABUF_HPP

#include <beast/config.hpp>
#include <beast/core/buffer_concepts.hpp>
#include <beast/core/detail/write_dynabuf.hpp>
#include <type_traits>
#include <utility>

namespace beast {

/** Write to a @b `DynamicBuffer`.

    This function appends the serialized representation of each provided
    argument into the dynamic buffer. It is capable of converting the
    following types of arguments:

    @li `boost::asio::const_buffer`

    @li `boost::asio::mutable_buffer`

    @li A type meeting the requirements of @b `ConvertibleToConstBuffer`

    @li A type meeting the requirements of @b `ConstBufferSequence`

    @li A type meeting the requirements of @b `MutableBufferSequence`

    For all types not listed above, the function will invoke
    `boost::lexical_cast` on the argument in an attempt to convert to
    a string, which is then appended to the dynamic buffer.

    When this function serializes numbers, it converts them to
    their text representation as if by a call to `std::to_string`.

    @param dynabuf The dynamic buffer to write to.

    @param args A list of one or more arguments to write.

    @throws unspecified Any exceptions thrown by `boost::lexical_cast`.

    @note This function participates in overload resolution only if
    the `dynabuf` parameter meets the requirements of @b `DynamicBuffer`.
*/
template<class DynamicBuffer, class... Args>
#if GENERATING_DOCS
void
#else
typename std::enable_if<is_DynamicBuffer<DynamicBuffer>::value>::type
#endif
write(DynamicBuffer& dynabuf, Args const&... args)
{
    detail::write_dynabuf(dynabuf, args...);
}

} // beast

#endif
