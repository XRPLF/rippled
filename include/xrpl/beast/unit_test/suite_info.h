//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_SUITE_INFO_HPP
#define BEAST_UNIT_TEST_SUITE_INFO_HPP

#include <cstring>
#include <functional>
#include <string>
#include <utility>

namespace beast {
namespace unit_test {

class runner;

/** Associates a unit test type with metadata. */
class suite_info
{
    using run_type = std::function<void(runner&)>;

    std::string name_;
    std::string module_;
    std::string library_;
    bool manual_;
    int priority_;
    run_type run_;

public:
    suite_info(
        std::string name,
        std::string module,
        std::string library,
        bool manual,
        int priority,
        run_type run)
        : name_(std::move(name))
        , module_(std::move(module))
        , library_(std::move(library))
        , manual_(manual)
        , priority_(priority)
        , run_(std::move(run))
    {
    }

    std::string const&
    name() const
    {
        return name_;
    }

    std::string const&
    module() const
    {
        return module_;
    }

    std::string const&
    library() const
    {
        return library_;
    }

    /// Returns `true` if this suite only runs manually.
    bool
    manual() const
    {
        return manual_;
    }

    /// Return the canonical suite name as a string.
    std::string
    full_name() const
    {
        return library_ + "." + module_ + "." + name_;
    }

    /// Run a new instance of the associated test suite.
    void
    run(runner& r) const
    {
        run_(r);
    }

    friend bool
    operator<(suite_info const& lhs, suite_info const& rhs)
    {
        // we want higher priority suites sorted first, thus the negation
        // of priority value here
        return std::forward_as_tuple(
                   -lhs.priority_, lhs.library_, lhs.module_, lhs.name_) <
            std::forward_as_tuple(
                   -rhs.priority_, rhs.library_, rhs.module_, rhs.name_);
    }
};

//------------------------------------------------------------------------------

/// Convenience for producing suite_info for a given test type.
template <class Suite>
suite_info
make_suite_info(
    std::string name,
    std::string module,
    std::string library,
    bool manual,
    int priority)
{
    return suite_info(
        std::move(name),
        std::move(module),
        std::move(library),
        manual,
        priority,
        [](runner& r) { Suite{}(r); });
}

}  // namespace unit_test
}  // namespace beast

#endif
