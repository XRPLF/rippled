//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/detail/is_call_possible.hpp>

namespace beast {
namespace detail {
namespace {

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

}
} // detail
} // beast
