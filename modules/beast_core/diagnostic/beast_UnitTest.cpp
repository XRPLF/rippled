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

UnitTest::UnitTest (String const& name,
                    String const& group,
                    When when)
    : m_name (name)
    , m_group (group)
    , m_when (when)
    , m_runner (nullptr)
{
    getAllTests().add (this);
}

UnitTest::~UnitTest()
{
    getAllTests().removeFirstMatchingValue (this);
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

void UnitTest::performTest (UnitTests* const runner)
{
    bassert (runner != nullptr);
    m_runner = runner;

    initialise();
    runTest();
    shutdown();
}

void UnitTest::logMessage (const String& message)
{
    m_runner->logMessage (message);
}

void UnitTest::beginTest (const String& testName)
{
    m_runner->beginNewTest (this, testName);
}

void UnitTest::pass ()
{
    m_runner->addPass();
}

void UnitTest::fail (String const& failureMessage)
{
    m_runner->addFail (failureMessage);
}

void UnitTest::expect (const bool result, const String& failureMessage)
{
    if (result)
    {
        pass ();
    }
    else
    {
        fail (failureMessage);
    }
}

//==============================================================================

UnitTests::UnitTests()
    : currentTest (nullptr),
      assertOnFailure (true),
      logPasses (false)
{
}

UnitTests::~UnitTests()
{
}

void UnitTests::setAssertOnFailure (bool shouldAssert) noexcept
{
    assertOnFailure = shouldAssert;
}

void UnitTests::setPassesAreLogged (bool shouldDisplayPasses) noexcept
{
    logPasses = shouldDisplayPasses;
}

int UnitTests::getNumResults() const noexcept
{
    return results.size();
}

const UnitTests::TestResult* UnitTests::getResult (int index) const noexcept
{
    return results [index];
}

bool UnitTests::anyTestsFailed () const noexcept
{
    for (int i = 0; i < results.size (); ++i)
    {
        if (results [i]->failures > 0)
            return true;
    }

    return false;
}

void UnitTests::resultsUpdated()
{
}

void UnitTests::runTest (UnitTest& test)
{
    try
    {
        test.performTest (this);
    }
    catch (std::exception& e)
    {
        String s;
        s << "Got an exception: " << e.what ();
        addFail (s);
    }
    catch (...)
    {
        addFail ("Got an unhandled exception");
    }
}

void UnitTests::runTest (String const& name)
{
    results.clear();
    resultsUpdated();

    UnitTest::TestList& tests (UnitTest::getAllTests ());

    for (int i = 0; i < tests.size(); ++i)
    {
        UnitTest& test = *tests [i];

        if (test.getGroup () == name && test.getWhen () == UnitTest::runAlways)
        {
            runTest (test);
        }
        else if (test.getName () == name)
        {
            runTest (test);
            break;
        }

    }
}

void UnitTests::runAllTests ()
{
    UnitTest::TestList& tests (UnitTest::getAllTests ());

    results.clear();
    resultsUpdated();

    for (int i = 0; i < tests.size(); ++i)
    {
        if (shouldAbortTests())
            break;

        UnitTest& test = *tests [i];

        if (test.getWhen () == UnitTest::runAlways)
            runTest (test);
    }

    endTest();

}

void UnitTests::logMessage (const String& message)
{
    Logger::writeToLog (message);
}

bool UnitTests::shouldAbortTests()
{
    return false;
}

void UnitTests::beginNewTest (UnitTest* const test, const String& subCategory)
{
    endTest();
    currentTest = test;

    TestResult* const r = new TestResult();
    results.add (r);
    r->unitTestName = test->getGroup() + "::" + test->getName();
    r->subcategoryName = subCategory;
    r->passes = 0;
    r->failures = 0;

    logMessage ("Test '" + r->unitTestName + "': " + subCategory);

    resultsUpdated ();
}

void UnitTests::endTest()
{
    if (results.size() > 0)
    {
        TestResult* const r = results.getLast();

        if (r->failures > 0)
        {
            String m ("FAILED!!  ");
            m << r->failures << (r->failures == 1 ? " test" : " tests")
              << " failed, out of a total of " << (r->passes + r->failures);

            logMessage (String::empty);
            logMessage (m);
            logMessage (String::empty);
        }
        else
        {
            //logMessage ("All tests completed successfully");
        }
    }
}

void UnitTests::addPass()
{
    {
        const ScopedLock sl (results.getLock());

        TestResult* const r = results.getLast();
        bassert (r != nullptr); // You need to call UnitTest::beginTest() before performing any tests!

        r->passes++;

        if (logPasses)
        {
            String message ("Test ");
            message << (r->failures + r->passes) << " passed";
            logMessage (message);
        }
    }

    resultsUpdated();
}

void UnitTests::addFail (const String& failureMessage)
{
    {
        const ScopedLock sl (results.getLock());

        TestResult* const r = results.getLast();
        bassert (r != nullptr); // You need to call UnitTest::beginTest() before performing any tests!

        r->failures++;

        String message ("Failure, #");
        message << (r->failures + r->passes);

        if (failureMessage.isNotEmpty())
            message << ": " << failureMessage;

        r->messages.add (message);

        logMessage (message);
    }

    resultsUpdated();

    if (assertOnFailure) { bassertfalse; }
}
