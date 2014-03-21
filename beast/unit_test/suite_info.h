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

#ifndef BEAST_UNIT_TEST_SUITE_INFO_H_INCLUDED
#define BEAST_UNIT_TEST_SUITE_INFO_H_INCLUDED

#include <functional>
#include <string>
#include <utility>

namespace beast {
namespace unit_test {

class runner;

/** Associates a unit test type with metadata. */
class suite_info
{
private:
    typedef std::function <void (runner&)> run_type;

    char const* m_name;
    char const* m_module;
    char const* m_library;
    bool m_manual;
    run_type m_run;

public:
    suite_info (
        char const* name,
        char const* module,
        char const* library,
        bool manual,
        run_type run)
        : m_name (name)
        , m_module (module)
        , m_library (library)
        , m_manual (manual)
        , m_run (std::move (run))
    {
    }

    char const*
    name() const
    {
        return m_name;
    }

    char const*
    module() const
    {
        return m_module;
    }

    char const*
    library() const
    {
        return m_library;
    }

    /** Returns `true` if this suite only runs manually. */
    bool
    manual() const
    {
        return m_manual;
    }

    /** Return the canonical suite name as a string. */
    std::string
    full_name() const
    {
        return 
            std::string (m_library) + "." +
            std::string (m_module) + "." +
            std::string (m_name);
    }

    /** Run a new instance of the associated test suite. */
    void
    run (runner& r) const
    {
        m_run (r);
    }
};

inline
bool
operator< (suite_info const& lhs, suite_info const& rhs)
{
    return lhs.full_name() < rhs.full_name();
}

/** Convenience for producing suite_info for a given test type. */
template <class Suite>
suite_info make_suite_info (char const* name, char const* module,
    char const* library, bool manual)
{
    return suite_info (name, module, library, manual,
        [](runner& r)
        {
            Suite test;
            return test (r);
        });
}

} // unit_test
} // beast

#endif
