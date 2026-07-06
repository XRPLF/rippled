// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/detail/const_container.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace beast::unit_test {

/** Holds a set of test condition outcomes in a testcase. */
class CaseResults
{
public:
    /** Holds the result of evaluating one test condition. */
    struct Test
    {
        explicit Test(bool pass) : pass(pass)
        {
        }

        Test(bool pass, std::string reason) : pass(pass), reason(std::move(reason))
        {
        }

        bool pass;
        std::string reason;
    };

private:
    class TestsT : public detail::ConstContainer<std::vector<Test>>
    {
    private:
        std::size_t failed_{0};

    public:
        TestsT() = default;

        /** Returns the total number of test conditions. */
        [[nodiscard]] std::size_t
        total() const
        {
            return cont().size();
        }

        /** Returns the number of failed test conditions. */
        [[nodiscard]] std::size_t
        failed() const
        {
            return failed_;
        }

        /** Register a successful test condition. */
        void
        pass()
        {
            cont().emplace_back(true);
        }

        /** Register a failed test condition. */
        void
        fail(std::string const& reason = "")
        {
            ++failed_;
            cont().emplace_back(false, reason);
        }
    };

    class LogT : public detail::ConstContainer<std::vector<std::string>>
    {
    public:
        /** Insert a string into the log. */
        void
        insert(std::string const& s)
        {
            cont().push_back(s);
        }
    };

    std::string name_;

public:
    explicit CaseResults(std::string name = "") : name_(std::move(name))
    {
    }

    /** Returns the name of this testcase. */
    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    /** Memberspace for a container of test condition outcomes. */
    TestsT tests;

    /** Memberspace for a container of testcase log messages. */
    LogT log;
};

//--------------------------------------------------------------------------

/** Holds the set of testcase results in a suite. */
class SuiteResults : public detail::ConstContainer<std::vector<CaseResults>>
{
private:
    std::string name_;
    std::size_t total_ = 0;
    std::size_t failed_ = 0;

public:
    explicit SuiteResults(std::string name = "") : name_(std::move(name))
    {
    }

    /** Returns the name of this suite. */
    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    /** Returns the total number of test conditions. */
    [[nodiscard]] std::size_t
    total() const
    {
        return total_;
    }

    /** Returns the number of failures. */
    [[nodiscard]] std::size_t
    failed() const
    {
        return failed_;
    }

    /** Insert a set of testcase results. */
    /** @{ */
    void
    insert(CaseResults&& r)
    {
        total_ += r.tests.total();
        failed_ += r.tests.failed();
        cont().emplace_back(std::move(r));
    }

    void
    insert(CaseResults const& r)
    {
        cont().push_back(r);
        total_ += r.tests.total();
        failed_ += r.tests.failed();
    }
    /** @} */
};

//------------------------------------------------------------------------------

// VFALCO TODO Make this a template class using scoped allocators
/** Holds the results of running a set of testsuites. */
class Results : public detail::ConstContainer<std::vector<SuiteResults>>
{
private:
    std::size_t cases_{0};
    std::size_t total_{0};
    std::size_t failed_{0};

public:
    Results() = default;

    /** Returns the total number of test cases. */
    [[nodiscard]] std::size_t
    cases() const
    {
        return cases_;
    }

    /** Returns the total number of test conditions. */
    [[nodiscard]] std::size_t
    total() const
    {
        return total_;
    }

    /** Returns the number of failures. */
    [[nodiscard]] std::size_t
    failed() const
    {
        return failed_;
    }

    /** Insert a set of suite results. */
    /** @{ */
    void
    insert(SuiteResults&& r)
    {
        cases_ += r.size();
        total_ += r.total();
        failed_ += r.failed();
        cont().emplace_back(std::move(r));
    }

    void
    insert(SuiteResults const& r)
    {
        cases_ += r.size();
        total_ += r.total();
        failed_ += r.failed();
        cont().push_back(r);
    }
    /** @} */
};

}  // namespace beast::unit_test
