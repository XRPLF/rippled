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

namespace std {

#ifndef _MSC_VER

template<class...>
using void_t = void;

template<bool B>
using bool_constant = std::integral_constant<bool, B>;

#endif

// Ideas from Howard Hinnant
//
// Specializations of is_constructible for pair and tuple which
// work around an apparent defect in the standard that causes well
// formed expressions involving pairs or tuples of non default-constructible
// types to generate compile errors.
//
template <class T, class U>
struct is_constructible <pair <T, U>>
    : integral_constant <bool,
        is_default_constructible <T>::value &&
        is_default_constructible <U>::value>
{
    explicit is_constructible() = default;
};

} // std

#endif
