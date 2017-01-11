//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_SUITE_LIST_HPP
#define BEAST_UNIT_TEST_SUITE_LIST_HPP

#include <beast/unit_test/suite_info.hpp>
#include <beast/unit_test/detail/const_container.hpp>
#include <boost/assert.hpp>
#include <typeindex>
#include <set>
#include <unordered_set>

namespace beast {
namespace unit_test {

/// A container of test suites.
class suite_list
    : public detail::const_container <std::set <suite_info>>
{
private:
#ifndef NDEBUG
    std::unordered_set<std::string> names_;
    std::unordered_set<std::type_index> classes_;
#endif

public:
    /** Insert a suite into the set.

        The suite must not already exist.
    */
    template<class Suite>
    void
    insert(
        char const* name,
        char const* module,
        char const* library,
        bool manual);
};

//------------------------------------------------------------------------------

template<class Suite>
void
suite_list::insert(
    char const* name,
    char const* module,
    char const* library,
    bool manual)
{
#ifndef NDEBUG
    {
        std::string s;
        s = std::string(library) + "." + module + "." + name;
        auto const result(names_.insert(s));
        BOOST_ASSERT(result.second); // Duplicate name
    }

    {
        auto const result(classes_.insert(
            std::type_index(typeid(Suite))));
        BOOST_ASSERT(result.second); // Duplicate type
    }
#endif
    cont().emplace(make_suite_info<Suite>(
        name, module, library, manual));
}

} // unit_test
} // beast

#endif

