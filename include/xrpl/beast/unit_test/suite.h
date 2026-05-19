// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/runner.h>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>

#include <ostream>
#include <sstream>
#include <string>

namespace beast::unit_test {

namespace detail {

template <class String>
static std::string
makeReason(String const& reason, char const* file, int line)
{
    std::string s(reason);
    if (!s.empty())
        s.append(": ");
    namespace fs = boost::filesystem;
    s.append(fs::path{file}.filename().string());
    s.append("(");
    s.append(boost::lexical_cast<std::string>(line));
    s.append(")");
    return s;
}

}  // namespace detail

class Thread;

enum class AbortT { NoAbortOnFail, AbortOnFail };

/** A testsuite class.

    Derived classes execute a series of testcases, where each testcase is
    a series of pass/fail tests. To provide a unit test using this class,
    derive from it and use the BEAST_DEFINE_UNIT_TEST macro in a
    translation unit.
*/
class Suite
{
private:
    bool abort_ = false;
    bool aborted_ = false;
    Runner* runner_ = nullptr;

    // This exception is thrown internally to stop the current suite
    // in the event of a failure, if the option to stop is set.
    struct AbortException : public std::exception
    {
        [[nodiscard]] char const*
        what() const noexcept override
        {
            return "test suite aborted";
        }
    };

    template <class CharT, class Traits, class Allocator>
    class LogBuf : public std::basic_stringbuf<CharT, Traits, Allocator>
    {
        Suite& suite_;

    public:
        explicit LogBuf(Suite& self) : suite_(self)
        {
        }

        ~LogBuf() override
        {
            sync();
        }

        int
        sync() override
        {
            auto const& s = this->str();
            if (s.size() > 0)
                suite_.runner_->log(s);
            this->str("");
            return 0;
        }
    };

    template <
        class CharT,
        class Traits = std::char_traits<CharT>,
        class Allocator = std::allocator<CharT>>
    class LogOs : public std::basic_ostream<CharT, Traits>
    {
        LogBuf<CharT, Traits, Allocator> buf_;

    public:
        explicit LogOs(Suite& self) : std::basic_ostream<CharT, Traits>(&buf_), buf_(self)
        {
        }
    };

    class ScopedTestcase;

    class TestcaseT
    {
        Suite& suite_;
        std::stringstream ss_;

    public:
        explicit TestcaseT(Suite& self) : suite_(self)
        {
        }

        /** Open a new testcase.

            A testcase is a series of evaluated test conditions. A test
            suite may have multiple test cases. A test is associated with
            the last opened testcase. When the test first runs, a default
            unnamed case is opened. Tests with only one case may omit the
            call to testcase.

            @param abort Determines if suite continues running after a failure.
        */
        void
        operator()(std::string const& name, AbortT abort = AbortT::NoAbortOnFail);

        ScopedTestcase
        operator()(AbortT abort);

        template <class T>
        ScopedTestcase
        operator<<(T const& t);
    };

public:
    /** Logging output stream.

        Text sent to the log output stream will be forwarded to
        the output stream associated with the runner.
    */
    LogOs<char> log;

    /** Memberspace for declaring test cases. */
    TestcaseT testcase;

    /** Returns the "current" running suite.
        If no suite is running, nullptr is returned.
    */
    static Suite*
    thisSuite()
    {
        return *pThisSuite();
    }

    Suite() : log(*this), testcase(*this)
    {
    }

    virtual ~Suite() = default;
    Suite(Suite const&) = delete;
    Suite&
    operator=(Suite const&) = delete;

    /** Invokes the test using the specified runner.

        Data members are set up here instead of the constructor as a
        convenience to writing the derived class to avoid repetition of
        forwarded constructor arguments to the base.
        Normally this is called by the framework for you.
    */
    template <class = void>
    void
    operator()(Runner& r);

    /** Record a successful test condition. */
    template <class = void>
    void
    pass();

    /** Record a failure.

        @param reason Optional text added to the output on a failure.

        @param file The source code file where the test failed.

        @param line The source code line number where the test failed.
    */
    /** @{ */
    template <class String>
    void
    fail(String const& reason, char const* file, int line);

    template <class = void>
    void
    fail(std::string const& reason = "");
    /** @} */

    /** Evaluate a test condition.

        This function provides improved logging by incorporating the
        file name and line number into the reported output on failure,
        as well as additional text specified by the caller.

        @param shouldBeTrue The condition to test. The condition
        is evaluated in a boolean context.

        @param reason Optional added text to output on a failure.

        @param file The source code file where the test failed.

        @param line The source code line number where the test failed.

        @return `true` if the test condition indicates success.
    */
    /** @{ */
    template <class Condition>
    bool
    expect(Condition const& shouldBeTrue)
    {
        return expect(shouldBeTrue, "");
    }

    template <class Condition, class String>
    bool
    expect(Condition const& shouldBeTrue, String const& reason);

    template <class Condition>
    bool
    expect(Condition const& shouldBeTrue, char const* file, int line)
    {
        return expect(shouldBeTrue, "", file, line);
    }

    template <class Condition, class String>
    bool
    expect(Condition const& shouldBeTrue, String const& reason, char const* file, int line);
    /** @} */

    //
    // DEPRECATED
    //
    // Expect an exception from f()
    template <class F, class String>
    bool
    except(F&& f, String const& reason);
    template <class F>
    bool
    except(F&& f)
    {
        return except(f, "");
    }
    template <class E, class F, class String>
    bool
    except(F&& f, String const& reason);
    template <class E, class F>
    bool
    except(F&& f)
    {
        return except<E>(f, "");
    }
    template <class F, class String>
    bool
    unexcept(F&& f, String const& reason);
    template <class F>
    bool
    unexcept(F&& f)
    {
        return unexcept(f, "");
    }

    /** Return the argument associated with the runner. */
    std::string const&
    arg() const
    {
        return runner_->arg();
    }

    // DEPRECATED
    // @return `true` if the test condition indicates success(a false value)
    template <class Condition, class String>
    bool
    unexpected(Condition shouldBeFalse, String const& reason);

    template <class Condition>
    bool
    unexpected(Condition shouldBeFalse)
    {
        return unexpected(shouldBeFalse, "");
    }

private:
    friend class Thread;

    static Suite**
    pThisSuite()
    {
        static Suite* kPTs = nullptr;  // NOLINT TODO
        return &kPTs;
    }

    /** Runs the suite. */
    virtual void
    run() = 0;

    void
    propagateAbort() const;

    template <class = void>
    void
    run(Runner& r);
};

//------------------------------------------------------------------------------

// Helper for streaming testcase names
class Suite::ScopedTestcase
{
private:
    Suite& suite_;
    std::stringstream& ss_;

public:
    ScopedTestcase&
    operator=(ScopedTestcase const&) = delete;

    ~ScopedTestcase()
    {
        auto const& name = ss_.str();
        if (!name.empty())
            suite_.runner_->testcase(name);
    }

    ScopedTestcase(Suite& self, std::stringstream& ss) : suite_(self), ss_(ss)
    {
        ss_.clear();
        ss_.str({});
    }

    template <class T>
    ScopedTestcase(Suite& self, std::stringstream& ss, T const& t) : suite_(self), ss_(ss)
    {
        ss_.clear();
        ss_.str({});
        ss_ << t;
    }

    template <class T>
    ScopedTestcase&
    operator<<(T const& t)
    {
        ss_ << t;
        return *this;
    }
};

//------------------------------------------------------------------------------

inline void
Suite::TestcaseT::operator()(std::string const& name, AbortT abort)
{
    suite_.abort_ = abort == AbortT::AbortOnFail;
    suite_.runner_->testcase(name);
}

inline Suite::ScopedTestcase
Suite::TestcaseT::operator()(AbortT abort)
{
    suite_.abort_ = abort == AbortT::AbortOnFail;
    return {suite_, ss_};
}

template <class T>
inline Suite::ScopedTestcase
Suite::TestcaseT::operator<<(T const& t)
{
    return {suite_, ss_, t};
}

//------------------------------------------------------------------------------

template <class>
void
Suite::operator()(Runner& r)
{
    *pThisSuite() = this;
    try
    {
        run(r);
        *pThisSuite() = nullptr;
    }
    catch (...)
    {
        *pThisSuite() = nullptr;
        throw;
    }
}

template <class Condition, class String>
bool
Suite::expect(Condition const& shouldBeTrue, String const& reason)
{
    if (shouldBeTrue)
    {
        pass();
        return true;
    }
    fail(reason);
    return false;
}

template <class Condition, class String>
bool
Suite::expect(Condition const& shouldBeTrue, String const& reason, char const* file, int line)
{
    if (shouldBeTrue)
    {
        pass();
        return true;
    }
    fail(detail::makeReason(reason, file, line));
    return false;
}

// DEPRECATED

template <class F, class String>
bool
Suite::except(F&& f, String const& reason)
{
    try
    {
        f();
        fail(reason);
        return false;
    }
    catch (...)
    {
        pass();
    }
    return true;
}

template <class E, class F, class String>
bool
Suite::except(F&& f, String const& reason)
{
    try
    {
        f();
        fail(reason);
        return false;
    }
    catch (E const&)
    {
        pass();
    }
    return true;
}

template <class F, class String>
bool
Suite::unexcept(F&& f, String const& reason)
{
    try
    {
        f();
        pass();
        return true;
    }
    catch (...)
    {
        fail(reason);
    }
    return false;
}

template <class Condition, class String>
bool
Suite::unexpected(Condition shouldBeFalse, String const& reason)
{
    bool const b = static_cast<bool>(shouldBeFalse);
    if (!b)
    {
        pass();
    }
    else
    {
        fail(reason);
    }
    return !b;
}

template <class>
void
Suite::pass()
{
    propagateAbort();
    runner_->pass();
}

// ::fail
template <class>
void
Suite::fail(std::string const& reason)
{
    propagateAbort();
    runner_->fail(reason);
    if (abort_)
    {
        aborted_ = true;
        BOOST_THROW_EXCEPTION(AbortException());
    }
}

template <class String>
void
Suite::fail(String const& reason, char const* file, int line)
{
    fail(detail::makeReason(reason, file, line));
}

inline void
Suite::propagateAbort() const
{
    if (abort_ && aborted_)
        BOOST_THROW_EXCEPTION(AbortException());
}

template <class>
void
Suite::run(Runner& r)
{
    runner_ = &r;

    try
    {
        run();
    }
    catch (AbortException const&)  // NOLINT(bugprone-empty-catch)
    {
        // ends the suite
    }
    catch (std::exception const& e)
    {
        runner_->fail("unhandled exception: " + std::string(e.what()));
    }
    catch (...)
    {
        runner_->fail("unhandled exception");
    }
}

#ifndef BEAST_EXPECT
/** Check a precondition.

    If the condition is false, the file and line number are reported.
*/
#define BEAST_EXPECT(cond) expect(cond, __FILE__, __LINE__)
#endif

#ifndef BEAST_EXPECTS
/** Check a precondition.

    If the condition is false, the file and line number are reported.
*/
#define BEAST_EXPECTS(cond, reason) \
    ((cond) ? (pass(), true) : (fail((reason), __FILE__, __LINE__), false))
#endif

}  // namespace beast::unit_test

//------------------------------------------------------------------------------

// detail:
// This inserts the suite with the given manual flag
#define BEAST_DEFINE_TESTSUITE_INSERT(Class, Module, Library, manual, priority) \
    static beast::unit_test::detail::InsertSuite<Class##_test>                  \
        Library##Module##Class##_test_instance(#Class, #Module, #Library, manual, priority)

//------------------------------------------------------------------------------

// Preprocessor directives for controlling unit test definitions.

// If this is already defined, don't redefine it. This allows
// programs to provide custom behavior for testsuite definitions
//
#ifndef BEAST_DEFINE_TESTSUITE

/** Enables insertion of test suites into the global container.
    The default is to insert all test suite definitions into the global
    container. If BEAST_DEFINE_TESTSUITE is user defined, this macro
    has no effect.
*/
#ifndef BEAST_NO_UNIT_TEST_INLINE
#define BEAST_NO_UNIT_TEST_INLINE 0
#endif

/** Define a unit test suite.

    Class     The type representing the class being tested.
    Module    Identifies the module.
    Library   Identifies the library.

    The declaration for the class implementing the test should be the same
    as Class ## _test. For example, if Class is aged_ordered_container, the
    test class must be declared as:

    @code

    struct aged_ordered_container_test : beast::unit_test::suite
    {
        //...
    };

    @endcode

    The macro invocation must appear in the same namespace as the test class.

    Unit test priorities were introduced so parallel unit_test::suites would
    execute faster. Suites with longer running times have higher priorities
    than unit tests with shorter running times.  Suites with no priorities
    are assumed to run most quickly, so they run last.
*/

#if BEAST_NO_UNIT_TEST_INLINE
#define BEAST_DEFINE_TESTSUITE(Class, Module, Library)
#define BEAST_DEFINE_TESTSUITE_MANUAL(Class, Module, Library)
#define BEAST_DEFINE_TESTSUITE_PRIO(Class, Module, Library, Priority)
#define BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(Class, Module, Library, Priority)

#else
#include <xrpl/beast/unit_test/global_suites.h>
#define BEAST_DEFINE_TESTSUITE(Class, Module, Library) \
    BEAST_DEFINE_TESTSUITE_INSERT(Class, Module, Library, false, 0)
#define BEAST_DEFINE_TESTSUITE_MANUAL(Class, Module, Library) \
    BEAST_DEFINE_TESTSUITE_INSERT(Class, Module, Library, true, 0)
#define BEAST_DEFINE_TESTSUITE_PRIO(Class, Module, Library, Priority) \
    BEAST_DEFINE_TESTSUITE_INSERT(Class, Module, Library, false, Priority)
#define BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(Class, Module, Library, Priority) \
    BEAST_DEFINE_TESTSUITE_INSERT(Class, Module, Library, true, Priority)
#endif

#endif

//------------------------------------------------------------------------------
