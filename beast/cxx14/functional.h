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

#ifndef BEAST_CXX14_FUNCTIONAL_H_INCLUDED
#define BEAST_CXX14_FUNCTIONAL_H_INCLUDED

#include <beast/cxx14/config.h>

#include <functional>
#include <type_traits>
#include <utility>

#if ! BEAST_NO_CXX14_COMPATIBILITY

namespace std {

// C++14 implementation of std::equal_to <void> specialization.
// This supports heterogeneous comparisons.
template <>
struct equal_to <void>
{
    // VFALCO NOTE Its not clear how to support is_transparent pre c++14
    typedef std::true_type is_transparent;

    template <class T, class U>
    auto operator() (T&& lhs, U&& rhs) const ->
        decltype (std::forward <T> (lhs) == std::forward <U> (rhs))
    {
        return std::forward <T> (lhs) == std::forward <U> (rhs);
    }
};

}

#endif

#endif
