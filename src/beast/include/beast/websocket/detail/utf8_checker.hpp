//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_UTF8_CHECKER_HPP
#define BEAST_WEBSOCKET_DETAIL_UTF8_CHECKER_HPP

#include <beast/core/type_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <cstdint>

namespace beast {
namespace websocket {
namespace detail {

/*  This is a modified work.

    Original version and license:
        https://www.cl.cam.ac.uk/~mgk25/ucs/utf8_check.c
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

    Additional changes:
        Optimized for predominantly 7-bit content, 2016
        https://github.com/uWebSockets/uWebSockets/blob/755bd362649c06abff102f18e273c5792c51c1a0/src/WebSocketProtocol.h#L198
    Copyright (c) 2016 Alex Hultman and contributors

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgement in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
*/

/** A UTF8 validator.

    This validator can be used to check if a buffer containing UTF8 text is
    valid. The write function may be called incrementally with segmented UTF8
    sequences. The finish function determines if all processed text is valid.
*/
template<class = void>
class utf8_checker_t
{
    std::size_t need_ = 0;
    std::uint8_t* p_ = have_;
    std::uint8_t have_[4];

public:
    /** Prepare to process text as valid utf8
    */
    void
    reset();

    /** Check that all processed text is valid utf8
    */
    bool
    finish();

    /** Check if text is valid UTF8

        @return `true` if the text is valid utf8 or false otherwise.
    */
    bool
    write(std::uint8_t const* in, std::size_t size);

    /** Check if text is valid UTF8

        @return `true` if the text is valid utf8 or false otherwise.
    */
    template<class ConstBufferSequence>
    bool
    write(ConstBufferSequence const& bs);
};

template<class _>
void
utf8_checker_t<_>::
reset()
{
    need_ = 0;
    p_ = have_;
}

template<class _>
bool
utf8_checker_t<_>::
finish()
{
    auto const success = need_ == 0;
    reset();
    return success;
}

template<class _>
template<class ConstBufferSequence>
bool
utf8_checker_t<_>::
write(ConstBufferSequence const& bs)
{
    static_assert(is_const_buffer_sequence<ConstBufferSequence>::value,
        "ConstBufferSequence requirements not met");
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    for(boost::asio::const_buffer b : bs)
        if(! write(buffer_cast<std::uint8_t const*>(b),
                buffer_size(b)))
            return false;
    return true;
}

template<class _>
bool
utf8_checker_t<_>::
write(std::uint8_t const* in, std::size_t size)
{
    auto const valid =
        [](std::uint8_t const*& p)
        {
            if (p[0] < 128)
            {
                ++p;
                return true;
            }
            if ((p[0] & 0x60) == 0x40)
            {
                if ((p[1] & 0xc0) != 0x80)
                    return false;
                p += 2;
                return true;
            }
            if ((p[0] & 0xf0) == 0xe0)
            {
                if ((p[1] & 0xc0) != 0x80 ||
                    (p[2] & 0xc0) != 0x80 ||
                    (p[0] == 224 && p[1] < 160) ||
                    (p[0] == 237 && p[1] > 159))
                        return false;
                p += 3;
                return true;
            }
            if ((p[0] & 0xf8) == 0xf0)
            {
                if (p[0] > 244 ||
                    (p[1] & 0xc0) != 0x80 ||
                    (p[2] & 0xc0) != 0x80 ||
                    (p[3] & 0xc0) != 0x80 ||
                    (p[0] == 240 && p[1] < 144) ||
                    (p[0] == 244 && p[1] > 143))
                        return false;
                p += 4;
                return true;
            }
            return false;
        };
    auto const valid_have =
        [&]()
        {
            if ((have_[0] & 0x60) == 0x40)
                return have_[0] <= 223;
            if ((have_[0] & 0xf0) == 0xe0)
            {
                if (p_ - have_ > 1 &&
                    ((have_[1] & 0xc0) != 0x80 ||
                    (have_[0] == 224 && have_[1] < 160) ||
                    (have_[0] == 237 && have_[1] > 159)))
                        return false;
                return true;
            }
            if ((have_[0] & 0xf8) == 0xf0)
            {
                auto const n = p_ - have_;
                if (n > 2 && (have_[2] & 0xc0) != 0x80)
                    return false;
                if (n > 1 &&
                    ((have_[1] & 0xc0) != 0x80 ||
                    (have_[0] == 240 && have_[1] < 144) ||
                    (have_[0] == 244 && have_[1] > 143)))
                        return false;
            }
            return true;
        };
    auto const needed =
        [](std::uint8_t const v)
        {
            if (v < 128)
                return 1;
            if (v < 194)
                return 0;
            if (v < 224)
                return 2;
            if (v < 240)
                return 3;
            if (v < 245)
                return 4;
            return 0;
        };

    auto const end = in + size;
    if (need_ > 0)
    {
        auto n = (std::min)(size, need_);
        size -= n;
        need_ -= n;
        while(n--)
            *p_++ = *in++;
        if(need_ > 0)
        {
            BOOST_ASSERT(in == end);
            return valid_have();
        }
        std::uint8_t const* p = &have_[0];
        if (! valid(p))
            return false;
        p_ = have_;
    }

    if(size <= sizeof(std::size_t))
        goto slow;

    // align in to sizeof(std::size_t) boundary
    {
        auto const in0 = in;
        auto last = reinterpret_cast<std::uint8_t const*>(
            ((reinterpret_cast<std::uintptr_t>(in) + sizeof(std::size_t) - 1) /
                sizeof(std::size_t)) * sizeof(std::size_t));
        while(in < last)
        {
            if(*in & 0x80)
            {
                size = size - (in - in0);
                goto slow;
            }
            ++in;
        }
        size = size - (in - in0);
    }

    // fast loop
    {
        auto const in0 = in;
        auto last = in + size - 7;
        auto constexpr mask = static_cast<
            std::size_t>(0x8080808080808080 & ~std::size_t{0});
        while(in < last)
        {
#if 0
            std::size_t temp;
            std::memcpy(&temp, in, sizeof(temp));
            if((temp & mask) != 0)
#else
            // Technically UB but works on all known platforms
            if((*reinterpret_cast<std::size_t const*>(in) & mask) != 0)
#endif
            {
                size = size - (in - in0);
                goto slow;
            }
            in += sizeof(std::size_t);
        }
        last += 4;
        while(in < last)
            if(! valid(in))
                return false;
        goto tail;
    }

    // slow loop: one code point at a time
slow:
    {
        auto last = in + size - 3;
        while(in < last)
            if(! valid(in))
                return false;
    }

tail:
    for(;;)
    {
        auto n = end - in;
        if(! n)
            break;
        auto const need = needed(*in);
        if (need == 0)
            return false;
        if(need <= n)
        {
            if(! valid(in))
                return false;
        }
        else
        {
            need_ = need - n;
            while(n--)
                *p_++ = *in++;
            return valid_have();
        }
    }
    return true;
}

using utf8_checker = utf8_checker_t<>;

template<class = void>
bool
check_utf8(char const* p, std::size_t n)
{
    utf8_checker c;
    if(! c.write(reinterpret_cast<const uint8_t*>(p), n))
        return false;
    return c.finish();
}

} // detail
} // websocket
} // beast

#endif
