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

#ifndef BEAST_CXX17_TYPE_TRAITS_H_INCLUDED
#define BEAST_CXX17_TYPE_TRAITS_H_INCLUDED

#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 6000

namespace std {

template <class _Ret, class _Fn, class... _Args>
struct _LIBCPP_TEMPLATE_VIS is_invocable_r
    : integral_constant<bool, __invokable_r<_Ret, _Fn, _Args...>::value>
{
};

}  // namespace std

#endif

#endif
