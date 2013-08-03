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

UnitTest::UnitTest (String const& className,
                    String const& packageName,
                    When when)
    : m_className (className)
    , m_packageName (packageName)
    , m_when (when)
    , m_runner (nullptr)
{
    getAllTests().add (this);
}

UnitTest::~UnitTest()
{
    getAllTests().removeFirstMatchingValue (this);
}

String const& UnitTest::getClassName() const noexcept
{
    return m_className;
}

String const& UnitTest::getPackageName() const noexcept
{
    return m_packageName;
}

UnitTest::TestList& UnitTest::getAllTests()
{
    static TestList s_tests;

    return s_tests;
}

void UnitTest::initialise()
{
}

void UnitTest::shutdown()
{
}

ScopedPointer <UnitTest::Suite>& UnitTest::run (UnitTests* const runner)
{
    bassert (runner != nullptr);
    m_runner = runner;

    m_suite = new Suite (m_className, m_packageName);

    initialise();

    try
    {
        runTest();
    }
    catch (...)
    {
        failException ();
    }

    shutdown();

    finishCase ();

    m_suite->secondsElapsed = RelativeTime (
        Time::getCurrentTime () - m_suite->whenStarted).inSeconds ();

    return m_suite;
}

void UnitTest::logMessage (String const& message)
{
    m_runner->logMessage (message);
}

void UnitTest::beginTestCase (String const& name)
{
    finishCase ();

    String s;
    s << m_packageName << "/" << m_className << ": " << name;
    logMessage (s);

    m_case = new Case (name, m_className);
}

bool UnitTest::expect (bool trueCondition, String const& failureMessage)
{
    if (trueCondition)
    {
        pass ();
    }
    else
    {
        fail (failureMessage);
    }

    return trueCondition;
}

bool UnitTest::unexpected (bool falseCondition, String const& failureMessage)
{
    if (! falseCondition)
    {
        pass ();
    }
    else
    {
        fail (failureMessage);
    }

    return ! falseCondition;
}

void UnitTest::pass ()
{
    // If this goes off it means you forgot to call beginTestCase()!
    bassert (m_case != nullptr);

    m_case->items.add (Item (true));
}

void UnitTest::fail (String const& failureMessage)
{
    // If this goes off it means you forgot to call beginTestCase()!
    bassert (m_case != nullptr);

    Item item (false, failureMessage);

    m_case->failures++;
    int const caseNumber = m_case->items.add (Item (false, failureMessage));

    String s;
    s << "#" << String (caseNumber) << " failed: " << failureMessage;
    logMessage (s);

    m_runner->onFailure ();
}

void UnitTest::failException ()
{
    Item item (false, "An exception was thrown");

    if (m_case != nullptr)
    {
        m_case->failures++;
    }
    else
    {
        // This hack gives us a test case, to handle the condition where an
        // exception was thrown before beginTestCase() was called.
        //
        beginTestCase ("Exception outside test case");
    }

    int const caseNumber = m_case->items.add (item);

    String s;
    s << "#" << String (caseNumber) << " threw an exception ";
    logMessage (s);

    m_runner->onFailure ();
}

//------------------------------------------------------------------------------

void UnitTest::finishCase ()
{
    if (m_case != nullptr)
    {
        // If this goes off it means you forgot to
        // report any passing test case items!
        //
        bassert (m_case->items.size () > 0);

        m_case->secondsElapsed = RelativeTime (
            Time::getCurrentTime () - m_case->whenStarted).inSeconds ();

        m_suite->tests += m_case->items.size ();
        m_suite->failures += m_case->failures;

        m_suite->cases.add (m_case.release ());
    }
}

//==============================================================================

UnitTests::UnitTests()
    : m_assertOnFailure (false)
{
}

UnitTests::~UnitTests()
{
}

void UnitTests::setAssertOnFailure (bool shouldAssert) noexcept
{
    m_assertOnFailure = shouldAssert;
}

UnitTests::Results const& UnitTests::getResults () const noexcept
{
    return *m_results;
}

bool UnitTests::anyTestsFailed () const noexcept
{
    return m_results->failures > 0;
}

void UnitTests::runTests (Array <UnitTest*> const& tests)
{
    m_results = new Results;

    for (int i = 0; i < tests.size (); ++i)
    {
        if (shouldAbortTests())
            break;

        runTest (*tests [i]);
    }

    m_results->secondsElapsed = RelativeTime (
        Time::getCurrentTime () - m_results->whenStarted).inSeconds ();
}

void UnitTests::runAllTests ()
{
    UnitTest::TestList const& allTests (UnitTest::getAllTests ());

    Array <UnitTest*> tests;

    tests.ensureStorageAllocated (allTests.size ());

    for (int i = 0; i < allTests.size(); ++i)
    {
        UnitTest* const test = allTests [i];

        if (test->getWhen () == UnitTest::runNormal)
        {
            tests.add (test);
        }
    }

    runTests (tests);
}

void UnitTests::runStartupTests ()
{
    UnitTest::TestList const& allTests (UnitTest::getAllTests ());

    Array <UnitTest*> tests;

    tests.ensureStorageAllocated (allTests.size ());

    for (int i = 0; i < allTests.size(); ++i)
    {
        UnitTest* const test = allTests [i];

        if (test->getWhen () == UnitTest::runStartup)
        {
            tests.add (test);
        }
    }

    runTests (tests);
}

void UnitTests::runTestsByName (String const& name)
{
    UnitTest::TestList const& allTests (UnitTest::getAllTests ());

    Array <UnitTest*> tests;

    tests.ensureStorageAllocated (allTests.size ());

    for (int i = 0; i < allTests.size(); ++i)
    {
        UnitTest* const test = allTests [i];

        if (test->getPackageName () == name && 
            (test->getWhen () == UnitTest::runNormal ||
             test->getWhen () == UnitTest::runStartup))
        {
            tests.add (test);
        }
        else if (test->getClassName () == name)
        {
            tests.add (test);
            break;
        }
    }

    runTests (tests);
}
 
void UnitTests::onFailure ()
{
    // A failure occurred and the setting to assert on failures is turned on.
    bassert (! m_assertOnFailure)
}

bool UnitTests::shouldAbortTests()
{
    return false;
}

void UnitTests::logMessage (const String& message)
{
    Logger::writeToLog (message);
}

void UnitTests::runTest (UnitTest& test)
{
    try
    {
        ScopedPointer <UnitTest::Suite> suite (test.run (this).release ());

        m_results->cases += suite->cases.size ();
        m_results->tests += suite->tests;
        m_results->failures += suite->failures;

        m_results->suites.add (suite.release ());
    }
    catch (...)
    {
        // Should never get here.
        Throw (std::runtime_error ("unhandled exception during unit tests"));
    }
}
