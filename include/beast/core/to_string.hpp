//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TO_STRING_HPP
#define BEAST_TO_STRING_HPP

#include <beast/core/buffer_concepts.hpp>
#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {

/** Convert a @b `ConstBufferSequence` to a `std::string`.

    This function will convert the octets in a buffer sequence to a string.
    All octets will be inserted into the resulting string, including null
    or unprintable characters.

    @param buffers The buffer sequence to convert.

    @return A string representing the contents of the input area.

    @note This function participates in overload resolution only if
    the streambuf parameter meets the requirements of @b `Streambuf`.
*/
template<class ConstBufferSequence
#if ! GENERATING_DOCS
    ,class = std::enable_if<is_ConstBufferSequence<
        ConstBufferSequence>::value>
#endif
>
std::string
to_string(ConstBufferSequence const& buffers)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    std::string s;
    s.reserve(buffer_size(buffers));
    for(auto const& buffer : buffers)
        s.append(buffer_cast<char const*>(buffer),
            buffer_size(buffer));
    return s;
}

} // beast

#endif
