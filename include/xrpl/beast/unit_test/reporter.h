// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/amount.h>
#include <xrpl/beast/unit_test/recorder.h>

#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace beast::unit_test {

namespace detail {

/** A simple test runner that writes everything to a stream in real time.
    The totals are output when the object is destroyed.
*/
template <class = void>
class Reporter : public Runner
{
private:
    using clock_type = std::chrono::steady_clock;

    struct CaseResults
    {
        std::string name;
        std::size_t total = 0;
        std::size_t failed = 0;

        explicit CaseResults(std::string name = "") : name(std::move(name))
        {
        }
    };

    struct SuiteResults
    {
        std::string name;
        std::size_t cases = 0;
        std::size_t total = 0;
        std::size_t failed = 0;
        typename clock_type::time_point start = clock_type::now();

        explicit SuiteResults(std::string name = "") : name(std::move(name))
        {
        }

        void
        add(CaseResults const& r);
    };

    struct Results
    {
        using run_time = std::pair<std::string, typename clock_type::duration>;

        static constexpr auto kMaxTop = 10;

        std::size_t suites = 0;
        std::size_t cases = 0;
        std::size_t total = 0;
        std::size_t failed = 0;
        std::vector<run_time> top;
        typename clock_type::time_point start = clock_type::now();

        void
        add(SuiteResults const& r);
    };

    std::ostream& os_;
    Results results_;
    SuiteResults suiteResults_;
    CaseResults caseResults_;

public:
    Reporter(Reporter const&) = delete;
    Reporter&
    operator=(Reporter const&) = delete;

    ~Reporter() override;

    explicit Reporter(std::ostream& os = std::cout);

private:
    static std::string
    fmtdur(typename clock_type::duration const& d);

    void
    onSuiteBegin(SuiteInfo const& info) override;

    void
    onSuiteEnd() override;

    void
    onCaseBegin(std::string const& name) override;

    void
    onCaseEnd() override;

    void
    onPass() override;

    void
    onFail(std::string const& reason) override;

    void
    onLog(std::string const& s) override;
};

//------------------------------------------------------------------------------

template <class Unused>
void
Reporter<Unused>::SuiteResults::add(CaseResults const& r)
{
    ++cases;
    total += r.total;
    failed += r.failed;
}

template <class Unused>
void
Reporter<Unused>::Results::add(SuiteResults const& r)
{
    ++suites;
    total += r.total;
    cases += r.cases;
    failed += r.failed;
    auto const elapsed = clock_type::now() - r.start;
    if (elapsed >= std::chrono::seconds{1})
    {
        auto const iter = std::lower_bound(
            top.begin(),
            top.end(),
            elapsed,
            [](run_time const& t1, typename clock_type::duration const& t2) {
                return t1.second > t2;
            });
        if (iter != top.end())
        {
            if (top.size() == kMaxTop)
                top.resize(top.size() - 1);
            top.emplace(iter, r.name, elapsed);
        }
        else if (top.size() < kMaxTop)
        {
            top.emplace_back(r.name, elapsed);
        }
    }
}

//------------------------------------------------------------------------------

template <class Unused>
Reporter<Unused>::Reporter(std::ostream& os) : os_(os)
{
}

template <class Unused>
Reporter<Unused>::~Reporter()
{
    if (results_.top.size() > 0)
    {
        os_ << "Longest suite times:\n";
        for (auto const& i : results_.top)
            os_ << std::setw(8) << fmtdur(i.second) << " " << i.first << '\n';
    }
    auto const elapsed = clock_type::now() - results_.start;
    os_ << fmtdur(elapsed) << ", " << Amount{results_.suites, "suite"} << ", "
        << Amount{results_.cases, "case"} << ", " << Amount{results_.total, "test"} << " total, "
        << Amount{results_.failed, "failure"} << std::endl;
}

template <class Unused>
std::string
Reporter<Unused>::fmtdur(typename clock_type::duration const& d)
{
    using namespace std::chrono;
    auto const ms = duration_cast<milliseconds>(d);
    if (ms < seconds{1})
        return boost::lexical_cast<std::string>(ms.count()) + "ms";
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << (ms.count() / 1000.) << "s";
    return ss.str();
}

template <class Unused>
void
Reporter<Unused>::onSuiteBegin(SuiteInfo const& info)
{
    suiteResults_ = SuiteResults{info.fullName()};
}

template <class Unused>
void
Reporter<Unused>::onSuiteEnd()
{
    results_.add(suiteResults_);
}

template <class Unused>
void
Reporter<Unused>::onCaseBegin(std::string const& name)
{
    caseResults_ = CaseResults(name);
    os_ << suiteResults_.name << (caseResults_.name.empty() ? "" : (" " + caseResults_.name))
        << std::endl;
}

template <class Unused>
void
Reporter<Unused>::onCaseEnd()
{
    suiteResults_.add(caseResults_);
}

template <class Unused>
void
Reporter<Unused>::onPass()
{
    ++caseResults_.total;
}

template <class Unused>
void
Reporter<Unused>::onFail(std::string const& reason)
{
    ++caseResults_.failed;
    ++caseResults_.total;
    os_ << "#" << caseResults_.total << " failed" << (reason.empty() ? "" : ": ") << reason
        << std::endl;
}

template <class Unused>
void
Reporter<Unused>::onLog(std::string const& s)
{
    os_ << s;
}

}  // namespace detail

using reporter = detail::Reporter<>;

}  // namespace beast::unit_test
