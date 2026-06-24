// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/suite_info.h>

#include <string>

namespace beast::unit_test {

// Predicate for implementing matches
class Selector
{
public:
    enum class ModeT {
        // Run all tests except manual ones
        All,

        // Run tests that match in any field
        Automatch,

        // Match on suite
        Suite,

        // Match on library
        Library,

        // Match on module (used internally)
        Module,

        // Match nothing (used internally)
        None
    };

private:
    ModeT mode_;
    std::string pat_;
    std::string library_;

public:
    template <class = void>
    explicit Selector(ModeT mode, std::string pattern = "");

    template <class = void>
    bool
    operator()(SuiteInfo const& s);
};

//------------------------------------------------------------------------------

template <class>
Selector::Selector(ModeT mode, std::string pattern) : mode_(mode), pat_(std::move(pattern))
{
    if (mode_ == ModeT::Automatch && pat_.empty())
        mode_ = ModeT::All;
}

template <class>
bool
Selector::operator()(SuiteInfo const& s)
{
    switch (mode_)
    {
        case ModeT::Automatch:
            // suite or full name
            if (s.name() == pat_ || s.fullName() == pat_)
            {
                mode_ = ModeT::None;
                return true;
            }

            // check module
            if (pat_ == s.module())
            {
                mode_ = ModeT::Module;
                library_ = s.library();
                return !s.manual();
            }

            // check library
            if (pat_ == s.library())
            {
                mode_ = ModeT::Library;
                return !s.manual();
            }

            // check start of name
            if (s.name().starts_with(pat_) || s.fullName().starts_with(pat_))
            {
                // Don't change the mode so that the partial pattern can match
                // more than once
                return !s.manual();
            }

            return false;

        case ModeT::Suite:
            return pat_ == s.name();

        case ModeT::Module:
            return pat_ == s.module() && !s.manual();

        case ModeT::Library:
            return pat_ == s.library() && !s.manual();

        case ModeT::None:
            return false;

        case ModeT::All:
        default:
            break;
    };

    return !s.manual();
}

//------------------------------------------------------------------------------

// Utility functions for producing predicates to select suites.

/** Returns a predicate that implements a smart matching rule.
    The predicate checks the suite, module, and library fields of the
    SuiteInfo in that order. When it finds a match, it changes modes
    depending on what was found:

        If a suite is matched first, then only the suite is selected. The
        suite may be marked manual.

        If a module is matched first, then only suites from that module
        and library not marked manual are selected from then on.

        If a library is matched first, then only suites from that library
        not marked manual are selected from then on.

*/
inline Selector
matchAuto(std::string const& name)
{
    return Selector(Selector::ModeT::Automatch, name);
}

/** Return a predicate that matches all suites not marked manual. */
inline Selector
matchAll()
{
    return Selector(Selector::ModeT::All);
}

/** Returns a predicate that matches a specific suite. */
inline Selector
matchSuite(std::string const& name)
{
    return Selector(Selector::ModeT::Suite, name);
}

/** Returns a predicate that matches all suites in a library. */
inline Selector
matchLibrary(std::string const& name)
{
    return Selector(Selector::ModeT::Library, name);
}

}  // namespace beast::unit_test
