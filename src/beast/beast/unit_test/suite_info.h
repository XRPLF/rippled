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
    using run_type = std::function <void (runner&)>;

    std::string name_;
    std::string module_;
    std::string library_;
    bool m_manual;
    run_type m_run;

public:
    template <class = void>
    suite_info (std::string const& name, std::string const& module,
        std::string const& library, bool manual, run_type run);

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

    /** Returns `true` if this suite only runs manually. */
    bool
    manual() const
    {
        return m_manual;
    }

    /** Return the canonical suite name as a string. */
    template <class = void>
    std::string
    full_name() const;

    /** Run a new instance of the associated test suite. */
    void
    run (runner& r) const
    {
        m_run (r);
    }
};

//------------------------------------------------------------------------------

template <class>
suite_info::suite_info (std::string const& name, std::string const& module,
        std::string const& library, bool manual, run_type run)
    : name_ (name)
    , module_ (module)
    , library_ (library)
    , m_manual (manual)
    , m_run (std::move (run))
{
}

template <class>
std::string
suite_info::full_name() const
{
    return library_ + "." + module_ + "." + name_;
}

inline
bool
operator< (suite_info const& lhs, suite_info const& rhs)
{
    return lhs.full_name() < rhs.full_name();
}

/** Convenience for producing suite_info for a given test type. */
template <class Suite>
suite_info
make_suite_info (std::string const& name, std::string const& module,
    std::string const& library, bool manual)
{
    return suite_info(name, module, library, manual,
        [](runner& r) { return Suite{}(r); });
}

} // unit_test
} // beast

#endif
