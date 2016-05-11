//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_SUITE_HPP
#define BEAST_UNIT_TEST_SUITE_HPP

#include <beast/unit_test/runner.hpp>
#include <string>
#include <sstream>

namespace beast {
namespace unit_test {

class thread;

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
    bool aborted_ = false;
    runner* runner_ = nullptr;

    // This exception is thrown internally to stop the current suite
    // in the event of a failure, if the option to stop is set.
    struct abort_exception : public std::exception
    {
        char const*
        what() const noexcept override
        {
            return "suite aborted";
        }
    };

public:
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
private:

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
    using scoped_stream = abstract_ostream::scoped_stream_type;

    /** Memberspace for logging. */
    log_t log;

    /** Memberspace for declaring test cases. */
    testcase_t testcase;

    /** Returns the "current" running suite.
        If no suite is running, nullptr is returned.
    */
    static suite* this_suite()
    {
        return *p_this_suite();
    }

    /** Invokes the test using the specified runner.
        Data members are set up here instead of the constructor as a
        convenience to writing the derived class to avoid repetition of
        forwarded constructor arguments to the base.
        Normally this is called by the framework for you.
    */
    template<class = void>
    void
    operator() (runner& r);

    /** Evaluate a test condition.
        The condition is passed as a template argument instead of `bool` so
        that implicit conversion is not required. The `reason` argument is
        logged if the condition is false.
        @return `true` if the test condition indicates success.
    */
    template<class Condition, class String>
    bool
    expect(Condition const& shouldBeTrue,
        String const& reason);

    template<class Condition>
    bool
    expect(Condition const& shouldBeTrue)
    {
        return expect(shouldBeTrue, "");
    }

    /** Expect an exception from f() */
    /** @{ */
    template <class F, class String>
    bool
    except (F&& f, String const& reason);

    template <class F>
    bool
    except (F&& f)
    {
        return except(f, "");
    }
    /** @} */

    /** Expect an exception of the given type from f() */
    /** @{ */
    template <class E, class F, class String>
    bool
    except (F&& f, String const& reason);

    template <class E, class F>
    bool
    except (F&& f)
    {
        return except<E>(f, "");
    }
    /** @} */

    /** Fail if f() throws */
    /** @{ */
    template <class F, class String>
    bool
    unexcept (F&& f, String const& reason);

    template <class F>
    bool
    unexcept (F&& f)
    {
        return unexcept(f, "");
    }
    /** @} */

    /** Return the argument associated with the runner. */
    std::string const&
    arg() const
    {
        return runner_->arg();
    }

    // DEPRECATED
    // @return `true` if the test condition indicates success (a false value)
    template <class Condition, class String>
    bool
    unexpected (Condition shouldBeFalse,
        String const& reason);

    template <class Condition>
    bool
    unexpected (Condition shouldBeFalse)
    {
        return unexpected (shouldBeFalse, "");
    }

    /** Record a successful test condition. */
    template <class = void>
    void
    pass();

    /** Record a failure. */
    template <class = void>
    void
    fail (std::string const& reason = "");

private:
    friend class thread;

    static
    suite**
    p_this_suite()
    {
        static suite* pts = nullptr;
        return &pts;
    }

    /** Runs the suite. */
    virtual
    void
    run() = 0;

    void
    propagate_abort();

    template <class = void>
    void
    run (runner& r);
};

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

template<class T>
inline
suite::scoped_testcase
suite::testcase_t::operator<< (T const& t)
{
    return { suite_, &ss_, t };
}

//------------------------------------------------------------------------------

template<class>
void
suite::operator()(runner& r)
{
    *p_this_suite() = this;
    try
    {
        run(r);
        *p_this_suite() = nullptr;
    }
    catch(...)
    {
        *p_this_suite() = nullptr;
        throw;
    }
}

template <class Condition, class String>
inline
bool
suite::expect(Condition const& shouldBeTrue,
    String const& reason)
{
    if(shouldBeTrue)
    {
        pass();
        return true;
    }
    fail(reason);
    return false;
}

template <class F, class String>
bool
suite::except (F&& f, String const& reason)
{
    try
    {
        f();
        fail(reason);
        return false;
    }
    catch(...)
    {
        pass();
    }
    return true;
}

template <class E, class F, class String>
bool
suite::except (F&& f, String const& reason)
{
    try
    {
        f();
        fail(reason);
        return false;
    }
    catch(E const&)
    {
        pass();
    }
    return true;
}

template <class F, class String>
bool
suite::unexcept (F&& f, String const& reason)
{
    try
    {
        f();
        pass();
        return true;
    }
    catch(...)
    {
        fail(reason);
    }
    return false;
}

template <class Condition, class String>
inline
bool
suite::unexpected (Condition shouldBeFalse,
    String const& reason)
{
    bool const b =
        static_cast<bool>(shouldBeFalse);
    if (! b)
        pass();
    else
        fail (reason);
    return ! b;
}

template <class>
void
suite::pass()
{
    propagate_abort();
    runner_->pass();
}

template <class>
void
suite::fail (std::string const& reason)
{
    propagate_abort();
    runner_->fail (reason);
    if (abort_)
    {
        aborted_ = true;
        throw abort_exception();
    }
}

inline
void
suite::propagate_abort()
{
    if (abort_ && aborted_)
        throw abort_exception();
}

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
        runner_->fail ("unhandled exception: " +
            std::string (e.what()));
    }
    catch (...)
    {
        runner_->fail ("unhandled exception");
    }
}

} // unit_test
} // beast

//------------------------------------------------------------------------------

// detail:
// This inserts the suite with the given manual flag
#define BEAST_DEFINE_TESTSUITE_INSERT(Class,Module,Library,manual) \
    static beast::unit_test::detail::insert_suite <Class##_test>   \
        Library ## Module ## Class ## _test_instance (             \
            #Class, #Module, #Library, manual)

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
#include <beast/unit_test/global_suites.hpp>
#define BEAST_DEFINE_TESTSUITE(Class,Module,Library) \
        BEAST_DEFINE_TESTSUITE_INSERT(Class,Module,Library,false)
#define BEAST_DEFINE_TESTSUITE_MANUAL(Class,Module,Library) \
        BEAST_DEFINE_TESTSUITE_INSERT(Class,Module,Library,true)

#endif

#endif

//------------------------------------------------------------------------------

#endif
