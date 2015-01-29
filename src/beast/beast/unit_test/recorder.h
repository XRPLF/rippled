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

#ifndef BEAST_UNIT_TEST_RECORDER_H_INCLUDED
#define BEAST_UNIT_TEST_RECORDER_H_INCLUDED

#include <beast/unit_test/results.h>
#include <beast/unit_test/runner.h>

namespace beast {
namespace unit_test {

/** A test runner that stores the results. */
class recorder : public runner
{
private:
    results m_results;
    suite_results m_suite;
    case_results m_case;

public:
    recorder() = default;
    recorder (recorder const&) = default;
    recorder& operator= (recorder const&) = default;

    /** Returns a report with the results of all completed suites. */
    results const&
    report() const
    {
        return m_results;
    }

private:
    virtual
    void
    on_suite_begin (suite_info const& info) override
    {
        m_suite = suite_results (info.full_name());
    }

    virtual
    void
    on_suite_end() override
    {
        m_results.insert (std::move (m_suite));
    }

    virtual
    void
    on_case_begin (std::string const& name) override
    {
        m_case = case_results (name);
    }

    virtual
    void
    on_case_end() override
    {
        if (m_case.tests.size() > 0)
            m_suite.insert (std::move (m_case));
    }

    virtual
    void
    on_pass() override
    {
        m_case.tests.pass();
    }

    virtual
    void
    on_fail (std::string const& reason) override
    {
        m_case.tests.fail (reason);
    }

    virtual
    void
    on_log (std::string const& s) override
    {
        m_case.log.insert (s);
    }
};

} // unit_test
} // beast

#endif
