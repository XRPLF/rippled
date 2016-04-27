//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFERS_DEBUG_HPP
#define BEAST_BUFFERS_DEBUG_HPP

#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {
namespace debug {

/** Diagnostic utility to convert a `ConstBufferSequence` to a string.

    @note Carriage returns and linefeeds will have additional escape
    representations printed for visibility.
*/
template<class Buffers>
std::string
buffers_to_string(Buffers const& bs)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    std::string s;
    s.reserve(buffer_size(bs));
    for(auto const& b : bs)
        s.append(buffer_cast<char const*>(b),
            buffer_size(b));
    for(auto i = s.size(); i-- > 0;)
        if(s[i] == '\r')
            s.replace(i, 1, "\\r");
        else if(s[i] == '\n')
            s.replace(i, 1, "\\n\n");
    return s;
}

} // debug
} // beast

#endif
