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
    Results results_;
    SuiteResults suite_;
    CaseResults case_;

public:
    Recorder() = default;

    /** Returns a report with the results of all completed suites. */
    [[nodiscard]] Results const&
    report() const
    {
        return results_;
    }

private:
    void
    onSuiteBegin(SuiteInfo const& info) override
    {
        suite_ = SuiteResults(info.fullName());
    }

    void
    onSuiteEnd() override
    {
        results_.insert(std::move(suite_));
    }

    void
    onCaseBegin(std::string const& name) override
    {
        case_ = CaseResults(name);
    }

    void
    onCaseEnd() override
    {
        if (!case_.tests.empty())
            suite_.insert(std::move(case_));
    }

    void
    onPass() override
    {
        case_.tests.pass();
    }

    void
    onFail(std::string const& reason) override
    {
        case_.tests.fail(reason);
    }

    void
    onLog(std::string const& s) override
    {
        case_.log.insert(s);
    }
};

}  // namespace beast::unit_test
