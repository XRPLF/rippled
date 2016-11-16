//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_BASE64_HPP
#define BEAST_DETAIL_BASE64_HPP

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <array>
#include <cctype>
#include <string>
#include <cstdint>
#include <vector>

/*
   Portions from http://www.adp-gmbh.ch/cpp/common/base64.html
   Copyright notice:

   base64.cpp and base64.h

   Copyright (C) 2004-2008 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

namespace ripple {
namespace cryptoconditions {

// NIKB NOTE: This has *NOT* standard base64 - it's base64url,
// which replaces the `+` with a `-` and the the `/` with a `_`
// with the padding suppressed on encoding and rejected on
// decoding.

template <class = void>
std::string const&
base64url_alphabet()
{
    static std::string const alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";
    return alphabet;
}

inline
bool
is_base64url(unsigned char c)
{
    return (std::isalnum(c) || (c == '-') || (c == '_'));
}

template <class = void>
std::string
base64url_encode (std::uint8_t const* data,
    std::size_t in_len)
{
    unsigned char c3[3], c4[4];
    int i = 0;
    int j = 0;

    std::string ret;
    ret.reserve (3 + in_len * 8 / 6);

    char const* alphabet (base64url_alphabet().data());

    while(in_len--)
    {
        c3[i++] = *(data++);
        if(i == 3)
        {
            c4[0] = (c3[0] & 0xfc) >> 2;
            c4[1] = ((c3[0] & 0x03) << 4) + ((c3[1] & 0xf0) >> 4);
            c4[2] = ((c3[1] & 0x0f) << 2) + ((c3[2] & 0xc0) >> 6);
            c4[3] = c3[2] & 0x3f;
            for(i = 0; (i < 4); i++)
                ret += alphabet[c4[i]];
            i = 0;
        }
    }

    if(i)
    {
        for(j = i; j < 3; j++)
            c3[j] = '\0';

        c4[0] = (c3[0] & 0xfc) >> 2;
        c4[1] = ((c3[0] & 0x03) << 4) + ((c3[1] & 0xf0) >> 4);
        c4[2] = ((c3[1] & 0x0f) << 2) + ((c3[2] & 0xc0) >> 6);
        c4[3] = c3[2] & 0x3f;

        for(j = 0; (j < i + 1); j++)
            ret += alphabet[c4[j]];
    }

    return ret;

}

template <size_t N>
std::string
base64url_encode (std::array<std::uint8_t, N> const& d)
{
    return base64url_encode (d.data(), d.size());
}

template <class = void>
std::string
base64url_encode (std::vector<std::uint8_t> const& d)
{
    return base64url_encode (d.data(), d.size());
}

template <class = void>
std::string
base64url_encode (Buffer const& d)
{
    return base64url_encode (d.data(), d.size());
}

template <class = void>
std::string
base64url_encode (Slice d)
{
    return base64url_encode (d.data(), d.size());
}

template <class = void>
std::vector<std::uint8_t>
base64url_decode(std::string const& data)
{
    int in_len = data.size();
    std::uint8_t c4[4];
    int i = 0;
    int j = 0;
    int in_ = 0;

    std::vector<std::uint8_t> ret;
    ret.reserve (in_len * 6 / 8);

    while(in_len-- && is_base64url(data[in_]))
    {
        c4[i++] = data[in_]; in_++;
        if(i == 4) {
            for(i = 0; i < 4; i++)
                c4[i] = static_cast<unsigned char>(
                    base64url_alphabet().find(c4[i]));

            ret.push_back ((c4[0] << 2) + ((c4[1] & 0x30) >> 4));
            ret.push_back (((c4[1] & 0xf) << 4) + ((c4[2] & 0x3c) >> 2));
            ret.push_back (((c4[2] & 0x3) << 6) + c4[3]);

            i = 0;
        }
    }

    if(i)
    {
        for(j = i; j < 4; j++)
            c4[j] = 0;

        for(j = 0; j < 4; j++)
            c4[j] = static_cast<unsigned char>(
                base64url_alphabet().find(c4[j]));

        std::uint8_t c3[3];

        c3[0] = (c4[0] << 2) + ((c4[1] & 0x30) >> 4);
        c3[1] = ((c4[1] & 0xf) << 4) + ((c4[2] & 0x3c) >> 2);
        c3[2] = ((c4[2] & 0x3) << 6) + c4[3];

        for(j = 0; (j < i - 1); j++)
            ret.push_back (c3[j]);
    }

    return ret;
}

}
}

#endif
