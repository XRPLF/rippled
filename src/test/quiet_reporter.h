//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef TEST_QUIET_REPORTER_H
#define TEST_QUIET_REPORTER_H

#include <beast/unit_test/amount.hpp>
#include <beast/unit_test/recorder.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace ripple {
namespace test {

/** A simple test runner that only reports failures and a summary to the output
    stream.  To also report log events, set the runner argument to "log".
*/
class quiet_reporter : public beast::unit_test::runner
{
private:

    using clock_type = std::chrono::steady_clock;

    struct case_results
    {
        std::string name;
        std::size_t total = 0;
        std::size_t failed = 0;

        explicit
        case_results(std::string name_ = "")
            : name(std::move(name_))
        {
        }
    };

    struct suite_results
    {
        std::string name;
        std::size_t cases = 0;
        std::size_t total = 0;
        std::size_t failed = 0;

        typename clock_type::time_point start = clock_type::now();

        explicit
        suite_results(std::string name_ = "")
            : name(std::move(name_))
        {
        }

        void
        add(case_results const& r)
        {
            cases++;
            total += r.total;
            failed += r.failed;
        }
    };

    struct results
    {
        std::size_t suites = 0;
        std::size_t cases = 0;
        std::size_t total = 0;
        std::size_t failed = 0;

        typename clock_type::time_point start = clock_type::now();

        using run_time = std::pair<std::string,
            typename clock_type::duration>;

        std::vector<run_time> top_;

        void
        add(suite_results const & s)
        {
            suites++;
            cases += s.cases;
            total += s.total;
            failed += s.failed;
            top_.emplace_back(s.name, clock_type::now() - s.start);

        }
    };

    std::ostream& os_;
    suite_results suite_results_;
    case_results case_results_;
    results results_;
    bool print_log_ = false;

    static
    std::string
    fmtdur(typename clock_type::duration const& d)
    {
        using namespace std::chrono;
        auto const ms = duration_cast<milliseconds>(d);
        if(ms < seconds{1})
            return boost::lexical_cast<std::string>(
                ms.count()) + "ms";
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) <<
           (ms.count()/1000.) << "s";
        return ss.str();
    }

public:
    quiet_reporter(quiet_reporter const&) = delete;
    quiet_reporter& operator=(quiet_reporter const&) = delete;
    explicit
    quiet_reporter(std::ostream& os = std::cout, bool log = false)
        : os_(os), print_log_{log} {}

    ~quiet_reporter()
    {
        using namespace beast::unit_test;
        auto & top = results_.top_;
        if(!top.empty())
        {
            std::sort(top.begin(), top.end(),
                [](auto const & a, auto const & b)
                {
                    return b.second < a.second;
                });

            if(top.size() > 10)
                top.resize(10);

            os_ << "Longest suite times:\n";
            for(auto const& i : top)
                os_ << std::setw(8) <<
                    fmtdur(i.second) << " " << i.first << '\n';
        }

        auto const elapsed = clock_type::now() - results_.start;
        os_ <<
            fmtdur(elapsed) << ", " <<
            amount{results_.suites, "suite"} << ", " <<
            amount{results_.cases, "case"} << ", " <<
            amount{results_.total, "test"} << " total, " <<
            amount{results_.failed, "failure"} <<
            std::endl;
    }

private:
    virtual
    void
    on_suite_begin(beast::unit_test::suite_info const& info) override
    {
        suite_results_ = suite_results{info.full_name()};
    }

    virtual
    void
    on_suite_end() override
    {
        results_.add(suite_results_);
    }

    virtual
    void
    on_case_begin(std::string const& name) override
    {
        case_results_ = case_results(name);
    }

    virtual
    void
    on_case_end() override
    {
        suite_results_.add(case_results_);
    }

    virtual
    void
    on_pass() override
    {
        ++case_results_.total;
    }

    virtual
    void
    on_fail(std::string const& reason) override
    {
        ++case_results_.failed;
        ++case_results_.total;
        os_ << suite_results_.name <<
            (case_results_.name.empty() ? "" :
                (" " + case_results_.name))
            << " #" << case_results_.total << " failed" <<
            (reason.empty() ? "" : ": ") << reason << std::endl;
    }

    virtual
    void
    on_log(std::string const& s) override
    {
        if (print_log_)
        {
            os_ << suite_results_.name <<
                (case_results_.name.empty() ? "" :
                (" " + case_results_.name))
                << " " << s;
            os_.flush();
        }
    }
};
} // ripple
} // test

#endif
