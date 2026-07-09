// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <functional>
#include <string>
#include <tuple>
#include <utility>

namespace beast::unit_test {

class Runner;

/** Associates a unit test type with metadata. */
class SuiteInfo
{
    using run_type = std::function<void(Runner&)>;

    std::string name_;
    std::string module_;
    std::string library_;
    bool manual_;
    int priority_;
    run_type run_;

public:
    SuiteInfo(
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

    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    [[nodiscard]] std::string const&
    module() const
    {
        return module_;
    }

    [[nodiscard]] std::string const&
    library() const
    {
        return library_;
    }

    /// Returns `true` if this suite only runs manually.
    [[nodiscard]] bool
    manual() const
    {
        return manual_;
    }

    /// Return the canonical suite name as a string.
    [[nodiscard]] std::string
    fullName() const
    {
        return library_ + "." + module_ + "." + name_;
    }

    /// Run a new instance of the associated test suite.
    void
    run(Runner& r) const
    {
        run_(r);
    }

    friend bool
    operator<(SuiteInfo const& lhs, SuiteInfo const& rhs)
    {
        // we want higher priority suites sorted first, thus the negation
        // of priority value here
        return std::forward_as_tuple(-lhs.priority_, lhs.library_, lhs.module_, lhs.name_) <
            std::forward_as_tuple(-rhs.priority_, rhs.library_, rhs.module_, rhs.name_);
    }
};

//------------------------------------------------------------------------------

/// Convenience for producing SuiteInfo for a given test type.
template <class Suite>
SuiteInfo
makeSuiteInfo(std::string name, std::string module, std::string library, bool manual, int priority)
{
    return SuiteInfo(
        std::move(name), std::move(module), std::move(library), manual, priority, [](Runner& r) {
            Suite{}(r);
        });
}

}  // namespace beast::unit_test
