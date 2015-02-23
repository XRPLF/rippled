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

#ifndef BEAST_UNIT_TEST_SUITE_LIST_H_INCLUDED
#define BEAST_UNIT_TEST_SUITE_LIST_H_INCLUDED

#include <beast/unit_test/suite_info.h>

#include <beast/container/const_container.h>

#include <cassert>
//#include <list>
#include <typeindex>
#include <set>
#include <unordered_set>

namespace beast {
namespace unit_test {

/** A container of test suites. */
class suite_list
    : public const_container <std::set <suite_info>>
{
private:
#ifndef NDEBUG
    std::unordered_set <std::string> names_;
    std::unordered_set <std::type_index> classes_;
#endif

public:
    /** Insert a suite into the set.
        The suite must not already exist.
    */
    template <class Suite>
    void
    insert (char const* name, char const* module, char const* library,
        bool manual);
};

//------------------------------------------------------------------------------

template <class Suite>
void
suite_list::insert (char const* name, char const* module, char const* library,
    bool manual)
{
#ifndef NDEBUG
    {
        std::string s;
        s = std::string(library) + "." + module + "." + name;
        auto const result (names_.insert(s));
        assert (result.second); // Duplicate name
    }

    {
        auto const result (classes_.insert (
            std::type_index (typeid(Suite))));
        assert (result.second); // Duplicate type
    }
#endif

    cont().emplace (std::move (make_suite_info <Suite> (
        name, module, library, manual)));
}

} // unit_test
} // beast

#endif

