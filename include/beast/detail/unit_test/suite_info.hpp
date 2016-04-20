//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_UNIT_TEST_SUITE_INFO_HPP
#define BEAST_DETAIL_UNIT_TEST_SUITE_INFO_HPP

#include <functional>
#include <string>
#include <utility>

namespace beast {
namespace detail {

inline
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
} // detail
} // beast

#endif
