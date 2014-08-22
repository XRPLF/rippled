//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Howard Hinnant <howard.hinnant@gmail.com>

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

#ifndef BEAST_UTILITY_META_H_INCLUDED
#define BEAST_UTILITY_META_H_INCLUDED

#include <beast/cxx14/type_traits.h> // <type_traits>

namespace beast {

template <bool ...> struct static_and;

template <bool b0, bool ... bN>
struct static_and <b0, bN...>
    : public std::integral_constant <
        bool, b0 && static_and<bN...>::value>
{
};

template <>
struct static_and<>
    : public std::true_type
{
};

static_assert( static_and<true, true, true>::value, "");
static_assert(!static_and<true, false, true>::value, "");

template <std::size_t ...>
struct static_sum;

template <std::size_t s0, std::size_t ...sN>
struct static_sum <s0, sN...>
    : public std::integral_constant <
        std::size_t, s0 + static_sum<sN...>::value>
{
};

template <>
struct static_sum<>
    : public std::integral_constant<std::size_t, 0>
{
};

static_assert(static_sum<5, 2, 17, 0>::value == 24, "");

template <class T, class U>
struct enable_if_lvalue
    : public std::enable_if
    <
    std::is_same<std::decay_t<T>, U>::value &&
    std::is_lvalue_reference<T>::value
    >
{
};

/** Ensure const reference function parameters are valid lvalues.
 
    Some functions, especially class constructors, accept const references and
    store them for later use. If any of those parameters are rvalue objects, 
    the object will be freed as soon as the function returns. This could 
    potentially lead to a variety of "use after free" errors.
 
    If the function is rewritten as a template using this type and the 
    parameters references as rvalue references (eg. TX&&), a compiler
    error will be generated if an rvalue is provided in the caller.
 
    @code
        // Example:
        struct X
        {
        };
        struct Y
        {
        };

        struct Unsafe
        {
            Unsafe (X const& x, Y const& y)
                : x_ (x)
                , y_ (y)
            {
            }

            X const& x_;
            Y const& y_;
        };

        struct Safe
        {
            template <class TX, class TY,
            class = beast::enable_if_lvalue_t<TX, X>,
            class = beast::enable_if_lvalue_t < TY, Y >>
                Safe (TX&& x, TY&& y)
                : x_ (x)
                , y_ (y)
            {
            }

            X const& x_;
            Y const& y_;
        };

        struct demo
        {
            void
                createObjects ()
            {
                X x {};
                Y const y {};
                Unsafe u1 (x, y);    // ok
                Unsafe u2 (X (), y);  // compiles, but u2.x_ becomes invalid at the end of the line.
                Safe s1 (x, y);      // ok
                //  Safe s2 (X (), y);  // compile-time error
            }
        };
    @endcode
*/
template <class T, class U>
using enable_if_lvalue_t = typename enable_if_lvalue<T, U>::type;

} // beast

#endif // BEAST_UTILITY_META_H_INCLUDED
