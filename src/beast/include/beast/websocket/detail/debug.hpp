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

#ifndef BEAST_WEBSOCKET_DETAIL_DEBUG_HPP
#define BEAST_WEBSOCKET_DETAIL_DEBUG_HPP

#include <boost/asio/buffer.hpp>
#include <iomanip>
#include <sstream>
#include <string>

namespace beast {
namespace websocket {
namespace detail {

template<class = void>
std::string
to_hex(boost::asio::const_buffer b)
{
    using namespace boost::asio;
    std::stringstream ss;
    auto p = buffer_cast<std::uint8_t const*>(b);
    auto n = buffer_size(b);
    while(n--)
    {
        ss <<
            std::setfill('0') <<
            std::setw(2) <<
            std::hex << int(*p++) << " ";
    }
    return ss.str();
}

template<class Buffers>
std::string
to_hex(Buffers const& bs)
{
    std::string s;
    for(auto const& b : bs)
        s.append(to_hex(boost::asio::const_buffer(b)));
    return s;
}

template<class Buffers>
std::string
buffers_to_string(Buffers const& bs)
{
    using namespace boost::asio;
    std::string s;
    s.reserve(buffer_size(bs));
    for(auto const& b : bs)
        s.append(buffer_cast<char const*>(b),
            buffer_size(b));
    return s;
}

template<class = void>
std::string
format(std::string s)
{
    auto const w = 84;
    for(int n = w*(s.size()/w); n>0; n-=w)
        s.insert(n, 1, '\n');
    return s;
}

} // detail
} // websocket
} // beast

#endif
