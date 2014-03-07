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

namespace beast
{

namespace UnitTestUtilities
{

JUnitXMLFormatter::JUnitXMLFormatter (UnitTests const& tests)
    : m_tests (tests)
    , m_currentTime (timeToString (Time::getCurrentTime ()))
    , m_hostName (SystemStats::getComputerName ())
{
}

// This is the closest thing to documentation on JUnit XML I could find:
//
// http://junitpdfreport.sourceforge.net/managedcontent/PdfTranslation
//
String JUnitXMLFormatter::createDocumentString ()
{
    UnitTests::Results const& results (m_tests.getResults ());

    ScopedPointer <XmlElement> testsuites (new XmlElement ("testsuites"));
    testsuites->setAttribute ("tests", String (results.tests));
    if (results.failures != 0)
        testsuites->setAttribute ("failures", String (results.failures));
    testsuites->setAttribute ("time", secondsToString (results.secondsElapsed));

    for (int i = 0; i < results.suites.size (); ++i)
    {
        UnitTest::Suite const& suite (*results.suites [i]);

        XmlElement* testsuite (new XmlElement ("testsuite"));;
        testsuite->setAttribute ("name", suite.className);
        testsuite->setAttribute ("tests", String (suite.tests));
        if (suite.failures != 0)
            testsuite->setAttribute ("failures", String (suite.failures));
        testsuite->setAttribute ("time", secondsToString (suite.secondsElapsed));
        testsuite->setAttribute ("timestamp", timeToString (suite.whenStarted));
        testsuite->setAttribute ("hostname", m_hostName);
        testsuite->setAttribute ("package", suite.packageName);
       
        testsuites->addChildElement (testsuite);

        for (int i = 0; i < suite.cases.size (); ++i)
        {
            UnitTest::Case const& testCase (*suite.cases [i]);

            XmlElement* testcase (new XmlElement ("testcase"));
            testcase->setAttribute ("name", testCase.name);
            testcase->setAttribute ("time", secondsToString (testCase.secondsElapsed));
            testcase->setAttribute ("classname", suite.className);

            testsuite->addChildElement (testcase);

            for (int i = 0; i < testCase.items.size (); ++i)
            {
                UnitTest::Item const& item (testCase.items.getUnchecked (i));

                if (!item.passed)
                {
                    XmlElement* failure (new XmlElement ("failure"));

                    String s;
                    s << "#" << String (i+1) << " " << item.failureMessage;
                    failure->setAttribute ("message", s);

                    testcase->addChildElement (failure);
                }
            }
        }
    }

    return testsuites->createDocument (
        //"https://svn.jenkins-ci.org/trunk/hudson/dtkit/dtkit-format/dtkit-junit-model/src/main/resources/com/thalesgroup/dtkit/junit/model/xsd/junit-4.xsd",
        "", 
        false,
        true,
        "UTF-8",
        999);
};

String JUnitXMLFormatter::timeToString (Time const& time)
{
    return time.toString (true, true, false, true);
}

String JUnitXMLFormatter::secondsToString (double seconds)
{
    if (seconds < .01)
        return String (seconds, 4);
    else if (seconds < 1)
        return String (seconds, 2);
    else if (seconds < 10)
        return String (seconds, 1);
    else
        return String (int (seconds));
}

//------------------------------------------------------------------------------

/** A unit test that always passes.
    This can be useful to diagnose continuous integration systems.
*/
class PassUnitTest : public UnitTest
{
public:
    PassUnitTest () : UnitTest ("Pass", "beast", runManual)
    {
    }

    void runTest ()
    {
        beginTestCase ("pass");

        pass ();
    }
};

static PassUnitTest passUnitTest;

//------------------------------------------------------------------------------

/** A unit test that always fails.
    This can be useful to diagnose continuous integration systems.
*/
class FailUnitTest : public UnitTest
{
public:
    FailUnitTest () : UnitTest ("Fail", "beast", runManual)
    {
    }

    void runTest ()
    {
        beginTestCase ("fail");

        fail ("Intentional failure");
    }
};

static FailUnitTest failUnitTest;

}

//------------------------------------------------------------------------------

class UnitTestUtilitiesTests : public UnitTest
{
public:
    UnitTestUtilitiesTests () : UnitTest ("UnitTestUtilities", "beast")
    {
    }

    void testPayload ()
    {
        using namespace UnitTestUtilities;

        int const maxBufferSize = 4000;
        int const minimumBytes = 1;
        int const numberOfItems = 100;
        int64 const seedValue = 50;

        beginTestCase ("Payload");

        Payload p1 (maxBufferSize);
        Payload p2 (maxBufferSize);

        for (int i = 0; i < numberOfItems; ++i)
        {
            p1.repeatableRandomFill (minimumBytes, maxBufferSize, seedValue);
            p2.repeatableRandomFill (minimumBytes, maxBufferSize, seedValue);

            expect (p1 == p2, "Should be equal");
        }
    }

    void runTest ()
    {
        testPayload ();
    }
};

static UnitTestUtilitiesTests unitTestUtilitiesTests;

}  // namespace beast
