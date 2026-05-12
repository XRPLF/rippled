// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/suite_list.h>

namespace beast::unit_test {

namespace detail {

/// Holds test suites registered during static initialization.
inline SuiteList&
globalSuites()
{
    static SuiteList kS;
    return kS;
}

template <class Suite>
struct InsertSuite
{
    InsertSuite(
        char const* name,
        char const* module,
        char const* library,
        bool manual,
        int priority)
    {
        globalSuites().insert<Suite>(name, module, library, manual, priority);
    }
};

}  // namespace detail

/// Holds test suites registered during static initialization.
inline SuiteList const&
globalSuites()
{
    return detail::globalSuites();
}

}  // namespace beast::unit_test
