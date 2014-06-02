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

#ifndef BEAST_UNIT_TEST_GLOBAL_SUITES_H_INCLUDED
#define BEAST_UNIT_TEST_GLOBAL_SUITES_H_INCLUDED

#include <beast/unit_test/suite_list.h>

namespace beast {
namespace unit_test {

namespace detail {

// Non const container is a detail, users are not allowed to modify!
inline
suite_list&
global_suites()
{
    static suite_list s;
    return s;
}

// Used to insert suites during static initialization
template <class Suite>
struct global_suite_instance
{
    global_suite_instance (char const* name, char const* module,
        char const* library, bool manual)
    {
        global_suites().insert <Suite> (
            name, module, library, manual);
    }
};

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
