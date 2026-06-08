// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/suite_info.h>

#include <boost/assert.hpp>

#include <mutex>
#include <string>

namespace beast::unit_test {

/** Unit test runner interface.

    Derived classes can customize the reporting behavior. This interface is
    injected into the unit_test class to receive the results of the tests.
*/
class Runner
{
    std::string arg_;
    bool default_ = false;
    bool failed_ = false;
    bool cond_ = false;
    std::recursive_mutex mutex_;

public:
    Runner() = default;
    virtual ~Runner() = default;
    Runner(Runner const&) = delete;
    Runner&
    operator=(Runner const&) = delete;

    /** Set the argument string.

        The argument string is available to suites and
        allows for customization of the test. Each suite
        defines its own syntax for the argument string.
        The same argument is passed to all suites.
    */
    void
    arg(std::string const& s)
    {
        arg_ = s;
    }

    /** Returns the argument string. */
    [[nodiscard]] std::string const&
    arg() const
    {
        return arg_;
    }

    /** Run the specified suite.
        @return `true` if any conditions failed.
    */
    template <class = void>
    bool
    run(SuiteInfo const& s);

    /** Run a sequence of suites.
        The expression
            `FwdIter::value_type`
        must be convertible to `SuiteInfo`.
        @return `true` if any conditions failed.
    */
    template <class FwdIter>
    bool
    run(FwdIter first, FwdIter last);

    /** Conditionally run a sequence of suites.
        pred will be called as:
        @code
            bool pred(SuiteInfo const&);
        @endcode
        @return `true` if any conditions failed.
    */
    template <class FwdIter, class Pred>
    bool
    runIf(FwdIter first, FwdIter last, Pred pred = Pred{});

    /** Run all suites in a container.
        @return `true` if any conditions failed.
    */
    template <class SequenceContainer>
    bool
    runEach(SequenceContainer const& c);

    /** Conditionally run suites in a container.
        pred will be called as:
        @code
            bool pred(SuiteInfo const&);
        @endcode
        @return `true` if any conditions failed.
    */
    template <class SequenceContainer, class Pred>
    bool
    runEachIf(SequenceContainer const& c, Pred pred = Pred{});

protected:
    /// Called when a new suite starts.
    virtual void
    onSuiteBegin(SuiteInfo const&)
    {
    }

    /// Called when a suite ends.
    virtual void
    onSuiteEnd()
    {
    }

    /// Called when a new case starts.
    virtual void
    onCaseBegin(std::string const&)
    {
    }

    /// Called when a new case ends.
    virtual void
    onCaseEnd()
    {
    }

    /// Called for each passing condition.
    virtual void
    onPass()
    {
    }

    /// Called for each failing condition.
    virtual void
    onFail(std::string const&)
    {
    }

    /// Called when a test logs output.
    virtual void
    onLog(std::string const&)
    {
    }

private:
    friend class Suite;

    // Start a new testcase.
    template <class = void>
    void
    testcase(std::string const& name);

    template <class = void>
    void
    pass();

    template <class = void>
    void
    fail(std::string const& reason);

    template <class = void>
    void
    log(std::string const& s);
};

//------------------------------------------------------------------------------

template <class>
bool
Runner::run(SuiteInfo const& s)
{
    // Enable 'default' testcase
    default_ = true;
    failed_ = false;
    onSuiteBegin(s);
    s.run(*this);
    // Forgot to call pass or fail.
    BOOST_ASSERT(cond_);
    onCaseEnd();
    onSuiteEnd();
    return failed_;
}

template <class FwdIter>
bool
Runner::run(FwdIter first, FwdIter last)
{
    bool failed(false);
    for (; first != last; ++first)
        failed = run(*first) || failed;
    return failed;
}

template <class FwdIter, class Pred>
bool
Runner::runIf(FwdIter first, FwdIter last, Pred pred)
{
    bool failed(false);
    for (; first != last; ++first)
    {
        if (pred(*first))
            failed = run(*first) || failed;
    }
    return failed;
}

template <class SequenceContainer>
bool
Runner::runEach(SequenceContainer const& c)
{
    bool failed(false);
    for (auto const& s : c)
        failed = run(s) || failed;
    return failed;
}

template <class SequenceContainer, class Pred>
bool
Runner::runEachIf(SequenceContainer const& c, Pred pred)
{
    bool failed(false);
    for (auto const& s : c)
    {
        if (pred(s))
            failed = run(s) || failed;
    }
    return failed;
}

template <class>
void
Runner::testcase(std::string const& name)
{
    std::scoped_lock const lock(mutex_);
    // Name may not be empty
    BOOST_ASSERT(default_ || !name.empty());
    // Forgot to call pass or fail
    BOOST_ASSERT(default_ || cond_);
    if (!default_)
        onCaseEnd();
    default_ = false;
    cond_ = false;
    onCaseBegin(name);
}

template <class>
void
Runner::pass()
{
    std::scoped_lock const lock(mutex_);
    if (default_)
        testcase("");
    onPass();
    cond_ = true;
}

template <class>
void
Runner::fail(std::string const& reason)
{
    std::scoped_lock const lock(mutex_);
    if (default_)
        testcase("");
    onFail(reason);
    failed_ = true;
    cond_ = true;
}

template <class>
void
Runner::log(std::string const& s)
{
    std::scoped_lock const lock(mutex_);
    if (default_)
        testcase("");
    onLog(s);
}

}  // namespace beast::unit_test
