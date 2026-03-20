// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/results.h>
#include <xrpl/beast/unit_test/runner.h>

namespace beast {
namespace unit_test {

/** A test runner that stores the results. */
class recorder : public runner
{
private:
    Results results_;
    SuiteResults suite_;
    CaseResults case_;

public:
    recorder() = default;

    /** Returns a report with the results of all completed suites. */
    Results const&
    report() const
    {
        return results_;
    }

private:
    virtual void
    onSuiteBegin(SuiteInfo const& info) override
    {
        suite_ = SuiteResults(info.fullName());
    }

    virtual void
    onSuiteEnd() override
    {
        results_.insert(std::move(suite_));
    }

    virtual void
    onCaseBegin(std::string const& name) override
    {
        case_ = CaseResults(name);
    }

    virtual void
    onCaseEnd() override
    {
        if (case_.tests.size() > 0)
            suite_.insert(std::move(case_));
    }

    virtual void
    onPass() override
    {
        case_.tests.pass();
    }

    virtual void
    onFail(std::string const& reason) override
    {
        case_.tests.fail(reason);
    }

    virtual void
    onLog(std::string const& s) override
    {
        case_.log.insert(s);
    }
};

}  // namespace unit_test
}  // namespace beast
