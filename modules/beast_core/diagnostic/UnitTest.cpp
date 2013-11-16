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
    : m_className (className.removeCharacters (" "))
    , m_packageName (packageName.removeCharacters (" "))
    , m_when (when)
    , m_runner (nullptr)
{
    getAllTests().add (this);
}

UnitTest::~UnitTest()
{
    getAllTests().removeFirstMatchingValue (this);
}

String UnitTest::getTestName() const noexcept
{
    String s;
    s << m_packageName << "." << m_className;
    return s;
}

String const& UnitTest::getClassName() const noexcept
{
    return m_className;
}

String const& UnitTest::getPackageName() const noexcept
{
    return m_packageName;
}

Journal UnitTest::journal () const
{
    bassert (m_runner != nullptr);
    return m_runner->journal();
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

ScopedPointer <UnitTest::Suite>& UnitTest::run (
    UnitTests* const runner)
{
    bassert (runner != nullptr);
    m_runner = runner;
    m_random = m_runner->m_random;

    m_suite = new Suite (m_className, m_packageName);

    initialise();

#if 0
    try
    {
        runTest();
    }
    catch (...)
    {
        failException ();
    }
#else
    runTest ();
#endif

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

void UnitTest::logReport (StringArray const& report)
{
    m_runner->logReport (report);
}

void UnitTest::beginTestCase (String const& name)
{
    finishCase ();

    String s;
    s << getTestName () << " : " << name;
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
    return expect (! falseCondition, failureMessage);
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

Random& UnitTest::random()
{
    // This method's only valid while the test is being run!
    bassert (m_runner != nullptr);

    return m_random;
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

UnitTests::JournalSink::JournalSink (UnitTests& tests)
    : m_tests (tests)
{
}

bool UnitTests::JournalSink::active (Journal::Severity) const
{
    return true;
}

bool UnitTests::JournalSink::console() const
{
    return false;
}

void UnitTests::JournalSink::console (bool)
{
}

Journal::Severity UnitTests::JournalSink::severity() const
{
    return Journal::kLowestSeverity;
}

void UnitTests::JournalSink::severity (Journal::Severity)
{
}

void UnitTests::JournalSink::write (Journal::Severity, std::string const& text)
{
    m_tests.logMessage (text);
}

//==============================================================================

UnitTests::UnitTests()
    : m_assertOnFailure (false)
    , m_sink (*this)
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

UnitTests::TestList UnitTests::selectTests (
    String const& match, TestList const& tests) const noexcept
{
    TestList list;
    list.ensureStorageAllocated (tests.size ());

    int const indexOfDot = match.indexOfChar ('.');
    String const package = (indexOfDot == -1) ? match : match.substring (0, indexOfDot);
    String const testname = (indexOfDot == -1) ? ""
        : match.substring (indexOfDot + 1, match.length () + 1);

    if (package != String::empty)
    {
        if (testname != String::empty)
        {
            // "package.testname" : First test which matches
            for (int i = 0; i < tests.size(); ++i)
            {
                UnitTest* const test = tests [i];
                if (package.equalsIgnoreCase (test->getPackageName ()) &&
                    testname.equalsIgnoreCase (test->getClassName ()))
                {
                    list.add (test);
                    break;
                }
            }
        }
        else
        {
            // Get all tests in the package
            list = selectPackage (package, tests);

            // If no trailing slash on package, try tests
            if (list.size () == 0 && indexOfDot == -1)
            {
                // Try "package" as a testname
                list = selectTest (package, tests);
            }
        }
    }
    else if (testname != String::empty)
    {
        list = selectTest (testname, tests);
    }
    else
    {
        // All non manual tests
        for (int i = 0; i < tests.size(); ++i)
        {
            UnitTest* const test = tests [i];
            if (test->getWhen () != UnitTest::runManual)
                list.add (test);
        }
    }

    return list;
}

UnitTests::TestList UnitTests::selectPackage (
    String const& package, TestList const& tests) const noexcept
{
    TestList list;
    list.ensureStorageAllocated (tests.size ());
    for (int i = 0; i < tests.size(); ++i)
    {
        UnitTest* const test = tests [i];
        if (package.equalsIgnoreCase (test->getPackageName ()) &&
            test->getWhen () != UnitTest::runManual)
            list.add (test);
    }
    return list;
}

UnitTests::TestList UnitTests::selectTest (
    String const& testname, TestList const& tests) const noexcept
{
    TestList list;
    for (int i = 0; i < tests.size(); ++i)
    {
        UnitTest* const test = tests [i];
        if (testname.equalsIgnoreCase (test->getClassName ()))
        {
            list.add (test);
            break;
        }
    }
    return list;
}

UnitTests::TestList UnitTests::selectStartupTests (TestList const& tests) const noexcept
{
    TestList list;
    for (int i = 0; i < tests.size(); ++i)
    {
        UnitTest* const test = tests [i];
        if (test->getWhen () == UnitTest::runStartup)
            list.add (test);
    }
    return list;
}

void UnitTests::runSelectedTests (String const& match, TestList const& tests, int64 randomSeed)
{
    runTests (selectTests (match, tests), randomSeed);
}

void UnitTests::runTests (TestList const& tests, int64 randomSeed)
{
    if (randomSeed == 0)
        randomSeed = Random().nextInt (0x7fffffff);
    m_random = Random (randomSeed);

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

void UnitTests::logReport (StringArray const& report)
{
    for (int i = 0; i < report.size (); ++i)
        logMessage (report [i]);
}

void UnitTests::runTest (UnitTest& test)
{
#if 0
    try
    {
#endif
        ScopedPointer <UnitTest::Suite> suite (test.run (this).release ());

        m_results->cases += suite->cases.size ();
        m_results->tests += suite->tests;
        m_results->failures += suite->failures;

        m_results->suites.add (suite.release ());
#if 0
    }
    catch (...)
    {
        // Should never get here.
        Throw (std::runtime_error ("unhandled exception during unit tests"));
    }
#endif
}

//------------------------------------------------------------------------------

/** A UnitTest that prints the list of available unit tests.
    Not an actual test (it always passes) but if you run it manually it
    will print a list of the names of all available unit tests in the program.
*/
class UnitTestsPrinter : public UnitTest
{
public:
    UnitTestsPrinter () : UnitTest ("print", "print", runManual)
    {
    }

    void runTest ()
    {
        beginTestCase ("List available unit tests");

        TestList const& list (UnitTest::getAllTests ());

        for (int i = 0; i < list.size (); ++i)
        {
            UnitTest const& test (*list [i]);

            String s;
            switch (test.getWhen ())
            {
            default:
            case UnitTest::runNormal:  s << "         "; break;
            case UnitTest::runManual:  s << "[manual] "; break;
            case UnitTest::runStartup: s << "[FORCED] "; break;
            };

            s << test.getTestName ();

            logMessage (s);
        }

        pass ();
    }
};

static UnitTestsPrinter unitTestsPrinter;
