//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_IS_CALL_POSSIBLE_HPP
#define BEAST_DETAIL_IS_CALL_POSSIBLE_HPP

#include <type_traits>

namespace beast {
namespace detail {

template<class R, class C, class ...A>
auto
is_call_possible_test(C&& c, int, A&& ...a)
    -> decltype(std::is_convertible<
        decltype(c(a...)), R>::value ||
            std::is_same<R, void>::value,
                std::true_type());

template<class R, class C, class ...A>
std::false_type
is_call_possible_test(C&& c, long, A&& ...a);

/** Metafunction returns `true` if F callable as R(A...)
    Example:
        is_call_possible<T, void(std::string)>
*/
/** @{ */
template<class C, class F>
struct is_call_possible
    : std::false_type
{
};

template<class C, class R, class ...A>
struct is_call_possible<C, R(A...)>
    : decltype(is_call_possible_test<R>(
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

struct is_call_possible_udt3
{
    int operator()(int);
};

#ifndef __INTELLISENSE__
// VFALCO Fails to compile with Intellisense
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

static_assert(is_call_possible<
    is_call_possible_udt3, int(int)>::value, "");

static_assert(! is_call_possible<
    is_call_possible_udt3 const, int(int)>::value, "");
#endif

} // test

} // detail
} // beast

#endif
