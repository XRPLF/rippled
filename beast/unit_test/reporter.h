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

#ifndef BEAST_UNIT_TEST_REPORTER_H_INLCUDED
#define BEAST_UNIT_TEST_REPORTER_H_INLCUDED

#include <beast/unit_test/amount.h>
#include <beast/unit_test/recorder.h>
#include <beast/streams/abstract_ostream.h>
#include <beast/streams/basic_std_ostream.h>
#include <boost/optional.hpp>
#include <functional>
#include <iostream>

namespace beast {
namespace unit_test {

/** A simple test runner that writes everything to a stream in real time.
    The totals are output when the object is destroyed.
*/
class reporter : public runner
{
private:
    struct case_results
    {
        std::string name;
        std::size_t total;
        std::size_t failed;

        case_results (std::string const& name_ = "")
            : name (name_)
            , total (0)
            , failed (0)
        {
        }
    };

    struct suite_results
    {
        std::string name;
        std::size_t cases;
        std::size_t total;
        std::size_t failed;

        explicit
        suite_results (std::string const& name_ = "")
            : name (name_)
            , cases (0)
            , total (0)
            , failed (0)
        {
        }

        void
        add (case_results const& r)
        {
            ++cases;
            total += r.total;
            failed += r.failed;
        }
    };

    struct results
    {
        std::size_t suites;
        std::size_t cases;
        std::size_t total;
        std::size_t failed;

        results()
            : suites (0)
            , cases (0)
            , total (0)
            , failed (0)
        {
        }

        void
        add (suite_results const& r)
        {
            ++suites;
            total += r.total;
            cases += r.cases;
            failed += r.failed;
        }
    };

    boost::optional <std_ostream> m_std_ostream;
    std::reference_wrapper <abstract_ostream> m_stream;
    results m_results;
    suite_results m_suite;
    case_results m_case;

public:
    reporter (reporter const&) = delete;
    reporter& operator= (reporter const&) = delete;

    explicit reporter (std::ostream& stream = std::cout)
        : m_std_ostream (std::ref (stream))
        , m_stream (*m_std_ostream)
    {
    }

    ~reporter()
    {
        m_stream.get() <<
            amount (m_results.suites, "suite") << ", " <<
            amount (m_results.cases, "case") << ", " <<
            amount (m_results.total, "test") << " total, " <<
            amount (m_results.failed, "failure")
            ;
    }

    explicit reporter (abstract_ostream& stream)
        : m_stream (stream)
    {
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
        m_results.add (m_suite);
    }

    virtual
    void
    on_case_begin (std::string const& name) override
    {
        m_case = case_results (name);

        m_stream.get() <<
            m_suite.name <<
            (m_case.name.empty() ?
                "" : (" " + m_case.name))
        ;
    }

    virtual
    void
    on_case_end() override
    {
        m_suite.add (m_case);
    }

    virtual
    void
    on_pass() override
    {
        ++m_case.total;
    }

    virtual
    void
    on_fail (std::string const& reason) override
    {
        ++m_case.failed;
        ++m_case.total;

        m_stream.get() <<
            "#" << m_case.total <<
            " failed" <<
            (reason.empty() ? "" : ": ") << reason
            ;
    }

    virtual
    void
    on_log (std::string const& s) override
    {
        m_stream.get() <<
            s;
    }
};

} // unit_test
} // beast

#endif
