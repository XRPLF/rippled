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

    /** The type of a list of tests. */
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

    /** Returns the fully qualified test name in the form <package>.<class> */
    String getTestName() const noexcept;

    /** Returns the class name of the test. */
    const String& getClassName() const noexcept;

    /** Returns the package name of the test. */
    String const& getPackageName () const noexcept;

    /** Returns the run option of the test. */
    When getWhen () const noexcept { return m_when; }

    /** Returns a Journal that logs to the UnitTests. */
    Journal journal () const;

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
    bool expect (bool trueCondition, String const& failureMessage = String::empty);

    /** Checks that the result of a test is false, and logs this result.

        This is basically the opposite of expect().

        @see expect
    */
    bool unexpected (bool falseCondition, String const& failureMessage = String::empty);

    /** Compares two values, and if they don't match, prints out a message containing the
        expected and actual result values.
    */
    template <class ActualType, class ExpectedType>
    bool expectEquals (ActualType actual, ExpectedType expected, String failureMessage = String::empty)
    {
        const bool result = (actual == expected);

        if (! result)
        {
            if (failureMessage.isNotEmpty())
                failureMessage << " -- ";

            failureMessage << "Expected value: " << expected << ", Actual value: " << actual;
        }

        return expect (result, failureMessage);
    }

    /** Causes the test item to pass. */
    void pass ();

    /** Causes the test item to fail. */
    void fail (String const& failureMessage = String::empty);

    /** Records an exception in the test item. */
    void failException ();

    //==============================================================================
    /** Writes a message to the test log.
        This can only be called during your runTest() method.
    */
    void logMessage (const String& message);

    void logReport (StringArray const& report);

    /** Returns a shared RNG that all unit tests should use. */
    Random& random();

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
    Random m_random;
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
class BEAST_API UnitTests : public Uncopyable
{
public:
    typedef UnitTest::TestList TestList;

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

    /** Selects zero or more tests from specified packages or test names.

        The name can be in these formats:
            ""
            <package | testname>
            <package> "."
            <package> "." <testname>
            "." <testname>

        ""
            An empty string will match all tests objects which are not
            marked to be run manually.

        <package | testname>
            Selects all tests which belong to that package, excluding those
            which must be run manually. If no package with that name exists,
            then this will select the first test from any package which matches
            the name. If the test is a manual test, it will be selected.

        <package> "."
            Selects all tests which belong to that package, excluding those
            which must be run manually. If no package with that name exists,
            then no tests will be selected.

        <package> "." <testname>
            Selects only the first test that matches the given testname and
            package, regardless of the manual run setting. If no test with a
            matching package and test name is found, then no test is selected.

        "." <testname>
            Selects the first test which matches the testname, even if it
            is a manual test.

        Some examples of names:

            "beast"             All unit tests in beast
            "beast.File"        Just the File beast unit test
            ".Random"           The first test with the name Random

        @note Matching is not case-sensitive.

        @param match The string used to match tests
        @param tests An optional parameter containing a list of tests to match.
    */
    TestList selectTests (String const& match = "",
                          TestList const& tests = UnitTest::getAllTests ()) const noexcept;

    /** Selects all tests which match the specified package.

        Tests marked to be run manually are not included.
        
        @note Matching is not case-sensitive.

        @param match The string used to match tests
        @param tests An optional parameter containing a list of tests to match.
    */
    TestList selectPackage (String const& package,
                            TestList const& tests = UnitTest::getAllTests ()) const noexcept;

    /** Selects the first test whose name matches, from any package.

        This can include tests marked to be run manually.
        
        @note Matching is not case-sensitive.
        
        @param match The name of the test to match.
        @param tests An optional parameter containing a list of tests to match.
    */
    TestList selectTest (String const& testname,
                         TestList const& tests = UnitTest::getAllTests ()) const noexcept;

    /** Selects the startup tests.
        A test marked as runStartup will be forced to run on launch.
        Typically these are lightweight tests that ensure the system
        environment will not cause the program to exhibit undefined behavior.

        @param tests An optional parameter containing a list of tests to match.
    */
    TestList selectStartupTests (TestList const& tests = UnitTest::getAllTests ()) const noexcept;

    /** Run a list of matching tests.
        This first calls selectTests and then runTeests on the resulting list.
        @param match The string used for matching.
        @param tests An optional parameter containing a list of tests to match.
    */
    void runSelectedTests (String const& match = "",
                           TestList const& tests = UnitTest::getAllTests (),
                           int64 randomSeed = 0);

    /** Runs the specified list of tests.
        @note The tests are run regardless of the run settings.
        @param tests The list of tests to run.
    */
    void runTests (TestList const& tests, int64 randomSeed = 0);

    Journal journal ()
    {
        return Journal (m_sink);
    }

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

    /** Logs a report about the current test progress.
        This calls logMessage for each String.
    */
    virtual void logReport (StringArray const& report);

private:
    void runTest (UnitTest& test);

private:
    class JournalSink : public Journal::Sink, public Uncopyable
    {
    public:
        explicit JournalSink (UnitTests& tests);
        void write (Journal::Severity severity, std::string const& text);
        bool active (Journal::Severity severity);
        bool console ();
        void set_severity (Journal::Severity severity);
        void set_console (bool);

    private:
        UnitTests& m_tests;
    };

    bool m_assertOnFailure;
    ScopedPointer <Results> m_results;
    Random m_random;
    JournalSink m_sink;
};

#endif
