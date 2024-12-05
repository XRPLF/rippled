//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_COMPARATORS_H_INCLUDED
#define RIPPLE_BASICS_COMPARATORS_H_INCLUDED

#include <functional>

namespace ripple {

#ifdef _MSC_VER

/*
 * MSVC 2019 version 16.9.0 added [[nodiscard]] to the std comparison
 * operator() functions. boost::bimap checks that the comparitor is a
 * BinaryFunction, in part by calling the function and ignoring the value.
 * These two things don't play well together. These wrapper classes simply
 * strip [[nodiscard]] from operator() for use in boost::bimap.
 *
 * See also:
 *  https://www.boost.org/doc/libs/1_75_0/libs/bimap/doc/html/boost_bimap/the_tutorial/controlling_collection_types.html
 */

template <class T = void>
struct less
{
    using result_type = bool;

    constexpr bool
    operator()(const T& left, const T& right) const
    {
        return std::less<T>()(left, right);
    }
};

template <class T = void>
struct equal_to
{
    using result_type = bool;

    constexpr bool
    operator()(const T& left, const T& right) const
    {
        return std::equal_to<T>()(left, right);
    }
};

#else

template <class T = void>
using less = std::less<T>;

template <class T = void>
using equal_to = std::equal_to<T>;

#endif

}  // namespace ripple

#endif
