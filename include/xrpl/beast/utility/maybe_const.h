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

#ifndef BEAST_UTILITY_MAYBE_CONST_H_INCLUDED
#define BEAST_UTILITY_MAYBE_CONST_H_INCLUDED

#include <type_traits>

namespace beast {

/** Makes T const or non const depending on a bool. */
template <bool IsConst, class T>
struct maybe_const
{
    explicit maybe_const() = default;
    using type = typename std::conditional<
        IsConst,
        typename std::remove_const<T>::type const,
        typename std::remove_const<T>::type>::type;
};

/** Alias for omitting `typename`. */
template <bool IsConst, class T>
using maybe_const_t = typename maybe_const<IsConst, T>::type;

}  // namespace beast

#endif
