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

#ifndef BEAST_UNIT_TEST_REPORTER_H_INCLUDED
#define BEAST_UNIT_TEST_REPORTER_H_INCLUDED

#include <beast/unit_test/amount.h>
#include <beast/unit_test/recorder.h>
#include <beast/streams/abstract_ostream.h>
#include <beast/streams/basic_std_ostream.h>
#include <boost/optional.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace beast {
namespace unit_test {

namespace detail {

/** A simple test runner that writes everything to a stream in real time.
    The totals are output when the object is destroyed.
*/
template <class = void>
class reporter : public runner
{
private:
    using clock_type = std::chrono::steady_clock;

    struct case_results
    {
        std::string name;
        std::size_t total = 0;
        std::size_t failed = 0;

        case_results (std::string const& name_ = "");
    };

    struct suite_results
    {
        std::string name;
        std::size_t cases = 0;
        std::size_t total = 0;
        std::size_t failed = 0;
        typename clock_type::time_point start =
            clock_type::now();

        explicit
        suite_results (std::string const& name_ = "");

        void
        add (case_results const& r);
    };

    struct results
    {
        using run_time = std::pair<std::string,
            typename clock_type::duration>;

        enum
        {
            max_top = 10
        };

        std::size_t suites = 0;
        std::size_t cases = 0;
        std::size_t total = 0;
        std::size_t failed = 0;
        std::vector<run_time> top;
        typename clock_type::time_point start =
            clock_type::now();

        void
        add (suite_results const& r);
    };

    boost::optional <std_ostream> std_ostream_;
    std::reference_wrapper <abstract_ostream> stream_;
    results results_;
    suite_results suite_results_;
    case_results case_results_;

public:
    reporter (reporter const&) = delete;
    reporter& operator= (reporter const&) = delete;

    ~reporter();

    explicit
    reporter (std::ostream& stream = std::cout);

    explicit
    reporter (abstract_ostream& stream);

private:
    static
    std::string
    fmtdur (typename clock_type::duration const& d);

    virtual
    void
    on_suite_begin (suite_info const& info) override;

    virtual
    void
    on_suite_end() override;

    virtual
    void
    on_case_begin (std::string const& name) override;

    virtual
    void
    on_case_end() override;

    virtual
    void
    on_pass() override;

    virtual
    void
    on_fail (std::string const& reason) override;

    virtual
    void
    on_log (std::string const& s) override;
};

//------------------------------------------------------------------------------

template <class _>
reporter<_>::case_results::case_results (
        std::string const& name_)
    : name (name_)
{
}

template <class _>
reporter<_>::suite_results::suite_results (
        std::string const& name_)
    : name (name_)
{
}

template <class _>
void
reporter<_>::suite_results::add (case_results const& r)
{
    ++cases;
    total += r.total;
    failed += r.failed;
}

template <class _>
void
reporter<_>::results::add (
    suite_results const& r)
{
    ++suites;
    total += r.total;
    cases += r.cases;
    failed += r.failed;

    auto const elapsed =
        clock_type::now() - r.start;
    if (elapsed >= std::chrono::seconds(1))
    {
        auto const iter = std::lower_bound(top.begin(),
            top.end(), elapsed,
            [](run_time const& t1,
                typename clock_type::duration const& t2)
            {
                return t1.second > t2;
            });
        if (iter != top.end())
        {
            if (top.size() == max_top)
                top.resize(top.size() - 1);
            top.emplace(iter, r.name, elapsed);
        }
        else if (top.size() < max_top)
        {
            top.emplace_back(r.name, elapsed);
        }
    }
}

//------------------------------------------------------------------------------

template <class _>
reporter<_>::reporter (
        std::ostream& stream)
    : std_ostream_ (std::ref (stream))
    , stream_ (*std_ostream_)
{
}

template <class _>
reporter<_>::~reporter()
{
    if (results_.top.size() > 0)
    {
        stream_.get() << "Longest suite times:";
        for (auto const& i : results_.top)
            stream_.get() << std::setw(8) <<
                fmtdur(i.second) << " " << i.first;
    }
    auto const elapsed =
        clock_type::now() - results_.start;
    stream_.get() <<
        fmtdur(elapsed) << ", " <<
        amount (results_.suites, "suite") << ", " <<
        amount (results_.cases, "case") << ", " <<
        amount (results_.total, "test") << " total, " <<
        amount (results_.failed, "failure");
}

template <class _>
reporter<_>::reporter (
        abstract_ostream& stream)
    : stream_ (stream)
{
}

template <class _>
std::string
reporter<_>::fmtdur (
    typename clock_type::duration const& d)
{
    using namespace std::chrono;
    auto const ms =
        duration_cast<milliseconds>(d);
    if (ms < seconds(1))
        return std::to_string(ms.count()) + "ms";
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) <<
        (ms.count()/1000.) << "s";
    return ss.str();
}

template <class _>
void
reporter<_>::on_suite_begin (
    suite_info const& info)
{
    suite_results_ = suite_results (info.full_name());
}

template <class _>
void
reporter<_>::on_suite_end()
{
    results_.add (suite_results_);
}

template <class _>
void
reporter<_>::on_case_begin (
    std::string const& name)
{
    case_results_ = case_results (name);

    stream_.get() <<
        suite_results_.name <<
        (case_results_.name.empty() ?
            "" : (" " + case_results_.name));
}

template <class _>
void
reporter<_>::on_case_end()
{
    suite_results_.add (case_results_);
}

template <class _>
void
reporter<_>::on_pass()
{
    ++case_results_.total;
}

template <class _>
void
reporter<_>::on_fail (
    std::string const& reason)
{
    ++case_results_.failed;
    ++case_results_.total;
    stream_.get() <<
        "#" << case_results_.total <<
        " failed" <<
        (reason.empty() ? "" : ": ") << reason;
}

template <class _>
void
reporter<_>::on_log (
    std::string const& s)
{
    stream_.get() << s;
}

} // detail

using reporter = detail::reporter<>;

} // unit_test
} // beast

#endif
