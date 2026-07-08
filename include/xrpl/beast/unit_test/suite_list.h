// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/detail/const_container.h>
#include <xrpl/beast/unit_test/suite_info.h>

#include <boost/assert.hpp>

#include <set>
#include <string>
#include <typeindex>
#include <unordered_set>

namespace beast::unit_test {

/// A container of test suites.
class SuiteList : public detail::ConstContainer<std::set<SuiteInfo>>
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
    template <class Suite>
    void
    insert(char const* name, char const* module, char const* library, bool manual, int priority);
};

//------------------------------------------------------------------------------

template <class Suite>
void
SuiteList::insert(
    char const* name,
    char const* module,
    char const* library,
    bool manual,
    int priority)
{
#ifndef NDEBUG
    {
        std::string s;
        s = std::string(library) + "." + module + "." + name;
        auto const result(names_.insert(s));
        BOOST_ASSERT(result.second);  // Duplicate name
    }

    {
        auto const result(classes_.insert(std::type_index(typeid(Suite))));
        BOOST_ASSERT(result.second);  // Duplicate type
    }
#endif
    cont().emplace(makeSuiteInfo<Suite>(name, module, library, manual, priority));
}

}  // namespace beast::unit_test
