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

#ifndef BEAST_TYPE_TRAITS_REMOVESIGNED_H_INCLUDED
#define BEAST_TYPE_TRAITS_REMOVESIGNED_H_INCLUDED

#include "IntegralConstant.h"
#include "../CStdInt.h"

namespace beast {

/** Returns an equally sized, unsigned type.
    Requires:
        IsIntegral<T>::value == true
*/
template <typename T>
struct RemoveSigned
{
    typedef T type;
};

template <> struct RemoveSigned <int8>   { typedef uint8  type; };
template <> struct RemoveSigned <int16>  { typedef uint16 type; };
template <> struct RemoveSigned <int32>  { typedef uint32 type; };
template <> struct RemoveSigned <int64>  { typedef uint64 type; };

}

#endif
