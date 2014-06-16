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

#ifndef BEAST_UNIT_TEST_RUNNER_H_INCLUDED
#define BEAST_UNIT_TEST_RUNNER_H_INCLUDED

#include <beast/unit_test/suite_info.h>

#include <beast/streams/abstract_ostream.h>

#include <cassert>
#include <string>

namespace beast {
namespace unit_test {

/** Unit test runner interface.
    Derived classes can customize the reporting behavior. This interface is
    injected into the unit_test class to receive the results of the tests.
*/
class runner
{
private:
    // Reroutes log output to the runner
    class stream_t : public abstract_ostream
    {
    private:
        runner& m_owner;

    public:
        stream_t() = delete;
        stream_t& operator= (stream_t const&) = delete;

        stream_t (runner& owner)
            : m_owner (owner)
        {
        }

        void
        write (string_type const& s)
        {
            m_owner.log (s);
        }
    };

    stream_t m_stream;
    bool m_default;
    bool m_failed;
    bool m_cond;

public:
    virtual ~runner() = default;
    runner (runner const&) = default;
    runner& operator= (runner const&) = default;

    runner()
        : m_stream (*this)
        , m_default (false)
        , m_failed (false)
        , m_cond (false)
    {
    }

    /** Run the specified suite.
        @return `true` if any conditions failed.
    */
    bool
    run (suite_info const& s)
    {
        // Enable 'default' testcase
        m_default = true;
        m_failed = false;
        on_suite_begin (s);
        s.run (*this);
        // Forgot to call pass or fail.
        assert (m_cond);
        on_case_end();
        on_suite_end();
        return m_failed;
    }

    /** Run a sequence of suites.
        The expression
            `FwdIter::value_type`
        must be convertible to `suite_info`.
        @return `true` if any conditions failed.
    */
    template <class FwdIter>
    bool
    run (FwdIter first, FwdIter last)
    {
        bool failed (false);
        for (;first != last; ++first)
            failed = run (*first) || failed;
        return failed;
    }

    /** Conditionally run a sequence of suites.
        pred will be called as:
        @code
            bool pred (suite_info const&);
        @endcode
        @return `true` if any conditions failed.
    */
    template <class FwdIter, class Pred>
    bool
    run_if (FwdIter first, FwdIter last, Pred pred = Pred())
    {
        bool failed (false);
        for (;first != last; ++first)
            if (pred (*first))
                failed = run (*first) || failed;
        return failed;
    }

    /** Run all suites in a container.
        @return `true` if any conditions failed.
    */
    template <class SequenceContainer>
    bool
    run_each (SequenceContainer const& c)
    {
        bool failed (false);
        for (auto const& s : c)
            failed = run (s) || failed;
        return failed;
    }

    /** Conditionally run suites in a container.
        pred will be called as:
        @code
            bool pred (suite_info const&);
        @endcode
        @return `true` if any conditions failed.
    */
    template <class SequenceContainer, class Pred>
    bool
    run_each_if (SequenceContainer const& c, Pred pred = Pred())
    {
        bool failed (false);
        for (auto const& s : c)
            if (pred (s))
                failed = run (s) || failed;
        return failed;
    }

private:
    //
    // Overrides
    //

    /** Called when a new suite starts. */
    virtual
    void
    on_suite_begin (suite_info const&)
    {
    }

    /** Called when a suite ends. */
    virtual
    void
    on_suite_end()
    {
    }

    /** Called when a new case starts. */
    virtual
    void
    on_case_begin (std::string const&)
    {
    }

    /** Called when a new case ends. */
    virtual
    void
    on_case_end()
    {
    }

    /** Called for each passing condition. */
    virtual
    void
    on_pass ()
    {
    }

    /** Called for each failing condition. */
    virtual
    void
    on_fail (std::string const&)
    {
    }

    /** Called when a test logs output. */
    virtual
    void
    on_log (std::string const&)
    {
    }

private:
    friend class suite;

    abstract_ostream&
    stream()
    {
        return m_stream;
    }

    // Start a new testcase.
    void
    testcase (std::string const& name)
    {
        // Name may not be empty
        assert (m_default || ! name.empty());
        // Forgot to call pass or fail
        assert (m_default || m_cond);
        if (! m_default)
            on_case_end();
        m_default = false;
        m_cond = false;
        on_case_begin (name);
    }

    void
    pass()
    {
        if (m_default)
            testcase ("");
        on_pass();
        m_cond = true;
    }

    void
    fail (std::string const& reason)
    {
        if (m_default)
            testcase ("");
        on_fail (reason);
        m_failed = true;
        m_cond = true;
    }

    void
    log (std::string const& s)
    {
        if (m_default)
            testcase ("");
        on_log (s);
    }
};

} // unit_test
} // beast

#endif
