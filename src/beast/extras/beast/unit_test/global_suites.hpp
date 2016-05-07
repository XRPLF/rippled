//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_GLOBAL_SUITES_HPP
#define BEAST_UNIT_TEST_GLOBAL_SUITES_HPP

#include <beast/unit_test/suite_list.hpp>

namespace beast {
namespace unit_test {

namespace detail {

template <class = void>
suite_list&
global_suites()
{
    static suite_list s;
    return s;
}

template <class Suite>
struct insert_suite
{
    template <class = void>
    insert_suite (char const* name, char const* module,
        char const* library, bool manual);
};

template <class Suite>
template <class>
insert_suite<Suite>::insert_suite (char const* name,
    char const* module, char const* library, bool manual)
{
    global_suites().insert <Suite> (
        name, module, library, manual);
}

} // detail

/** Holds suites registered during static initialization. */
inline
suite_list const&
global_suites()
{
    return detail::global_suites();
}

} // unit_test
} // beast

#endif
