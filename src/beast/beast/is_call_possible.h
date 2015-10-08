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

#ifndef BEAST_IS_CALL_POSSIBLE_H_INCLUDED
#define BEAST_IS_CALL_POSSIBLE_H_INCLUDED

#include <type_traits>

namespace beast {

namespace detail {

template <class R, class C, class ...A>
auto
is_call_possible_test(C&& c, int, A&& ...a)
    -> decltype(std::is_convertible<
        decltype(c(a...)), R>::value ||
            std::is_same<R, void>::value,
                std::true_type());

template <class R, class C, class ...A>
std::false_type
is_call_possible_test(C&& c, long, A&& ...a);

} // detail

/** Metafunction returns `true` if F callable as R(A...)
    Example:
        is_call_possible<T, void(std::string)>
*/
/** @{ */
template <class C, class F>
struct is_call_possible
    : std::false_type
{
};

template <class C, class R, class ...A>
struct is_call_possible<C, R(A...)>
    : decltype(detail::is_call_possible_test<R>(
        std::declval<C>(), 1, std::declval<A>()...))
{
};
/** @} */

namespace test {

struct is_call_possible_udt1
{
    void operator()(int) const;
};

struct is_call_possible_udt2
{
    int operator()(int) const;
};

static_assert(is_call_possible<
    is_call_possible_udt1, void(int)>::value, "");

static_assert(! is_call_possible<
    is_call_possible_udt1, void(void)>::value, "");

static_assert(is_call_possible<
    is_call_possible_udt2, int(int)>::value, "");

static_assert(! is_call_possible<
    is_call_possible_udt2, int(void)>::value, "");

static_assert(! is_call_possible<
    is_call_possible_udt2, void(void)>::value, "");

}

}

#endif
