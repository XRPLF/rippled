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

#ifndef BEAST_UNIT_TEST_SUITE_H_INCLUDED
#define BEAST_UNIT_TEST_SUITE_H_INCLUDED

#include <beast/unit_test/runner.h>

#include <beast/utility/noexcept.h>
#include <string>
#include <sstream>

namespace beast {
namespace unit_test {

/** A testsuite class.
    Derived classes execute a series of testcases, where each testcase is
    a series of pass/fail tests. To provide a unit test using this class,
    derive from it and use the BEAST_DEFINE_UNIT_TEST macro in a
    translation unit.
*/
class suite
{
public:
    enum abort_t
    {
        no_abort_on_fail,
        abort_on_fail
    };

private:
    bool abort_ = false;
    runner* runner_ = nullptr;

    struct abort_exception;

    // Memberspace
    class log_t
    {
    private:
        friend class suite;
        suite* suite_ = nullptr;

    public:
        log_t () = default;

        template <class T>
        abstract_ostream::scoped_stream_type
        operator<< (T const& t);

        /** Returns the raw stream used for output. */
        abstract_ostream&
        stream();
    };

    class scoped_testcase;

    // Memberspace
    class testcase_t
    {
    private:
        friend class suite;
        suite* suite_ = nullptr;
        std::stringstream ss_;

    public:
        testcase_t() = default;

        /** Open a new testcase.
            A testcase is a series of evaluated test conditions. A test suite
            may have multiple test cases. A test is associated with the last
            opened testcase. When the test first runs, a default unnamed
            case is opened. Tests with only one case may omit the call
            to testcase.
            @param abort If `true`, the suite will be stopped on first failure.
        */
        void
        operator() (std::string const& name,
            abort_t abort = no_abort_on_fail);

        /** Stream style composition of testcase names. */
        /** @{ */
        scoped_testcase
        operator() (abort_t abort);

        template <class T>
        scoped_testcase
        operator<< (T const& t);
        /** @} */
    };

    // Hacks to make this header-only
    
    template <class = void>
    void
    run (runner& r);

    template <class = void>
    void
    do_fail (std::string const& reason);

public:
    /** Type for scoped stream logging.
        To use this type, declare a local variable of the type
        on the stack in derived class member function and construct
        it from log.stream();

        @code

        scoped_stream ss (log.stream();

        ss << "Hello" << std::endl;
        ss << "world" << std::endl;

        @endcode

        Streams constructed in this fashion will not have the line
        ending automatically appended.

        Thread safety:

            The scoped_stream may only be used by one thread.
            Multiline output sent to the stream will be atomically
            written to the underlying abstract_Ostream
    */
    typedef abstract_ostream::scoped_stream_type scoped_stream;

    /** Memberspace for logging. */
    log_t log;

    /** Memberspace for declaring test cases. */
    testcase_t testcase;

    /** Invokes the test using the specified runner.
        Data members are set up here instead of the constructor as a
        convenience to writing the derived class to avoid repetition of
        forwarded constructor arguments to the base.
        Normally this is called by the framework for you.
    */
    void
    operator() (runner& r);

    /** Evaluate a test condition.
        The condition is passed as a template argument instead of `bool` so
        that implicit conversion is not required. The `reason` argument is
        logged if the condition is false.
        @return `true` if the test condition indicates success.
    */
    template <class Condition>
    bool
    expect (Condition shouldBeTrue, std::string const& reason = "");

    // DEPRECATED
    // @return `true` if the test condition indicates success (a false value)
    template <class Condition>
    bool
    unexpected (Condition shouldBeFalse, std::string const& reason = "");

    /** Record a successful test condition. */
    void
    pass()
    {
        runner_->pass();
    }

    /** Record a failure. */
    void
    fail (std::string const& reason = "");

private:
    /** Runs the suite. */
    virtual
    void
    run() = 0;
};

//------------------------------------------------------------------------------

// This exception is thrown internally to stop the current suite
// in the event of a failure, if the option to stop is set.
struct suite::abort_exception : public std::exception
{
    char const*
    what() const noexcept override;
};

inline
char const*
suite::abort_exception::what() const noexcept
{
    return "suite aborted on failed condition";
}

//------------------------------------------------------------------------------

template <class T>
inline
abstract_ostream::scoped_stream_type
suite::log_t::operator<< (T const& t)
{
    return suite_->runner_->stream() << t;
}

/** Returns the raw stream used for output. */
inline
abstract_ostream&
suite::log_t::stream()
{
    return suite_->runner_->stream();
}

//------------------------------------------------------------------------------

// Helper for streaming testcase names
class suite::scoped_testcase
{
private:
    suite* suite_;
    std::stringstream* ss_;

public:
    ~scoped_testcase();

    scoped_testcase (suite* s, std::stringstream* ss);

    template <class T>
    scoped_testcase (suite* s, std::stringstream* ss, T const& t);

    scoped_testcase& operator= (scoped_testcase const&) = delete;

    template <class T>
    scoped_testcase&
    operator<< (T const& t);
};

inline
suite::scoped_testcase::~scoped_testcase()
{
    auto const& name (ss_->str());
    if (! name.empty())
        suite_->runner_->testcase (name);
}

inline
suite::scoped_testcase::scoped_testcase (suite* s, std::stringstream* ss)
    : suite_ (s)
    , ss_ (ss)
{
    ss_->clear();
    ss_->str({});

}

template <class T>
inline
suite::scoped_testcase::scoped_testcase (suite* s, std::stringstream* ss, T const& t)
    : suite_ (s)
    , ss_ (ss)
{
    ss_->clear();
    ss_->str({});
    *ss_ << t;
}

template <class T>
inline
suite::scoped_testcase&
suite::scoped_testcase::operator<< (T const& t)
{
    *ss_ << t;
    return *this;
}

//------------------------------------------------------------------------------

inline
void
suite::testcase_t::operator() (std::string const& name,
    abort_t abort)
{
    suite_->abort_ = abort == abort_on_fail;
    suite_->runner_->testcase (name);
}

inline
suite::scoped_testcase
suite::testcase_t::operator() (abort_t abort)
{
    suite_->abort_ = abort == abort_on_fail;
    return { suite_, &ss_ };
}

template <class T>
inline
suite::scoped_testcase
suite::testcase_t::operator<< (T const& t)
{
    return { suite_, &ss_, t };
}

//------------------------------------------------------------------------------

template <class>
void
suite::run (runner& r)
{
    runner_ = &r;
    log.suite_ = this;
    testcase.suite_ = this;

    try
    {
        run();
    }
    catch (abort_exception const&)
    {
        // ends the suite
    }
    catch (std::exception const& e)
    {
        fail (std::string ("unhandled exception: ") +
            std::string (e.what()));
    }
    catch (...)
    {
        fail ("unhandled exception");
    }
}

inline
void
suite::operator() (runner& r)
{
    run (r);
}

template <class Condition>
inline
bool
suite::expect (Condition shouldBeTrue, std::string const& reason)
{
    if (shouldBeTrue)
        pass();
    else
        do_fail (reason);
    return shouldBeTrue;
}

template <class Condition>
inline
bool
suite::unexpected (Condition shouldBeFalse, std::string const& reason)
{
    if (! shouldBeFalse)
        pass();
    else
        do_fail (reason);
    return ! shouldBeFalse;
}

template <class>
void
suite::do_fail (std::string const& reason)
{
    runner_->fail (reason);
    if (abort_)
        throw abort_exception();
}

inline
void
suite::fail (std::string const& reason)
{
    do_fail (reason);
}

} // unit_test
} // beast

//------------------------------------------------------------------------------

// detail:
// This inserts the suite with the given manual flag
#define BEAST_DEFINE_TESTSUITE_INSERT(Class,Module,Library,manual) \
    static beast::unit_test::detail::global_suite_instance <Class##_test>    \
        Library ## Module ## Class ## _test_instance (                       \
            #Class, #Module, #Library, manual);

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
*/

#if BEAST_NO_UNIT_TEST_INLINE
#define BEAST_DEFINE_TESTSUITE(Class,Module,Library)

#else
#include <beast/unit_test/global_suites.h>
#define BEAST_DEFINE_TESTSUITE(Class,Module,Library) \
        BEAST_DEFINE_TESTSUITE_INSERT(Class,Module,Library,false)
#define BEAST_DEFINE_TESTSUITE_MANUAL(Class,Module,Library) \
        BEAST_DEFINE_TESTSUITE_INSERT(Class,Module,Library,true)

#endif

#endif

//------------------------------------------------------------------------------

#endif
