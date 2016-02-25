//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_ASIO_BUFFERS_DEBUG_H_INLUDED
#define BEAST_ASIO_BUFFERS_DEBUG_H_INLUDED

#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {
namespace debug {

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
