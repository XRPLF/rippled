//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Howard Hinnant <howard.hinnant@gmail.com>,
        Vinnie Falco <vinnie.falco@gmail.com

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

#ifndef BEAST_HASH_ENDIAN_H_INCLUDED
#define BEAST_HASH_ENDIAN_H_INCLUDED

namespace beast {

// endian provides answers to the following questions:
// 1.  Is this system big or little endian?
// 2.  Is the "desired endian" of some class or function the same as the
//     native endian?
enum class endian
{
#ifdef _MSC_VER
    big    = 1,
    little = 0,
    native = little
#else
    native = __BYTE_ORDER__,
    little = __ORDER_LITTLE_ENDIAN__,
    big    = __ORDER_BIG_ENDIAN__
#endif
};

#ifndef __INTELLISENSE__
static_assert(endian::native == endian::little ||
              endian::native == endian::big,
              "endian::native shall be one of endian::little or endian::big");

static_assert(endian::big != endian::little,
              "endian::big and endian::little shall have different values");
#endif

} // beast

#endif
