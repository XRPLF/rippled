// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/suite_info.h>

#include <string>

namespace beast::unit_test {

// Predicate for implementing matches
class selector
{
public:
    enum class mode_t {
        // Run all tests except manual ones
        all,

        // Run tests that match in any field
        automatch,

        // Match on suite
        suite,

        // Match on library
        library,

        // Match on module (used internally)
        module,

        // Match nothing (used internally)
        none
    };

private:
    mode_t mode_;
    std::string pat_;
    std::string library_;

public:
    template <class = void>
    explicit selector(mode_t mode, std::string const& pattern = "");

    template <class = void>
    bool
    operator()(suite_info const& s);
};

//------------------------------------------------------------------------------

template <class>
selector::selector(mode_t mode, std::string const& pattern) : mode_(mode), pat_(pattern)
{
    if (mode_ == mode_t::automatch && pattern.empty())
        mode_ = mode_t::all;
}

template <class>
bool
selector::operator()(suite_info const& s)
{
    switch (mode_)
    {
        case mode_t::automatch:
            // suite or full name
            if (s.name() == pat_ || s.full_name() == pat_)
            {
                mode_ = mode_t::none;
                return true;
            }

            // check module
            if (pat_ == s.module())
            {
                mode_ = mode_t::module;
                library_ = s.library();
                return !s.manual();
            }

            // check library
            if (pat_ == s.library())
            {
                mode_ = mode_t::library;
                return !s.manual();
            }

            // check start of name
            if (s.name().starts_with(pat_) || s.full_name().starts_with(pat_))
            {
                // Don't change the mode so that the partial pattern can match
                // more than once
                return !s.manual();
            }

            return false;

        case mode_t::suite:
            return pat_ == s.name();

        case mode_t::module:
            return pat_ == s.module() && !s.manual();

        case mode_t::library:
            return pat_ == s.library() && !s.manual();

        case mode_t::none:
            return false;

        case mode_t::all:
        default:
            break;
    };

    return !s.manual();
}

//------------------------------------------------------------------------------

// Utility functions for producing predicates to select suites.

/** Returns a predicate that implements a smart matching rule.
    The predicate checks the suite, module, and library fields of the
    suite_info in that order. When it finds a match, it changes modes
    depending on what was found:

        If a suite is matched first, then only the suite is selected. The
        suite may be marked manual.

        If a module is matched first, then only suites from that module
        and library not marked manual are selected from then on.

        If a library is matched first, then only suites from that library
        not marked manual are selected from then on.

*/
inline selector
match_auto(std::string const& name)
{
    return selector(selector::mode_t::automatch, name);
}

/** Return a predicate that matches all suites not marked manual. */
inline selector
match_all()
{
    return selector(selector::mode_t::all);
}

/** Returns a predicate that matches a specific suite. */
inline selector
match_suite(std::string const& name)
{
    return selector(selector::mode_t::suite, name);
}

/** Returns a predicate that matches all suites in a library. */
inline selector
match_library(std::string const& name)
{
    return selector(selector::mode_t::library, name);
}

}  // namespace beast::unit_test
