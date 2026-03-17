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
    results results_;
    suite_results suite_;
    case_results case_;

public:
    recorder() = default;

    /** Returns a report with the results of all completed suites. */
    results const&
    report() const
    {
        return results_;
    }

private:
    virtual void
    on_suite_begin(suite_info const& info) override
    {
        suite_ = suite_results(info.full_name());
    }

    virtual void
    on_suite_end() override
    {
        results_.insert(std::move(suite_));
    }

    virtual void
    on_case_begin(std::string const& name) override
    {
        case_ = case_results(name);
    }

    virtual void
    on_case_end() override
    {
        if (case_.tests.size() > 0)
            suite_.insert(std::move(case_));
    }

    virtual void
    on_pass() override
    {
        case_.tests.pass();
    }

    virtual void
    on_fail(std::string const& reason) override
    {
        case_.tests.fail(reason);
    }

    virtual void
    on_log(std::string const& s) override
    {
        case_.log.insert(s);
    }
};

}  // namespace unit_test
}  // namespace beast
