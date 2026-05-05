// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/results.h>
#include <xrpl/beast/unit_test/runner.h>

namespace beast::unit_test {

/** A test runner that stores the results. */
class Recorder : public Runner
{
private:
    Results m_results_;
    SuiteResults m_suite_;
    CaseResults m_case_;

public:
    Recorder() = default;

    /** Returns a report with the results of all completed suites. */
    [[nodiscard]] Results const&
    report() const
    {
        return m_results_;
    }

private:
    void
    onSuiteBegin(SuiteInfo const& info) override
    {
        m_suite_ = SuiteResults(info.fullName());
    }

    void
    onSuiteEnd() override
    {
        m_results_.insert(std::move(m_suite_));
    }

    void
    onCaseBegin(std::string const& name) override
    {
        m_case_ = CaseResults(name);
    }

    void
    onCaseEnd() override
    {
        if (!m_case_.tests.empty())
            m_suite_.insert(std::move(m_case_));
    }

    void
    onPass() override
    {
        m_case_.tests.pass();
    }

    void
    onFail(std::string const& reason) override
    {
        m_case_.tests.fail(reason);
    }

    void
    onLog(std::string const& s) override
    {
        m_case_.log.insert(s);
    }
};

}  // namespace beast::unit_test
