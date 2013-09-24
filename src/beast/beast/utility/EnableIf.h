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

#ifndef BEAST_UTILITY_ENABLEIF_H_INCLUDED
#define BEAST_UTILITY_ENABLEIF_H_INCLUDED

#include "../type_traits/IntegralConstant.h"

namespace beast
{

template <bool Enable, class T = void>
struct EnableIfBool : TrueType { typedef T type; };

template <class T>
struct EnableIfBool <false, T> : FalseType { };

template <class Cond, class T = void>
struct EnableIf : public EnableIfBool <Cond::value, T> { };

template <bool Enable, class T = void>
struct DisableIfBool : FalseType { typedef T type; };

template <class T>
struct DisableIfBool <true, T> { };

template <class Cond, class T = void>
struct DisableIf : public DisableIfBool <Cond::value, T> { };

}

#endif
