//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_UTF8_CHECKER_HPP
#define BEAST_WEBSOCKET_DETAIL_UTF8_CHECKER_HPP

#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <string> // DEPRECATED

namespace beast {
namespace websocket {
namespace detail {

// Code adapted from
// http://bjoern.hoehrmann.de/utf-8/decoder/dfa/
/*
    Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject
    to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. *
*/
template<class = void>
class utf8_checker_t
{
    // Table for the UTF8 decode state machine
    using lut_type = std::uint8_t[400];
    static
    lut_type const&
    lut()
    {
        // 400 elements
        static std::uint8_t constexpr tab[] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
            7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
            8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
            0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
            0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
            0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
            1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
            1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
            1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1  // s7..s8
        };
        return tab;
    }

    std::uint32_t state_ = 0;
    std::uint32_t codepoint_ = 0;

public:
    void
    reset();

    // Returns `true` on success
    bool
    write(void const* buffer, std::size_t size);

    // Returns `true` on success
    template<class BufferSequence>
    bool
    write(BufferSequence const& bs);

    // Returns `true` on success
    bool
    finish();
};

template<class _>
void
utf8_checker_t<_>::reset()
{
    state_ = 0;
    codepoint_ = 0;
}

template<class _>
bool
utf8_checker_t<_>::write(void const* buffer, std::size_t size)
{
    auto p = static_cast<std::uint8_t const*>(buffer);
    auto plut = &lut()[0];
    while(size)
    {
        auto const byte = *p;
        auto const type = plut[byte];
        if(state_)
            codepoint_ = (byte & 0x3fu) | (codepoint_ << 6);
        else
            codepoint_ = (0xff >> type) & byte;
        state_ = plut[256 + state_ * 16 + type];
        if(state_ == 1)
        {
            reset();
            return false;
        }
        ++p;
        --size;
    }
    return true;
}

template<class _>
template<class BufferSequence>
bool
utf8_checker_t<_>::write(BufferSequence const& bs)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    for(auto const& b : bs)
        if(! write(buffer_cast<void const*>(b),
                buffer_size(b)))
            return false;
    return true;
}

template<class _>
bool
utf8_checker_t<_>::finish()
{
    auto const success = state_ == 0;
    reset();
    return success;
}

using utf8_checker = utf8_checker_t<>;

template<class = void>
bool
check_utf8(char const* p, std::size_t n)
{
    utf8_checker c;
    if(! c.write(p, n))
        return false;
    return c.finish();
}

} // detail
} // websocket
} // beast

#endif
