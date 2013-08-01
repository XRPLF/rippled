//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_UNITTEST_H_INCLUDED
#define BEAST_UNITTEST_H_INCLUDED

class UnitTests;

/** This is a base class for classes that perform a unit test.

    To write a test using this class, your code should look something like this:

    @code

    class MyTest : public UnitTest
    {
    public:
        MyTest() : UnitTest ("Foobar testing", "packageName") { }

        void runTest()
        {
            beginTestCase ("Part 1");

            expect (myFoobar.doesSomething());
            expect (myFoobar.doesSomethingElse());

            beginTestCase ("Part 2");

            expect (myOtherFoobar.doesSomething());
            expect (myOtherFoobar.doesSomethingElse());

            //...
        }
    };

    // This makes the unit test available in the global list
    // It doesn't have to be static.
    //
    static MyTest myTest;

    @endcode

    To run one or more unit tests, use the UnitTests class.

    @see UnitTests
*/
class BEAST_API UnitTest : public Uncopyable
{
public:
    /** When the test should be run. */
    enum When
    {
        /** Test will be run when @ref runAllTests is called.
            @see runAllTests
        */
        runNormal,

        /** Test will excluded from @ref runAllTests.
            The test can be manually run from @ref runTestsByName.
            @see runAllTests, runTestsByName
        */
        runManual,

        /** Test will be additionlly forced to run on every launch.
            If any failures occur, FatalError is called. The tests will
            also be run from @ref runAllTests or @ref runTestsByName if
            explicitly invoked.

            @see FatalError
        */
        runStartup
    };

    /** Describes a single test item.

        An item created for each call to the test functions, such as @ref expect
        or @expectEquals.
    */
    struct Item
    {
        explicit Item (bool passed_, String failureMessage_ = "")
            : passed (passed_)
            , failureMessage (failureMessage_)
        {
        }

        bool passed;
        String failureMessage;
    };

    /** Describes a test case.
        A test case represents a group of Item objects.
    */
    struct Case
    {
        explicit Case (String const& name_, String const& className_)
            : name (name_)
            , className (className_)
            , whenStarted (Time::getCurrentTime ())
            , secondsElapsed (0)
            , failures (0)
        {
        }

        String name;
        String className;

        Time whenStarted;
        double secondsElapsed;

        int failures;

        Array <Item, CriticalSection> items;
    };

    /** Contains the results of a test.

        One of these objects is instantiated each time UnitTest::beginTestCase() is called, and
        it contains details of the number of subsequent UnitTest::expect() calls that are
        made.
    */
    struct Suite
    {
        String className;
        String packageName;
        Time whenStarted;
        double secondsElapsed;
        int tests;
        int failures;
        OwnedArray <Case, CriticalSection> cases;

        //----

        Suite (String const& className_, String const& packageName_)
            : className (className_)
            , packageName (packageName_)
            , whenStarted (Time::getCurrentTime ()) // hack for now
            , secondsElapsed (0)
            , tests (0)
            , failures (0)
        {
        }

        // for convenience
        String getSuiteName () const noexcept
        {
            String s;
            s << packageName << "::" << className;
            return s;
        }
    };

    /** The type of a list of tests.
    */
    typedef Array <UnitTest*, CriticalSection> TestList;

    //--------------------------------------------------------------------------

    /** Creates a test with the given name, group, and run option.

        The group is used when you want to run all tests in a particular group
        instead of all tests in general. The run option allows you to write some
        tests that are only available manually. For examplem, a performance unit
        test that takes a long time which you might not want to run every time
        you run all tests.
    */
    /*
        suiteName: A name 
        className: The name of the class that the unit test exercises
        packageName: A real or pseudo "namespace" describing the general area of
                     functionality to which the specified class belongs.
                     Examples: "network", "core", "ui"
                     A package name can appear in multiple testsuite instances.
    */    
    explicit UnitTest (String const& name,
                       String const& group = "",
                       When when = runNormal);

    /** Destructor. */
    virtual ~UnitTest();

    /** Returns the class name of the test. */
    const String& getClassName() const noexcept;

    /** Returns the package name of the test. */
    String const& getPackageName () const noexcept;

    /** Returns the run option of the test. */
    When getWhen () const noexcept { return m_when; }

    /** Runs the test, using the specified UnitTests.
        You shouldn't need to call this method directly - use
        UnitTests::runTests() instead.
    */
    ScopedPointer <Suite>& run (UnitTests* runner);

    /** Returns the set of all UnitTest objects that currently exist. */
    static TestList& getAllTests();

    //--------------------------------------------------------------------------

    /** You can optionally implement this method to set up your test.
        This method will be called before runTest().
    */
    virtual void initialise();

    /** You can optionally implement this method to clear up after your test has been run.
        This method will be called after runTest() has returned.
    */
    virtual void shutdown();

    /** Implement this method in your subclass to actually run your tests.

        The content of your implementation should call beginTestCase() and expect()
        to perform the tests.
    */
    virtual void runTest() = 0;

    /** Tells the system that a new subsection of tests is beginning.
        This should be called from your runTest() method, and may be called
        as many times as you like, to demarcate different sets of tests.
    */
    void beginTestCase (String const& name);

    // beginTestCase ()

    /** Checks that the result of a test is true, and logs this result.

        In your runTest() method, you should call this method for each condition that
        you want to check, e.g.

        @code
        void runTest()
        {
            beginTestCase ("basic tests");
            expect (x + y == 2);
            expect (getThing() == someThing);
            ...etc...
        }
        @endcode

        If Suite is true, a pass is logged; if it's false, a failure is logged.
        If the failure message is specified, it will be written to the log if the test fails.
    */
    void expect (bool trueCondition, String const& failureMessage = String::empty);

    /** Checks that the result of a test is false, and logs this result.

        This is basically the opposite of expect().

        @see expect
    */
    void unexpected (bool falseCondition, String const& failureMessage = String::empty);

    /** Compares two values, and if they don't match, prints out a message containing the
        expected and actual result values.
    */
    template <class ActualType, class ExpectedType>
    void expectEquals (ActualType actual, ExpectedType expected, String failureMessage = String::empty)
    {
        const bool result = (actual == expected);

        if (! result)
        {
            if (failureMessage.isNotEmpty())
                failureMessage << " -- ";

            failureMessage << "Expected value: " << expected << ", Actual value: " << actual;
        }

        expect (result, failureMessage);
    }

    /** Causes the test item to pass. */
    void pass ();

    /** Causes the test item to fail. */
    void fail (String const& failureMessage);

    /** Records an exception in the test item. */
    void failException ();

    //==============================================================================
    /** Writes a message to the test log.
        This can only be called during your runTest() method.
    */
    void logMessage (const String& message);

private:
    void finishCase ();

private:
    //==============================================================================
    String const m_className;
    String const m_packageName;
    When const m_when;
    UnitTests* m_runner;
    ScopedPointer <Suite> m_suite;
    ScopedPointer <Case> m_case;
};

//==============================================================================
/**
    Runs a set of unit tests.

    You can instantiate one of these objects and use it to invoke tests on a set of
    UnitTest objects.

    By using a subclass of UnitTests, you can intercept logging messages and
    perform custom behaviour when each test completes.

    @see UnitTest
*/
class BEAST_API UnitTests : Uncopyable
{
public:
    struct Results
    {
        Results ()
            : whenStarted (Time::getCurrentTime ())
            , cases (0)
            , tests (0)
            , failures (0)
        {
        }

        Time whenStarted;
        double secondsElapsed;
        int cases;
        int tests;
        int failures;

        OwnedArray <UnitTest::Suite> suites;
    };

    /** */
    UnitTests();

    /** Destructor. */
    virtual ~UnitTests();

    /** Sets a flag to indicate whether an assertion should be triggered if a test fails.
        This is true by default.
    */
    void setAssertOnFailure (bool shouldAssert) noexcept;

    /** Retrieve the information on all the suites that were run.
        This is overwritten every time new tests are run.
    */
    Results const& getResults () const noexcept;

    /** Returns `true` if any test failed. */
    bool anyTestsFailed () const noexcept;

    //--------------------------------------------------------------------------

    /** Runs the specified list of tests.
        This is used internally and won't normally need to be called.
    */
    void runTests (Array <UnitTest*> const& tests);

    /** Runs all the UnitTest objects that currently exist.
        This calls @ref runTests for all the objects listed in @ref UnitTest::getAllTests.
    */
    void runAllTests ();

    /** Runs the startup tests. */
    void runStartupTests ();

    /** Run a particular test or group. */
    void runTestsByName (String const& name);

protected:
    friend class UnitTest;

    /** Called on a failure. */
    void onFailure ();

    /** This can be overridden to let the runner know that it should abort the tests
        as soon as possible, e.g. because the thread needs to stop.
    */
    virtual bool shouldAbortTests ();

    /** Logs a message about the current test progress.
        By default this just writes the message to the Logger class, but you could override
        this to do something else with the data.
    */
    virtual void logMessage (String const& message);

private:
    void runTest (UnitTest& test);

private:
    bool m_assertOnFailure;
    ScopedPointer <Results> m_results;
    UnitTest* m_currentTest;
};

#endif
