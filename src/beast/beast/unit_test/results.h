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

#ifndef BEAST_UNIT_TEST_RESULTS_H_INCLUDED
#define BEAST_UNIT_TEST_RESULTS_H_INCLUDED

#include <beast/container/const_container.h>

#include <string>
#include <vector>

namespace beast {
namespace unit_test {

/** Holds a set of test condition outcomes in a testcase. */
class case_results
{
public:
    /** Holds the result of evaluating one test condition. */
    struct test
    {
        explicit test (bool pass_)
            : pass (pass_)
        {
        }

        test (bool pass_, std::string const& reason_)
            : pass (pass_)
            , reason (reason_)
        {
        }

        bool pass;
        std::string reason;
    };

private:
    class tests_t
        : public const_container <std::vector <test>>
    {
    private:
        std::size_t failed_;

    public:
        tests_t ()
            : failed_ (0)
        {
        }

        /** Returns the total number of test conditions. */
        std::size_t
        total() const
        {
            return cont().size();
        }

        /** Returns the number of failed test conditions. */
        std::size_t
        failed() const
        {
            return failed_;
        }

        /** Register a successful test condition. */
        void
        pass()
        {
            cont().emplace_back (true);
        }

        /** Register a failed test condition. */
        void
        fail (std::string const& reason = "")
        {
            ++failed_;
            cont().emplace_back (false, reason);
        }
    };

    class log_t
        : public const_container <std::vector <std::string>>
    {
    public:
        /** Insert a string into the log. */
        void
        insert (std::string const& s)
        {
            cont().push_back (s);
        }
    };

    std::string name_;

public:
    explicit case_results (std::string const& name = "")
        : name_ (name)
    {
    }

    /** Returns the name of this testcase. */
    std::string const&
    name() const
    {
        return name_;
    }

    /** Memberspace for a container of test condition outcomes. */
    tests_t tests;

    /** Memberspace for a container of testcase log messages. */
    log_t log;
};

//--------------------------------------------------------------------------

/** Holds the set of testcase results in a suite. */
class suite_results
    : public const_container <std::vector <case_results>>
{
private:
    std::string name_;
    std::size_t total_ = 0;
    std::size_t failed_ = 0;

public:
    explicit suite_results (std::string const& name = "")
        : name_ (name)
    {
    }

    /** Returns the name of this suite. */
    std::string const&
    name() const
    {
        return name_;
    }

    /** Returns the total number of test conditions. */
    std::size_t
    total() const
    {
        return total_;
    }

    /** Returns the number of failures. */
    std::size_t
    failed() const
    {
        return failed_;
    }

    /** Insert a set of testcase results. */
    /** @{ */
    void
    insert (case_results&& r)
    {
        cont().emplace_back (std::move (r));
        total_ += r.tests.total();
        failed_ += r.tests.failed();
    }

    void
    insert (case_results const& r)
    {
        cont().push_back (r);
        total_ += r.tests.total();
        failed_ += r.tests.failed();
    }
    /** @} */
};

//------------------------------------------------------------------------------

// VFALCO TODO Make this a template class using scoped allocators
/** Holds the results of running a set of testsuites. */
class results
    : public const_container <std::vector <suite_results>>
{
private:
    std::size_t m_cases;
    std::size_t total_;
    std::size_t failed_;

public:
    results()
        : m_cases (0)
        , total_ (0)
        , failed_ (0)
    {
    }

    /** Returns the total number of test cases. */
    std::size_t
    cases() const
    {
        return m_cases;
    }

    /** Returns the total number of test conditions. */
    std::size_t
    total() const
    {
        return total_;
    }

    /** Returns the number of failures. */
    std::size_t
    failed() const
    {
        return failed_;
    }

    /** Insert a set of suite results. */
    /** @{ */
    void
    insert (suite_results&& r)
    {
        m_cases += r.size();
        total_ += r.total();
        failed_ += r.failed();
        cont().emplace_back (std::move (r));
    }

    void
    insert (suite_results const& r)
    {
        m_cases += r.size();
        total_ += r.total();
        failed_ += r.failed();
        cont().push_back (r);
    }
    /** @} */
};

} // unit_test
} // beast

#endif
