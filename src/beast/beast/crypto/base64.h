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

#ifndef BEAST_CRYPTO_BASE64_H_INCLUDED
#define BEAST_CRYPTO_BASE64_H_INCLUDED

#include <cctype>
#include <string>

namespace beast {

/*
   Portions from http://www.adp-gmbh.ch/cpp/common/base64.html
   Copyright notice:

   base64.cpp and base64.h

   Copyright (C) 2004-2008 Ren� Nyffenegger

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

   Ren� Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

template <class = void>
std::string const&
base64_alphabet()
{
    static std::string const alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    return alphabet;
}

inline
bool
is_base64(unsigned char c)
{
    return (std::isalnum(c) || (c == '+') || (c == '/'));
}

template <class = void>
std::string
base64_encode (std::uint8_t const* data,
    std::size_t in_len)
{
    unsigned char c3[3], c4[4];
    int i = 0;
    int j = 0;

    std::string ret;
    ret.reserve (3 + in_len * 8 / 6);

    char const* alphabet (base64_alphabet().data());

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

        while((i++ < 3))
            ret += '=';
    }

    return ret;

}

template <class = void>
std::string
base64_encode (std::string const& s)
{
    return base64_encode (reinterpret_cast <
        std::uint8_t const*> (s.data()), s.size());
}

template <class = void>
std::string
base64_decode(std::string const& data)
{
    int in_len = data.size();
    unsigned char c3[3], c4[4];
    int i = 0;
    int j = 0;
    int in_ = 0;

    std::string ret;
    ret.reserve (in_len * 6 / 8); // ???

    while(in_len-- && (data[in_] != '=') &&
        is_base64(data[in_]))
    {
        c4[i++] = data[in_]; in_++;
        if(i == 4) {
            for(i = 0; i < 4; i++)
                c4[i] = base64_alphabet().find(c4[i]);

            c3[0] = (c4[0] << 2) + ((c4[1] & 0x30) >> 4);
            c3[1] = ((c4[1] & 0xf) << 4) + ((c4[2] & 0x3c) >> 2);
            c3[2] = ((c4[2] & 0x3) << 6) + c4[3];

            for(i = 0; (i < 3); i++)
                ret += c3[i];
            i = 0;
        }
    }

    if(i)
    {
        for(j = i; j < 4; j++)
            c4[j] = 0;

        for(j = 0; j < 4; j++)
            c4[j] = base64_alphabet().find(c4[j]);

        c3[0] = (c4[0] << 2) + ((c4[1] & 0x30) >> 4);
        c3[1] = ((c4[1] & 0xf) << 4) + ((c4[2] & 0x3c) >> 2);
        c3[2] = ((c4[2] & 0x3) << 6) + c4[3];

        for(j = 0; (j < i - 1); j++)
            ret += c3[j];
    }

    return ret;
}

}

#endif
