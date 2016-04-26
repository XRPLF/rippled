//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WRITE_STREAMBUF_HPP
#define BEAST_WRITE_STREAMBUF_HPP

#include <beast/type_check.hpp>
#include <beast/detail/write_streambuf.hpp>
#include <type_traits>
#include <utility>

namespace beast {

/** Write to a Streambuf.

    This function appends the serialized representation of each provided
    argument into the stream buffer. It is capable of converting the
    following types of arguments:

    * `boost::asio::const_buffer`
    * `boost::asio::mutable_buffer`
    * A type for which the call to `boost::asio::buffer()` is defined
    * A type meeting the requirements of `ConstBufferSequence`
    * A type meeting the requirements of `MutableBufferSequence`

    For all types not listed above, the function will invoke
    `boost::lexical_cast` on the argument in an attempt to convert to
    a string, which is then appended to the stream buffer.

    When this function serializes numbers, it converts them to
    their text representation as if by a call to `std::to_string`.

    @param streambuf The stream buffer to write to.

    @param args A list of one or more arguments to write.

    @throws Any exceptions thrown by `boost::lexical_cast`.

    @note This function participates in overload resolution only if
    the `streambuf` parameter meets the requirements of Streambuf.
*/
template<class Streambuf, class... Args>
#if GENERATING_DOCS
void
#else
typename std::enable_if<is_Streambuf<Streambuf>::value>::type
#endif
write(Streambuf& streambuf, Args&&... args)
{
    detail::write_streambuf(streambuf, std::forward<Args>(args)...);
}

} // beast

#endif
